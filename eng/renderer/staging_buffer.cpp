#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <deque>

static size_t align_up2(size_t val, size_t al) { return (val + al - 1) & ~(al - 1); }

namespace gfx {

StagingBuffer::StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept
    : queue(queue), staging_buffer(&RendererVulkan::get_buffer(staging_buffer)) {
    if(!queue) {
        ENG_WARN("Queue is nullptr");
        assert(false);
    }
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    assert(cmdpool);
    submission_thread_fence = queue->make_fence(false);
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

StagingBuffer& StagingBuffer::send_to(Handle<Buffer> dst_buffer, size_t dst_offset, Handle<Buffer> src_buffer,
                                      size_t src_offset, size_t size) {
    if(!dst_buffer || !src_buffer) {
        ENG_ERROR("Invalid src {} or dst {} buffer handles", *dst_buffer, *src_buffer);
        return *this;
    }
    if(size == 0) {
        ENG_WARN("Upload data size is 0. Not Sending");
        assert(false);
        return *this;
    }
    get_submission().transfers.push_back(TransferFromBuffer{
        .dst_buffer = dst_buffer, .dst_offset = dst_offset, .src_buffer = src_buffer, .src_offset = src_offset, .size = size });
    return *this;
}

void StagingBuffer::submit(VkFence fence) {
    get_submission().fence = fence;
    process_submission();
}

void StagingBuffer::submit_wait(VkFence fence) {
    get_submission().fence = fence;
    process_submission();
    submission_done.wait(false);
}

void StagingBuffer::swap_submissions() {
    std::swap(submissions[0], submissions[1]);
    std::swap(cmds[0], cmds[1]);
    *submissions[0] = Submission{};
    cmdpool->reset(cmds[0]);
    staging_buffer->_size = 0ull;
}

StagingBuffer& StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, std::span<const std::byte> data) {
    if(!buffer) {
        ENG_ERROR("Invalid buffer. Not Sending");
        return *this;
    }
    if(data.size_bytes() == 0) {
        ENG_ERROR("Upload data size is 0. Not Sending");
        return *this;
    }
    const size_t real_offset = offset == ~0ull ? RendererVulkan::get_buffer(buffer).size() : offset;
    if(real_offset + data.size_bytes() > RendererVulkan::get_buffer(buffer).capacity()) {
        if(!RendererVulkan::get_buffer(buffer).is_resizable) {
            ENG_ERROR("Cannot resize the buffer!");
            return *this;
        }
        resize(buffer, real_offset + data.size_bytes());
    }
    get_submission().transfers.push_back(TransferBuffer{
        .handle = buffer, .offset = offset, .data = std::vector<std::byte>{ data.begin(), data.end() } });
    return *this;
}

StagingBuffer& StagingBuffer::send_to(Handle<Image> image, VkImageLayout final_layout, const VkBufferImageCopy2 region,
                                      std::span<const std::byte> data) {
    if(!image) {
        ENG_ERROR("Invalid image. Not Sending");
        return *this;
    }
    if(data.empty()) {
        ENG_ERROR("Upload data size is 0. Not Sending");
        return *this;
    }
    transition_image(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, false);
    get_submission().transfers.push_back(TransferImage{
        .handle = image, .final_layout = final_layout, .data = { data.begin(), data.end() } });
    return *this;
}

void StagingBuffer::resize(Handle<Buffer> buffer, size_t new_size) {
    auto& r = *RendererVulkan::get_instance();
    auto& b = r.get_buffer(buffer);
    if(!b.is_resizable) { return; }
    auto vk_info = b.vk_info;
    vk_info.size = new_size;
    Buffer nb = Buffer{ b.name, b.dev, b.vma, b.vk_info, b.vma_info };

    const auto region = Vks(VkBufferCopy2{ .srcOffset = 0ull, .dstOffset = 0ull, .size = b.size() });
    const auto copy_info =
        Vks(VkCopyBufferInfo2{ .srcBuffer = b.buffer, .dstBuffer = nb.buffer, .regionCount = 1, .pRegions = &region });
    auto mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                             .srcAccessMask = 0,
                                             .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                             .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT });
    const auto dep_info = Vks(VkDependencyInfo{ .memoryBarrierCount = 1ul, .pMemoryBarriers = &mem_barrier });
    const auto cmd = cmdpool->begin(cmds[0]);
    vkCmdPipelineBarrier2(cmd, &dep_info);
    vkCmdCopyBuffer2(cmd, &copy_info);
    mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        .dstAccessMask = 0 });
    vkCmdPipelineBarrier2(cmd, &dep_info);
    cmdpool->end(cmd);
    queue->with_cmd_buf(cmd).submit_wait(-1ull);
    nb._size = b.size();
    r.replace_buffer(buffer, std::move(nb));
}

VkImageMemoryBarrier2 StagingBuffer::generate_image_barrier(Handle<Image> image, VkImageLayout layout, bool is_final_layout) {
    auto& r = *RendererVulkan::get_instance();
    auto& i = r.get_image(image);
    return is_final_layout
               ? Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                            .dstAccessMask = VK_ACCESS_2_NONE,
                                            .oldLayout = i.current_layout,
                                            .newLayout = layout,
                                            .image = i.image,
                                            .subresourceRange = { .aspectMask = i.deduce_aspect(),
                                                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                                                  .layerCount = VK_REMAINING_ARRAY_LAYERS } })
               : Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                            .srcAccessMask = VK_ACCESS_2_NONE,
                                            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                            .oldLayout = i.current_layout,
                                            .newLayout = layout,
                                            .image = i.image,
                                            .subresourceRange = { .aspectMask = i.deduce_aspect(),
                                                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                                                  .layerCount = VK_REMAINING_ARRAY_LAYERS } });
}

