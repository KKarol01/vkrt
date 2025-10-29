#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/utils.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/to_vk.hpp>

namespace eng
{
namespace gfx
{

void StagingBuffer::init(SubmitQueue* queue, const Callback<void(Handle<Buffer>)>& on_buffer_resize)
{
    auto* r = Engine::get().renderer;
    buffer = Buffer{ BufferDescriptor{ "staging buffer", CAPACITY,
                                       BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT | BufferUsage::CPU_ACCESS } };
    VkBufferMetadata::init(buffer);
    this->queue = queue;
    this->on_buffer_resize = on_buffer_resize;
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    dummy_sync = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "sbuf dummy sync" });
    allocations.reserve(128);
}

void StagingBuffer::resize(Handle<Buffer> buffer, size_t newsize)
{
    auto& old = buffer.get();
    if(newsize <= old.capacity)
    {
        old.size = std::min(old.size, newsize);
        return;
    }
    const auto info = BufferDescriptor{ old.name, newsize, old.usage };
    Buffer newbuffer{ info };
    VkBufferMetadata::init(newbuffer);
    newbuffer.size = old.size;
    if(newbuffer.memory) { memcpy(newbuffer.memory, old.memory, old.size); }
    else if(old.size > 0)
    {
        flush(); // flush any previous transactions that might've been done for that buffer.
        get_transaction().cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                       PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW);
        get_transaction().cmd->copy(newbuffer, old, 0, { 0, old.size });
        // flush()->wait_cpu(~0ull); // copy contents to new buffer and wait for completion before destroying the old one.
    }
    retired_bufs.push_back(RetiredBuffer{ &get_transaction(), old });
    old = std::move(newbuffer);
    if(on_buffer_resize) { on_buffer_resize(buffer); }
#ifdef ENG_SBUF_DEBUG_STATS
    ++debugstats.buf_resize_count;
#endif
}

void StagingBuffer::copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range)
{
    if(range.size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->capacity < dst_offset + range.size) { resize(dst, dst_offset + range.size); }
    if(dst->memory && src->memory)
    {
        memcpy((std::byte*)dst->memory + dst_offset, (const std::byte*)src->memory + range.offset, range.size);
    }
    else { get_transaction().cmd->copy(dst.get(), src.get(), dst_offset, range); }
    dst->size = std::max(dst->size, dst_offset + range.size);
}

void StagingBuffer::copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size)
{
    if(src_size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->capacity < dst_offset + src_size) { resize(dst, dst_offset + src_size); }
    if(dst->memory) { memcpy((std::byte*)dst->memory + dst_offset, src, src_size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < src_size)
        {
            auto alloc = allocate(src_size - uploaded);
            memcpy(alloc.mem, (const std::byte*)src + uploaded, alloc.size);
            get_transaction().cmd->copy(dst.get(), buffer, dst_offset + uploaded, { alloc.offset, alloc.size });
            uploaded += alloc.size;
        }
    }
    dst->size = std::max(dst->size, dst_offset + src_size);
}

void StagingBuffer::copy(Handle<Image> dst, const void* const src, ImageLayout dstlayout)
{
    auto& img = dst.get();
    const auto block_data = GetBlockData(img.format);
    const auto bx = img.width / block_data.x;
    const auto by = img.height / block_data.y;
    const auto bz = img.depth / block_data.z;
    const auto bcount = bx * by * bz;
    const auto rowsize = bx * block_data.size;
    const auto imgsize = bcount * block_data.size;

    get_transaction().cmd->barrier(img, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::TRANSFER_BIT,
                                   PipelineAccess::TRANSFER_WRITE_BIT, dst->current_layout, ImageLayout::TRANSFER_DST);
    size_t upbytes = 0;
    while(upbytes < imgsize)
    {
        const auto upsize = imgsize - upbytes;
        const auto uprows = upbytes / rowsize;
        auto alloc = allocate(upsize);
        if(alloc.size < rowsize)
        {
            reset();
            continue;
        }
        const auto urows = (uprows % img.height);
        const auto uslices = (uprows / img.height);
        const auto alrows = (alloc.size / rowsize);
        const auto alslices = std::max((alrows / img.height), 1ull);
        assert(alslices == 1);
        const auto copy = Vks(VkBufferImageCopy2{
            .bufferOffset = alloc.offset + upbytes,
            .imageSubresource = { gfx::to_vk(img.deduce_aspect()), 0, 0, 1 },
            .imageOffset = { 0, (int32_t)(urows * block_data.y), (int32_t)(uslices * block_data.z) },
            .imageExtent = { img.width, (uint32_t)(alrows * block_data.y), (uint32_t)(alslices * block_data.z) } });
        memcpy(alloc.mem, (std::byte*)src + upbytes, alloc.size);
        get_transaction().cmd->copy(img, buffer, &copy, 1);
        upbytes += alrows * rowsize;
    }
    if(dstlayout != ImageLayout::UNDEFINED) { barrier(dst, dstlayout); }
    assert(upbytes == imgsize);
}

