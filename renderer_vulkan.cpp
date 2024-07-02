#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"


Buffer::Buffer(const std::string& name, uint64_t size, VkBufferUsageFlags usage, bool map) {
	RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
	vk::BufferCreateInfo binfo;
	VmaAllocationCreateInfo vinfo{};
	const auto use_bda = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) > 0;

	binfo.size = size;
	binfo.usage = usage;

	vinfo.usage = VMA_MEMORY_USAGE_AUTO;
	if(map) { vinfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; }
	if(use_bda) { vinfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; }

	VmaAllocationInfo vainfo{};
	if(vmaCreateBuffer(renderer->vma, &binfo, &vinfo, &buffer, &alloc, &vainfo) != VK_SUCCESS) {
		throw std::runtime_error{ "Could not create buffer" };
	}

	mapped = vainfo.pMappedData;

	if(use_bda) {
		vk::BufferDeviceAddressInfo bdainfo;
		bdainfo.buffer = buffer;
		bda = vkGetBufferDeviceAddress(renderer->dev, &bdainfo);
	}

	set_debug_name(buffer, name);
}


Image::Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers, VkFormat format,
	  VkSampleCountFlagBits samples, VkImageUsageFlags usage)
	: format(format), mips(mips), layers(layers) {
	RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
	vk::ImageCreateInfo iinfo;

	int dims = -1;
	if(width > 1) { ++dims; }
	if(height > 1) { ++dims; }
	if(depth > 1) { ++dims; }
	VkImageType types[]{ VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };

	iinfo.imageType = types[dims];
	iinfo.format = format;
	iinfo.extent = { width, height, depth };
	iinfo.mipLevels = mips;
	iinfo.arrayLayers = layers;
	iinfo.samples = samples;
	iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	iinfo.usage = usage;
	iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo vmainfo{};
	vmainfo.usage = VMA_MEMORY_USAGE_AUTO;

	if(vmaCreateImage(renderer->vma, &iinfo, &vmainfo, &image, &alloc, nullptr) != VK_SUCCESS) {
		throw std::runtime_error{ "Could not create image" };
	}

	VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };

	VkImageAspectFlags view_aspect{ VK_IMAGE_ASPECT_COLOR_BIT };
	if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) { view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; }

	vk::ImageViewCreateInfo ivinfo;
	ivinfo.image = image;
	ivinfo.viewType = view_types[dims];
	ivinfo.components = {};
	ivinfo.format = format;
	ivinfo.subresourceRange = { .aspectMask = view_aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 };

	if(vkCreateImageView(renderer->dev, &ivinfo, nullptr, &view) != VK_SUCCESS) {
		throw std::runtime_error{ "Could not create image default view" };
	}

	set_debug_name(image, name);
	set_debug_name(view, std::format("{}_default_view", name));
}

void Image::transition_layout(VkImageLayout dst, bool from_undefined) {
	RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
	vk::ImageMemoryBarrier2 imgb;
	imgb.image = image;
	imgb.oldLayout = from_undefined ? VK_IMAGE_LAYOUT_UNDEFINED : current_layout;
	imgb.newLayout = dst;
	imgb.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	imgb.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	imgb.srcAccessMask = VK_ACCESS_NONE;
	imgb.dstAccessMask = VK_ACCESS_NONE;
	imgb.subresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = mips,
		.baseArrayLayer = 0,
		.layerCount = layers,
	};

	vk::CommandBufferBeginInfo cmdbi;
	cmdbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(renderer->cmd, &cmdbi);

	vk::DependencyInfo dep;
	dep.pImageMemoryBarriers = &imgb;
	dep.imageMemoryBarrierCount = 1;
	vkCmdPipelineBarrier2(renderer->cmd, &dep);

	vkEndCommandBuffer(renderer->cmd);

	vk::CommandBufferSubmitInfo cmdsinfo;
	cmdsinfo.commandBuffer = renderer->cmd;

	vk::SubmitInfo2 sinfo;
	sinfo.commandBufferInfoCount = 1;
	sinfo.pCommandBufferInfos = &cmdsinfo;
	vkQueueSubmit2(renderer->gq, 1, &sinfo, nullptr);
	vkQueueWaitIdle(renderer->gq);

	current_layout = dst;
}

void RendererVulkan::render_model(Model& model) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

	auto& meshes = model.meshes;

    uint64_t num_vertices = 0, num_indices = 0;
    for(const auto& m : meshes) {
        num_vertices += m.vertices.size();
        num_indices += m.indices.size();
    }

    vertices.reserve(num_vertices);
    indices.reserve(num_indices);

    for(const auto& m : meshes) {
        const auto num_indices = indices.size();
        const auto num_vertices = vertices.size();
        vertices.insert(vertices.end(), m.vertices.begin(), m.vertices.end());
        indices.insert(indices.end(), m.indices.begin(), m.indices.end());

        for(uint64_t i = num_indices; i < indices.size(); ++i) {
            indices.at(i) += num_vertices;
        }
    }

    model.num_vertices = vertices.size();
    model.num_indices = indices.size();

    vertex_buffer = Buffer{ "vertex_buffer", vertices.size() * sizeof(vertices[0]),
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            true };
    index_buffer = Buffer{ "index_buffer", indices.size() * sizeof(indices[0]),
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           true };

    memcpy(vertex_buffer.mapped, vertices.data(), vertices.size() * sizeof(vertices[0]));
    memcpy(index_buffer.mapped, indices.data(), indices.size() * sizeof(indices[0]));
}
