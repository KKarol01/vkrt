#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <deque>

static size_t align_up2(size_t val, size_t al) { return (val + al - 1) & ~(al - 1); }

StagingBuffer::StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept
    : queue(queue), staging_buffer(&RendererVulkan::get_instance()->get_buffer(staging_buffer)) {
    if(!queue) {
        ENG_WARN("Queue is nullptr");
        assert(false);
    }
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    assert(cmdpool);
    submission_thread_fence = queue->make_fence(true);
    assert(submission_thread_fence);
    for(auto i = 0; i < 2; ++i) {
        submissions[i] = std::make_unique<Submission>();
        cmds[i] = cmdpool->allocate();
        assert(submissions[i]);
        assert(cmds[i]);
    }
}

StagingBuffer::StagingBuffer(StagingBuffer&& o) noexcept { *this = std::move(o); }

StagingBuffer& StagingBuffer::operator=(StagingBuffer&& o) noexcept {
    if(o.on_submit_complete_thread.joinable()) { o.on_submit_complete_thread.join(); }
    queue = std::exchange(o.queue, nullptr);
    cmdpool = std::exchange(o.cmdpool, nullptr);
    for(int i = 0; i < 2; ++i) {
        cmds[i] = std::exchange(o.cmds[i], nullptr);
        submissions[i] = std::exchange(o.submissions[i], nullptr);
    }
    staging_buffer = std::exchange(o.staging_buffer, nullptr);
    submission_done = o.submission_done.load();
    submission_thread_fence = std::exchange(o.submission_thread_fence, nullptr);
    return *this;
}

void StagingBuffer::submit(VkFence fence) {
    get_submission().fence = fence;
    process_submission();
}

void StagingBuffer::submit_wait(VkFence fence) {
    get_submission().fence = fence;
    process_submission();
    submission_done.wait(true);
}

void StagingBuffer::swap_submissions() {
    std::swap(submissions[0], submissions[1]);
    std::swap(cmds[0], cmds[1]);
    *submissions[0] = Submission{};
    cmdpool->reset(cmds[0]);
}

StagingBuffer& StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, std::span<const std::byte> data) {
    if(!buffer) {
        ENG_ERROR("Invalid buffer. Not Sending");
        assert(false);
        return *this;
    }
    if(data.size_bytes() == 0) {
        ENG_WARN("Upload data size is 0. Not Sending");
        assert(false);
        return *this;
    }
    get_submission().transfers.push_back(TransferBuffer{
        .handle = buffer, .offset = offset, .data = std::vector<std::byte>{ data.begin(), data.end() } });
    return *this;
}

void StagingBuffer::resize(Handle<Buffer> buffer, size_t new_size) { auto& r = *RendererVulkan::get_instance(); }

void StagingBuffer::process_submission() {
    auto& r = *RendererVulkan::get_instance();

    std::vector<VkCopyBufferInfo2> buffer_copy_infos;
    std::deque<VkBufferCopy2> buffer_copies;
    // todo: implement subdividing data if all doesn't fit at once
    for(const auto& e : get_submission().transfers) {
        if(auto* tb = std::get_if<TransferBuffer>(&e)) {
            const auto offset = staging_buffer->size();
            const auto pushed_size = push_data(tb->data);
            buffer_copy_infos.push_back(Vks(VkCopyBufferInfo2{
                .srcBuffer = staging_buffer->buffer,
                .dstBuffer = r.get_buffer(tb->handle).buffer,
                .regionCount = 1,
                .pRegions = &buffer_copies.emplace_back(Vks(VkBufferCopy2{
                    .srcOffset = offset, .dstOffset = tb->offset, .size = pushed_size })) }));
        } else {
            assert(false);
        }
    }
    const auto cmd = cmdpool->begin(cmds[0]);
    auto mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                             .srcAccessMask = 0,
                                             .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                             .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT });
    const auto dep_info = Vks(VkDependencyInfo{ .memoryBarrierCount = 1ul, .pMemoryBarriers = &mem_barrier });
    vkCmdPipelineBarrier2(cmd, &dep_info);
    for(const auto& e : buffer_copy_infos) {
        vkCmdCopyBuffer2(cmd, &e);
    }
    mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        .dstAccessMask = 0 });
    vkCmdPipelineBarrier2(cmd, &dep_info);
    cmdpool->end(cmd);

    VkFence fence = get_submission().fence;
    if(!fence) { fence = submission_thread_fence; }
    submission_done.wait(true);
    submission_done = false;
    queue->with_cmd_buf(cmd).with_fence(fence).submit();
    on_submit_complete_thread = std::jthread{ [this, fence] {
        queue->wait_fence(fence, -1ull);
        submission_done = true;
        if(fence == submission_thread_fence) { queue->reset_fence(submission_thread_fence); }
    } };
    swap_submissions();
}

size_t StagingBuffer::push_data(const std::vector<std::byte>& data) {
    assert(data.size() <= staging_buffer->free_space());
    if(data.size() > staging_buffer->free_space()) {
        // todo: implement subdividing data if all doesn't fit at once
        ENG_WARN("Data is too big for staging buffer: {} > {}", data.size(), staging_buffer->capacity());
        return 0ull;
    }
    memcpy((std::byte*)staging_buffer->mapped + staging_buffer->size(), data.data(), data.size());
    staging_buffer->_size = std::min(staging_buffer->capacity(), staging_buffer->size() + align_up2(data.size(), 8ull));
    return data.size();
}