void StagingBuffer::copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy)
{
    // dst and src may be the same. if that's the case, need to make sure to check if ranges overlap.
    const auto oldsrcl = src->current_layout;
    assert(oldsrcl != ImageLayout::UNDEFINED);
    const auto dstrange = ImageSubRange{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    // get_transaction().cmd->barrier(dst.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
    //                                PipelineAccess::TRANSFER_WRITE_BIT, dst->current_layout, ImageLayout::TRANSFER_DST, dstrange);
    // get_transaction().cmd->barrier(src.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
    //                                PipelineAccess::TRANSFER_READ_BIT, src->current_layout, ImageLayout::TRANSFER_SRC, srcrange);
    get_transaction().cmd->copy(dst.get(), src.get(), copy, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC);
    // get_transaction().cmd->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
    //                                PipelineAccess::NONE, ImageLayout::TRANSFER_DST, dst_final_layout, dstrange);
    // get_transaction().cmd->barrier(src.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT,
    //                                PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::TRANSFER_SRC, oldsrcl, srcrange);
}

void StagingBuffer::blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit)
{
    const auto oldsrcl = src->current_layout;
    assert(oldsrcl != ImageLayout::UNDEFINED);
    const auto dstrange = ImageSubRange{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    // get_transaction().cmd->barrier(dst.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
    //                                PipelineAccess::TRANSFER_WRITE_BIT, dst->current_layout, ImageLayout::TRANSFER_DST, dstrange);
    // get_transaction().cmd->barrier(src.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
    //                                PipelineAccess::TRANSFER_READ_BIT, src->current_layout, ImageLayout::TRANSFER_SRC, srcrange);
    get_transaction().cmd->blit(dst.get(), src.get(), blit, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC, ImageFilter::LINEAR);
    // get_transaction().cmd->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
    //                                PipelineAccess::NONE, ImageLayout::TRANSFER_DST, dst_final_layout, dstrange);
    // get_transaction().cmd->barrier(src.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT,
    //                                PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::TRANSFER_SRC, oldsrcl, srcrange);
}

void StagingBuffer::barrier()
{
    get_transaction().cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                   PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW);
}

void StagingBuffer::barrier(Handle<Image> image, ImageLayout dstlayout)
{
    get_transaction().cmd->barrier(image.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                   PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, image->current_layout, dstlayout);
}

void StagingBuffer::barrier(Handle<Image> image, ImageLayout dstlayout, ImageSubRange range)
{
    get_transaction().cmd->barrier(image.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                   PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, image->current_layout,
                                   dstlayout, range);
}

// main idea of flush:
// 1. if no transactions staged: return dummy, always signaled, sync.
// 2. go over the staged ones, changed their states to pending
// 3. submit.
Sync* StagingBuffer::flush()
{
    if(staged.empty()) { return dummy_sync; }
    auto* sem = staged.front()->sync;
    for(auto* e : staged)
    {
        if(e->state == Transaction::State::RECORDING) { cmdpool->end(e->cmd); }
        e->state = Transaction::State::PENDING;
        queue->with_cmd_buf(e->cmd);
        queue->signal_sync(e->sync, PipelineStage::TRANSFER_BIT);
    }
    queue->submit();
    staged.clear();
#ifdef ENG_SBUF_DEBUG_STATS
    ++debugstats.flush_count;
#endif
    return sem;
}

// main idea of reset
// 1. flush any staged
// 2. start removing oldest transactions until failure.
// 3. on each success, reset sync, cmd, add them to their free lists
// 4. remove all the allocations belonging to the deleted transactions
//    and put them in the free allocs for later allocs.
// 5. actually remove transactions with pop_front to not disturb pointers/refs to other transactions.
// 6. optionally, if all the transactions have completed and have been deleted,
//    reset everything, with buffer head and free allocs
void StagingBuffer::reset()
{
    flush();
    assert(staged.empty());
    auto it = transactions.begin();
    while(it != transactions.end())
    {
        if(it->state == Transaction::State::PENDING && it->sync->wait_cpu(0) == VK_SUCCESS)
        {
            it->sync->reset();
            cmdpool->reset(it->cmd);
            cmds.push_back(it->cmd);
            syncs.push_back(it->sync);
            ++it;
            continue;
        }
        break;
    }
    bool sort_free_allocs = false;
    for(auto dit = transactions.begin(); dit != it; ++dit)
    {
        {
            for(auto& e : allocations)
            {
                if(e.transaction == &*dit)
                {
                    assert(e.transaction);
                    sort_free_allocs = true;
                    e.transaction = nullptr;
                    free_allocs.push_back(e);
                }
            }
            std::erase_if(allocations, [dit](const auto& e) { return !e.transaction; });
        }
        {
            for(auto& e : retired_bufs)
            {
                assert(e.transaction);
                if(e.transaction == &*dit) { VkBufferMetadata::destroy(e.buf); }
            }
            std::erase_if(retired_bufs, [dit](const auto& e) { return e.transaction == &*dit; });
        }
    }
    if(sort_free_allocs)
    {
        std::sort(free_allocs.begin(), free_allocs.end(),
                  [](const auto& a, const auto& b) { return a.realsize < b.realsize; });
    }
    const auto delcount = std::distance(transactions.begin(), it);
    for(auto i = 0u; i < delcount; ++i)
    {
        transactions.pop_front();
    }
    if(transactions.empty())
    {
        assert(allocations.empty());
        assert(retired_bufs.empty());
        assert(cmds.size());
        assert(syncs.size());
        head = 0;
        free_allocs.clear();
#ifdef ENG_SBUF_DEBUG_STATS
        debugstats.reset();
#endif
    }
}

