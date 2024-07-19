#pragma once

#include <vulkan/vulkan.h>

// clang-format off
namespace vks {
	#define INIT_VK_STRUCT(name, type) struct name : public Vk##name { name() : Vk##name({type}) {} }

	INIT_VK_STRUCT(Win32SurfaceCreateInfoKHR, VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
	INIT_VK_STRUCT(PhysicalDeviceSynchronization2Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
	INIT_VK_STRUCT(PhysicalDeviceHostQueryResetFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES);
	INIT_VK_STRUCT(PhysicalDeviceDynamicRenderingFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
	INIT_VK_STRUCT(PhysicalDeviceDescriptorIndexingFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);
	INIT_VK_STRUCT(PhysicalDeviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
	INIT_VK_STRUCT(ImageCreateInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	INIT_VK_STRUCT(ImageViewCreateInfo, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
	INIT_VK_STRUCT(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
	INIT_VK_STRUCT(CommandPoolCreateInfo, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
	INIT_VK_STRUCT(CommandBufferAllocateInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
	INIT_VK_STRUCT(CopyBufferToImageInfo2, VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2);
	INIT_VK_STRUCT(BufferImageCopy2, VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2);
	INIT_VK_STRUCT(ImageCopy);
	INIT_VK_STRUCT(ImageMemoryBarrier2, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
	INIT_VK_STRUCT(DependencyInfo, VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
	INIT_VK_STRUCT(BlitImageInfo2, VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2);
	INIT_VK_STRUCT(CommandBufferBeginInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
	INIT_VK_STRUCT(ImageBlit2, VK_STRUCTURE_TYPE_IMAGE_BLIT_2);
	INIT_VK_STRUCT(SubmitInfo2, VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
	INIT_VK_STRUCT(CommandBufferSubmitInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
	INIT_VK_STRUCT(BufferDeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	INIT_VK_STRUCT(AccelerationStructureGeometryKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
	INIT_VK_STRUCT(AccelerationStructureGeometryTrianglesDataKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR);
	INIT_VK_STRUCT(AccelerationStructureBuildGeometryInfoKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
	INIT_VK_STRUCT(AccelerationStructureBuildSizesInfoKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
	INIT_VK_STRUCT(AccelerationStructureCreateInfoKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	INIT_VK_STRUCT(AccelerationStructureBuildRangeInfoKHR, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	INIT_VK_STRUCT(PhysicalDeviceBufferDeviceAddressFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
	INIT_VK_STRUCT(PhysicalDeviceAccelerationStructureFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
	INIT_VK_STRUCT(PhysicalDeviceRayTracingPipelineFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
	INIT_VK_STRUCT(ShaderModuleCreateInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
	INIT_VK_STRUCT(PipelineShaderStageCreateInfo, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
	INIT_VK_STRUCT(DescriptorPoolCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
	INIT_VK_STRUCT(DescriptorSetAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
	INIT_VK_STRUCT(WriteDescriptorSetAccelerationStructureKHR, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
	INIT_VK_STRUCT(WriteDescriptorSet, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	INIT_VK_STRUCT(SwapchainCreateInfoKHR, VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
	INIT_VK_STRUCT(SemaphoreCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
	INIT_VK_STRUCT(AcquireNextImageInfoKHR, VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR);
	INIT_VK_STRUCT(SemaphoreSubmitInfo, VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
	INIT_VK_STRUCT(PresentInfoKHR, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
	INIT_VK_STRUCT(AccelerationStructureInstanceKHR);
    INIT_VK_STRUCT(StridedDeviceAddressRegionKHR);
    INIT_VK_STRUCT(PhysicalDeviceRayTracingPipelinePropertiesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
    INIT_VK_STRUCT(PhysicalDeviceAccelerationStructurePropertiesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
    INIT_VK_STRUCT(DescriptorSetVariableDescriptorCountAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
    INIT_VK_STRUCT(SamplerCreateInfo, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    INIT_VK_STRUCT(DescriptorSetLayoutBindingFlagsCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    INIT_VK_STRUCT(PhysicalDeviceScalarBlockLayoutFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES);
}
// clang-format on