void StagingBuffer::transition_image(Handle<Image> image, VkImageLayout layout, bool is_final_layout) {
    auto& r = *RendererVulkan::get_instance();
    auto& i = r.get_image(image);
    const auto barrier = generate_image_barrier(image, layout, is_final_layout);
    const auto dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier });
    const auto cmd = cmdpool->begin(cmds[0]);
    vkCmdPipelineBarrier2(cmd, &dep_info);
    cmdpool->end(cmd);
    queue->with_cmd_buf(cmd).submit_wait(-1ull); // wait because cmd might be pending on subsequent begins
    i.current_layout = layout;
}

void StagingBuffer::process_submission() {
    auto& r = *RendererVulkan::get_instance();

    std::vector<VkCopyBufferInfo2> buffer_copy_infos;
    std::vector<VkCopyBufferToImageInfo2> image_copy_infos;
    std::vector<VkImageMemoryBarrier2> image_barriers;
    std::deque<VkBufferCopy2> buffer_copies;
    std::deque<VkBufferImageCopy2> image_copies;
    // todo: implement subdividing data if all doesn't fit at once
    for(const auto& e : get_submission().transfers) {
        if(auto* tb = std::get_if<TransferBuffer>(&e)) {
            const auto offset = staging_buffer->size();
            const auto pushed_size = push_data(tb->data);
            const auto real_offset = tb->offset == ~0ull ? r.get_buffer(tb->handle).size() : tb->offset;
            assert(r.get_buffer(tb->handle).capacity() >= real_offset + tb->data.size());
            r.get_buffer(tb->handle)._size = std::max(r.get_buffer(tb->handle).size(), real_offset + tb->data.size());
            buffer_copy_infos.push_back(Vks(VkCopyBufferInfo2{
                .srcBuffer = staging_buffer->buffer,
                .dstBuffer = r.get_buffer(tb->handle).buffer,
                .regionCount = 1,
                .pRegions = &buffer_copies.emplace_back(Vks(VkBufferCopy2{
                    .srcOffset = offset, .dstOffset = real_offset, .size = pushed_size })) }));
        } else if(auto* ti = std::get_if<TransferImage>(&e)) {
            const auto offset = staging_buffer->size();
            const auto pushed_size = push_data(ti->data);
            image_copy_infos.push_back(Vks(VkCopyBufferToImageInfo2{
                .srcBuffer = staging_buffer->buffer,
                .dstImage = r.get_image(ti->handle).image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1,
                .pRegions = &image_copies.emplace_back(Vks(VkBufferImageCopy2{
                    .bufferOffset = offset,
                    .imageSubresource = { .aspectMask = r.get_image(ti->handle).deduce_aspect(), .mipLevel = 0, .layerCount = 1 },
                    .imageExtent = r.get_image(ti->handle).vk_info.extent })) }));
            image_barriers.push_back(generate_image_barrier(ti->handle, ti->final_layout, true));
            r.get_image(ti->handle).current_layout = ti->final_layout;
        } else if(auto* tb = std::get_if<TransferFromBuffer>(&e)) {
            buffer_copy_infos.push_back(Vks(VkCopyBufferInfo2{
                .srcBuffer = r.get_buffer(tb->src_buffer).buffer,
                .dstBuffer = r.get_buffer(tb->dst_buffer).buffer,
                .regionCount = 1,
                .pRegions = &buffer_copies.emplace_back(Vks(VkBufferCopy2{
                    .srcOffset = tb->src_offset, .dstOffset = tb->dst_offset, .size = tb->size })) }));
        } else {
            assert(false);
        }
    }
    const auto cmd = cmdpool->begin(cmds[0]);
    auto mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                             .srcAccessMask = 0,
                                             .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                             .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT });
    auto dep_info = Vks(VkDependencyInfo{ .memoryBarrierCount = 1u, .pMemoryBarriers = &mem_barrier });
    vkCmdPipelineBarrier2(cmd, &dep_info);
    for(const auto& e : buffer_copy_infos) {
        vkCmdCopyBuffer2(cmd, &e);
    }
    for(const auto& e : image_copy_infos) {
        vkCmdCopyBufferToImage2(cmd, &e);
    }
    mem_barrier = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        .dstAccessMask = 0 });
    dep_info.imageMemoryBarrierCount = (uint32_t)image_copy_infos.size();
    dep_info.pImageMemoryBarriers = image_barriers.data();
    vkCmdPipelineBarrier2(cmd, &dep_info);
    cmdpool->end(cmd);

    VkFence fence = get_submission().fence;
    if(!fence) { fence = submission_thread_fence; }

    // if(on_submit_complete_thread.joinable()) { // todo: make another fence, so this line can go lower
    //     on_submit_complete_thread.join();
    // }
    submission_done.wait(false);
    submission_done.store(false);
    queue->with_cmd_buf(cmd).with_fence(fence).submit_wait(-1ull);
    Submission* current_submission = &get_submission();
    on_submit_complete_thread = std::jthread{ [this, fence, subm = current_submission] {
        queue->wait_fence(fence, -1ull);
        if(fence == submission_thread_fence) { queue->reset_fence(submission_thread_fence); }
        for(auto& e : subm->transfers) {
            if(auto* tb = std::get_if<TransferBuffer>(&e)) {
                auto& b = RendererVulkan::get_buffer(tb->handle);
                b._size = std::max(b._size, tb->offset + tb->data.size());
            }
        }
        submission_done.store(true);
        submission_done.notify_one();
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

} // namespace gfx