// main idea of this algorithm:
// 1. if there is free memory - allocate.
// 2. if no free space in the buffer, look for free alocs
// 3. allocs are sorted in ascending size, so pick the first one that is big enough.
// 4. if there is no big enough, get the biggest one.
// 5. if no free allocs, and no memory, call reset, and repeat.
StagingBuffer::Allocation StagingBuffer::allocate(size_t size)
{
    while(true)
    {
        auto free_space = CAPACITY - head;
        if(free_space == 0)
        {
            auto it = std::upper_bound(free_allocs.begin(), free_allocs.end(), size,
                                       [](const auto size, const auto& iter) { return size < iter.realsize; });
            if(it == free_allocs.end() && free_allocs.size() > 0) { it = it - 1; }
            if(it != free_allocs.end())
            {
                auto fa = free_allocs.back();
                free_allocs.pop_back();
                fa.transaction = &get_transaction();
                fa.size = std::min(size, fa.realsize);
#ifdef ENG_SBUF_DEBUG_STATS
                ++debugstats.freealloc_count;
#endif
                return fa;
            }
            reset();
            continue;
        }
        assert(free_space >= ALIGNMENT && free_space % ALIGNMENT == 0);
        const auto aligned_size = std::min(align_up2(size, ALIGNMENT), free_space);
        assert(aligned_size % ALIGNMENT == 0 && aligned_size <= free_space);
        void* mem = (std::byte*)buffer.memory + head;
        head += aligned_size;
        Allocation alloc{
            &get_transaction(),          &buffer, mem, (uintptr_t)mem - (uintptr_t)buffer.memory, aligned_size,
            std::min(aligned_size, size)
        };
        allocations.push_back(alloc);
#ifdef ENG_SBUF_DEBUG_STATS
        ++debugstats.linalloc_count;
#endif
        return alloc;
    }
}

// 1. find one recording
// 2. if failed, make new one.
StagingBuffer::Transaction& StagingBuffer::get_transaction()
{
    if(transactions.empty())
    {
#ifdef ENG_SBUF_DEBUG_STATS
        ++debugstats.transaction_count;
#endif
        transactions.emplace_back();
    }
    if(transactions.back().state != Transaction::State::RECORDING)
    {
        auto& back = transactions.back();
        if(back.state == Transaction::State::UNINITIALIZED)
        {
            back.state = Transaction::State::INITIAL;
            back.cmd = get_cmd();
            back.sync = get_sync();
        }
        if(back.state == Transaction::State::INITIAL)
        {
            back.state = Transaction::State::RECORDING;
            cmdpool->begin(back.cmd);
            staged.push_back(&back);
        }
        if(back.state == Transaction::State::PENDING)
        {
#ifdef ENG_SBUF_DEBUG_STATS
            ++debugstats.transaction_count;
#endif
            transactions.emplace_back();
            return get_transaction();
        }
    }
    return transactions.back();
}

CommandBuffer* StagingBuffer::get_cmd()
{
    if(cmds.empty())
    {
#ifdef ENG_SBUF_DEBUG_STATS
        ++debugstats.cmd_count;
#endif
        return cmdpool->allocate();
    }
    auto* back = cmds.back();
    cmds.pop_back();
    return back;
}

Sync* StagingBuffer::get_sync()
{
    if(syncs.empty())
    {
#ifdef ENG_SBUF_DEBUG_STATS
        ++debugstats.sync_count;
#endif
        return Engine::get().renderer->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "staging sync" });
    }
    auto* back = syncs.back();
    syncs.pop_back();
    return back;
}

#ifdef ENG_SBUF_DEBUG_STATS
void StagingBuffer::DebugStats::reset()
{
    ENG_LOG("[SBUF STATS]\ntc: {}, cmd: {}, sync: {}, bres: {}, fc: {}, lin: {}, free: {}", transaction_count,
            cmd_count, sync_count, buf_resize_count, flush_count, linalloc_count, freealloc_count);
    flush_count = 0;
    linalloc_count = 0;
    freealloc_count = 0;
    buf_resize_count = 0;
    transaction_count = 0;
    sync_count = 0;
    cmd_count = 0;
}
#endif

} // namespace gfx
} // namespace eng
