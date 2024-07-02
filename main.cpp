#include <volk/volk.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <vk-bootstrap/src/VkBootstrap.h>
#include <shaderc/shaderc.hpp>
#include <stb/stb_include.h>
#include <iostream>
#include <format>
#include "vulkan_structs.hpp"
#include "model_loader.hpp"

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

struct Window {
    uint32_t width, height;
    GLFWwindow* window;
    VkSurfaceKHR surface;
};

struct Camera {
    glm::mat4 get_view() const { return glm::lookAt(position, position + direction, glm::vec3{ 0.0f, 1.0f, 0.0f }); }

    glm::vec3 position{ 0.0f, 0.0f, 2.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

static Window window;
static Camera camera;

#include "engine.hpp"
#include "renderer_vulkan.hpp"

static void build_blas(Model& model) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());

    vks::AccelerationStructureGeometryTrianglesDataKHR triangles;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = renderer->vertex_buffer.bda;
    triangles.vertexStride = sizeof(Vertex);
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = renderer->index_buffer.bda;
    triangles.maxVertex = model.num_vertices - 1;
    triangles.transformData = {};

    vks::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    vks::AccelerationStructureBuildGeometryInfoKHR geometry_info;
    geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    geometry_info.geometryCount = 1;
    geometry_info.pGeometries = &geometry;

    const uint32_t max_primitives = model.num_indices / 3;
    vks::AccelerationStructureBuildSizesInfoKHR build_size_info;
    vkGetAccelerationStructureBuildSizesKHR(renderer->dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geometry_info, &max_primitives, &build_size_info);

    Buffer buffer_blas{ "blas_buffer", build_size_info.accelerationStructureSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    vks::AccelerationStructureCreateInfoKHR blas_info;
    blas_info.buffer = buffer_blas.buffer;
    blas_info.size = build_size_info.accelerationStructureSize;
    blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkAccelerationStructureKHR blas;
    vkCreateAccelerationStructureKHR(renderer->dev, &blas_info, nullptr, &blas);

    Buffer buffer_scratch{ "scratch_buffer", build_size_info.buildScratchSize,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    vks::AccelerationStructureBuildGeometryInfoKHR build_info;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;
    build_info.dstAccelerationStructure = blas;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.scratchData.deviceAddress = buffer_scratch.bda;

    vks::AccelerationStructureBuildRangeInfoKHR offset;
    offset.firstVertex = 0;
    offset.primitiveCount = max_primitives;
    offset.primitiveOffset = 0;
    offset.transformOffset = 0;

    vks::CommandBufferBeginInfo begin_info;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(renderer->cmd, &begin_info);
    VkAccelerationStructureBuildRangeInfoKHR* offsets[]{ &offset };
    vkCmdBuildAccelerationStructuresKHR(renderer->cmd, 1, &build_info, offsets);
    vkEndCommandBuffer(renderer->cmd);

    vks::CommandBufferSubmitInfo cmd_submit_info;
    cmd_submit_info.commandBuffer = renderer->cmd;

    vks::SubmitInfo2 submit_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_submit_info;
    vkQueueSubmit2(renderer->gq, 1, &submit_info, nullptr);
    vkDeviceWaitIdle(renderer->dev);

    renderer->blas = blas;
    renderer->blas_buffer = Buffer{ buffer_blas };
}

static void build_tlas(Model& model) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());

    // clang-format off
    VkTransformMatrixKHR transform{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.5f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    // clang-format on

    vks::AccelerationStructureInstanceKHR instance;
    instance.transform = transform;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = renderer->blas_buffer.bda;

    Buffer buffer_instance{ "tlas_instance_buffer", sizeof(instance) * 2,
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true };
    memcpy(buffer_instance.mapped, &instance, sizeof(instance));

    // clang-format off
    VkTransformMatrixKHR transform2{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, -0.5f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    // clang-format on
    instance.transform = transform2;
    memcpy((std::byte*)buffer_instance.mapped + sizeof(instance), &instance, sizeof(instance));

    vks::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = false;
    geometry.geometry.instances.data.deviceAddress = buffer_instance.bda;

    vks::AccelerationStructureBuildGeometryInfoKHR build_info;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;

    vks::AccelerationStructureBuildSizesInfoKHR build_size;
    const uint32_t max_primitives = 2;
    vkGetAccelerationStructureBuildSizesKHR(renderer->dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &max_primitives, &build_size);

    Buffer buffer_tlas{ "tlas_buffer", build_size.accelerationStructureSize,
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false };

    vks::AccelerationStructureCreateInfoKHR acc_info;
    acc_info.buffer = buffer_tlas.buffer;
    acc_info.size = build_size.accelerationStructureSize;
    acc_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VkAccelerationStructureKHR tlas;
    vkCreateAccelerationStructureKHR(renderer->dev, &acc_info, nullptr, &tlas);

    Buffer buffer_scratch{ "tlas_scratch_buffer", build_size.buildScratchSize,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    vks::AccelerationStructureBuildGeometryInfoKHR build_tlas;
    build_tlas.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_tlas.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_tlas.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_tlas.dstAccelerationStructure = tlas;
    build_tlas.geometryCount = 1;
    build_tlas.pGeometries = &geometry;
    build_tlas.scratchData.deviceAddress = buffer_scratch.bda;

    vks::AccelerationStructureBuildRangeInfoKHR build_range;
    build_range.primitiveCount = 2;
    build_range.primitiveOffset = 0;
    build_range.firstVertex = 0;
    build_range.transformOffset = 0;
    VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

    vks::CommandBufferBeginInfo begin_info;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(renderer->cmd, &begin_info);
    vkCmdBuildAccelerationStructuresKHR(renderer->cmd, 1, &build_tlas, build_ranges);
    vkEndCommandBuffer(renderer->cmd);

    vks::CommandBufferSubmitInfo cmd_submit_info;
    cmd_submit_info.commandBuffer = renderer->cmd;

    vks::SubmitInfo2 submit_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_submit_info;
    vkQueueSubmit2(renderer->gq, 1, &submit_info, nullptr);
    vkDeviceWaitIdle(renderer->dev);

    renderer->tlas = tlas;
    renderer->tlas_buffer = Buffer{ buffer_tlas };
}

static void compile_shaders() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    shaderc::Compiler c;

    static const auto read_file = [](const std::filesystem::path& path) {
        std::ifstream file{ path };
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    std::filesystem::path files[]{
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "closest_hit.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss2.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "raygen.glsl",
    };
    shaderc_shader_kind kinds[]{ shaderc_closesthit_shader, shaderc_miss_shader, shaderc_miss_shader, shaderc_raygen_shader };
    std::vector<std::vector<uint32_t>> compiled_modules;
    std::vector<VkShaderModule> modules;

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    for(int i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        auto res = c.CompileGlslToSpv(read_file(files[i]), kinds[i], files[i].filename().string().c_str(), options);

        if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
            throw std::runtime_error{ std::format("Could not compile shader: {}, because: \"{}\"", files[i].string(), res.GetErrorMessage()) };
        }

        compiled_modules.emplace_back(res.begin(), res.end());

        vks::ShaderModuleCreateInfo module_info;
        module_info.codeSize = compiled_modules.back().size() * sizeof(compiled_modules.back()[0]);
        module_info.pCode = compiled_modules.back().data();
        vkCreateShaderModule(renderer->dev, &module_info, nullptr, &modules.emplace_back());
    }

    renderer->shader_modules = modules;
}

static void build_rtpp() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
    accelerationStructureLayoutBinding.binding = 0;
    accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureLayoutBinding.descriptorCount = 1;
    accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
    resultImageLayoutBinding.binding = 1;
    resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultImageLayoutBinding.descriptorCount = 1;
    resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding uniformBufferBinding{};
    uniformBufferBinding.binding = 2;
    uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferBinding.descriptorCount = 1;
    uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    std::vector<VkDescriptorSetLayoutBinding> bindings({ accelerationStructureLayoutBinding, resultImageLayoutBinding, uniformBufferBinding });

    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;

    VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI{};
    descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetlayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorSetlayoutCI.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(renderer->dev, &descriptorSetlayoutCI, nullptr, &descriptorSetLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
    vkCreatePipelineLayout(renderer->dev, &pipelineLayoutCI, nullptr, &pipelineLayout);

    /*
            Setup ray tracing shader groups
    */
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;

    // Ray generation group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stage.module = renderer->shader_modules.at(3);
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stage.module = renderer->shader_modules.at(1);
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stage.module = renderer->shader_modules.at(2);
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Closest hit group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stage.module = renderer->shader_modules.at(0);
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    /*
            Create the ray tracing pipeline
    */
    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
    rayTracingPipelineCI.pGroups = shaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
    rayTracingPipelineCI.layout = pipelineLayout;
    VkPipeline pipeline;
    vkCreateRayTracingPipelinesKHR(renderer->dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline);

    renderer->raytracing_pipeline = pipeline;
    renderer->raytracing_layout = pipelineLayout;
    renderer->shaderGroups = shaderGroups;
    renderer->raytracing_set_layout = descriptorSetLayout;
}

template <typename T> static T align_up(T a, T b) { return (a + b - 1) & -b; }

static void build_shader_binding_tables() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    const uint32_t handleSize = renderer->rt_props.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = align_up(renderer->rt_props.shaderGroupHandleSize, renderer->rt_props.shaderGroupHandleAlignment);
    const uint32_t groupCount = static_cast<uint32_t>(renderer->shaderGroups.size());
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    vkGetRayTracingShaderGroupHandlesKHR(renderer->dev, renderer->raytracing_pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

    const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    Buffer raygenShaderBindingTable = Buffer{ "buffer_sbt", handleSize * 4, bufferUsageFlags, true };

    // Copy handles
    memcpy(raygenShaderBindingTable.mapped, shaderHandleStorage.data(), handleSize * 4);

    renderer->sbt = Buffer{ raygenShaderBindingTable };
}

static void build_descriptor_sets() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());

    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 } };
    vks::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
    descriptorPoolCreateInfo.poolSizeCount = 3;
    descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
    descriptorPoolCreateInfo.maxSets = 2;
    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(renderer->dev, &descriptorPoolCreateInfo, nullptr, &descriptorPool);

    VkDescriptorSetLayout descriptorSetLayout;
    vks::DescriptorSetAllocateInfo descriptorSetAllocateInfo;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &renderer->raytracing_set_layout;
    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(renderer->dev, &descriptorSetAllocateInfo, &descriptorSet);

    vks::WriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &renderer->tlas;

    vks::WriteDescriptorSet accelerationStructureWrite{};
    // The specialized acceleration structure descriptor has to be chained
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
    accelerationStructureWrite.dstSet = descriptorSet;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageDescriptor{};
    storageImageDescriptor.imageView = renderer->rt_image.view;
    storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo ubo_descriptor{};
    ubo_descriptor.buffer = renderer->ubo.buffer;
    ubo_descriptor.offset = 0;
    ubo_descriptor.range = 128;

    vks::WriteDescriptorSet resultImageWrite;
    resultImageWrite.dstSet = descriptorSet;
    resultImageWrite.dstBinding = 1;
    resultImageWrite.dstArrayElement = 0;
    resultImageWrite.descriptorCount = 1;
    resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultImageWrite.pImageInfo = &storageImageDescriptor;

    vks::WriteDescriptorSet uniformBufferWrite;
    uniformBufferWrite.dstSet = descriptorSet;
    uniformBufferWrite.dstBinding = 2;
    uniformBufferWrite.dstArrayElement = 0;
    uniformBufferWrite.descriptorCount = 1;
    uniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferWrite.pBufferInfo = &ubo_descriptor;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = { accelerationStructureWrite, resultImageWrite, uniformBufferWrite };
    vkUpdateDescriptorSets(renderer->dev, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

    renderer->raytracing_set = descriptorSet;
}

static void create_swapchain() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::SwapchainCreateInfoKHR sinfo;
    sinfo.surface = window.surface;
    sinfo.minImageCount = 2;
    sinfo.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    sinfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sinfo.imageExtent = VkExtent2D{ window.width, window.height };
    sinfo.imageArrayLayers = 1;
    sinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sinfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sinfo.clipped = true;

    VkSwapchainKHR swapchain;
    if(const auto error = vkCreateSwapchainKHR(renderer->dev, &sinfo, nullptr, &swapchain); error != VK_SUCCESS) {
        throw std::runtime_error{ "Could not create swapchain" };
    }

    uint32_t num_images;
    vkGetSwapchainImagesKHR(renderer->dev, swapchain, &num_images, nullptr);

    std::vector<VkImage> images(num_images);
    vkGetSwapchainImagesKHR(renderer->dev, swapchain, &num_images, images.data());

    renderer->swapchain = swapchain;
    renderer->swapchain_images = images;
    renderer->swapchain_format = sinfo.imageFormat;
}

static void create_rt_output_image() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    renderer->rt_image = Image{
        "rt_image", window.width, window.height, 1, 1, 1, renderer->swapchain_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    };
    renderer->rt_image.transition_layout(VK_IMAGE_LAYOUT_GENERAL, true);
}

static void init_vulkan() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    if(volkInitialize() != VK_SUCCESS) { throw std::runtime_error{ "Volk loader not found. Stopping" }; }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
                        .enable_validation_layers()
                        .use_default_debug_messenger()
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        .require_api_version(VK_MAKE_API_VERSION(0, 1, 3, 0))
                        .build();

    if(!inst_ret) { throw std::runtime_error{ "Failed to create Vulkan instance. Error: " }; }

    vkb::Instance vkb_inst = inst_ret.value();
    volkLoadInstance(vkb_inst.instance);

    window.width = 1280;
    window.height = 768;
    window.window = glfwCreateWindow(window.width, window.height, "title", 0, 0);
    vks::Win32SurfaceCreateInfoKHR surface_info;
    surface_info.hwnd = glfwGetWin32Window(window.window);
    surface_info.hinstance = GetModuleHandle(nullptr);
    vkCreateWin32SurfaceKHR(vkb_inst.instance, &surface_info, nullptr, &window.surface);

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.require_present()
                        .set_surface(window.surface)
                        .set_minimum_version(1, 3)
                        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
                        .select();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    vkb::DeviceBuilder device_builder{ phys_ret.value() };

    vks::PhysicalDeviceSynchronization2Features synch2_features;
    synch2_features.synchronization2 = true;

    vks::PhysicalDeviceHostQueryResetFeatures query_reset_features;
    query_reset_features.hostQueryReset = true;

    vks::PhysicalDeviceDynamicRenderingFeatures dyn_features;
    dyn_features.dynamicRendering = true;

    vks::PhysicalDeviceDescriptorIndexingFeatures desc_features;
    desc_features.descriptorBindingVariableDescriptorCount = true;
    desc_features.descriptorBindingPartiallyBound = true;
    desc_features.shaderSampledImageArrayNonUniformIndexing = true;
    desc_features.shaderStorageImageArrayNonUniformIndexing = true;
    desc_features.descriptorBindingSampledImageUpdateAfterBind = true;
    desc_features.descriptorBindingStorageImageUpdateAfterBind = true;
    desc_features.descriptorBindingUniformBufferUpdateAfterBind = true;
    desc_features.descriptorBindingStorageBufferUpdateAfterBind = true;
    desc_features.runtimeDescriptorArray = true;

    vks::PhysicalDeviceFeatures2 dev_2_features;
    dev_2_features.features = {
        .geometryShader = true,
        .multiDrawIndirect = true,
        .fragmentStoresAndAtomics = true,
    };

    vks::PhysicalDeviceBufferDeviceAddressFeatures bda_features;
    bda_features.bufferDeviceAddress = true;

    vks::PhysicalDeviceAccelerationStructureFeaturesKHR acc_features;
    acc_features.accelerationStructure = true;

    vks::PhysicalDeviceRayTracingPipelineFeaturesKHR rtpp_features;
    rtpp_features.rayTracingPipeline = true;
    rtpp_features.rayTraversalPrimitiveCulling = true;

    auto dev_ret = device_builder.add_pNext(&dev_2_features)
                       .add_pNext(&desc_features)
                       .add_pNext(&dyn_features)
                       .add_pNext(&query_reset_features)
                       .add_pNext(&synch2_features)
                       .add_pNext(&bda_features)
                       .add_pNext(&acc_features)
                       .add_pNext(&rtpp_features)
                       .build();
    if(!dev_ret) { throw std::runtime_error{ "Failed to create Vulkan device. Error: " }; }
    vkb::Device vkb_device = dev_ret.value();
    volkLoadDevice(vkb_device.device);

    VkDevice device = vkb_device.device;

    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if(!graphics_queue_ret) { throw std::runtime_error{ "Failed to get graphics queue. Error: " }; }

    VkQueue graphics_queue = graphics_queue_ret.value();

    VkPhysicalDeviceProperties2 pdev_props{};
    pdev_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR pdev_rtpp_props{};
    pdev_rtpp_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    pdev_props.pNext = &pdev_rtpp_props;
    vkGetPhysicalDeviceProperties2(phys_ret->physical_device, &pdev_props);

    renderer->instance = vkb_inst.instance;
    renderer->dev = device;
    renderer->pdev = phys_ret->physical_device;
    renderer->gqi = vkb_device.get_queue_index(vkb::QueueType::graphics).value();
    renderer->gq = vkb_device.get_queue(vkb::QueueType::graphics).value();
    renderer->pqi = vkb_device.get_queue_index(vkb::QueueType::present).value();
    renderer->pq = vkb_device.get_queue(vkb::QueueType::present).value();
    renderer->rt_props = pdev_rtpp_props;

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = inst_ret->fp_vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = inst_ret->fp_vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
    vulkanFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice = renderer->pdev;
    allocatorCreateInfo.device = renderer->dev;
    allocatorCreateInfo.instance = renderer->instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

    VmaAllocator allocator;
    if(vmaCreateAllocator(&allocatorCreateInfo, &allocator) != VK_SUCCESS) { throw std::runtime_error{ "Could not create vma" }; }

    renderer->vma = allocator;

    vks::CommandPoolCreateInfo cmdpi;
    cmdpi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdpi.queueFamilyIndex = renderer->gqi;

    VkCommandPool pool;
    if(vkCreateCommandPool(device, &cmdpi, nullptr, &pool) != VK_SUCCESS) { throw std::runtime_error{ "Could not create command pool" }; }

    vks::CommandBufferAllocateInfo cmdi;
    cmdi.commandPool = pool;
    cmdi.commandBufferCount = 1;
    cmdi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer cmd;
    if(vkAllocateCommandBuffers(device, &cmdi, &cmd) != VK_SUCCESS) { throw std::runtime_error{ "Could not allocate command buffer" }; }

    renderer->cmdpool = pool;
    renderer->cmd = cmd;
    renderer->ubo = Buffer{ "ubo", 128, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true };

    vks::SemaphoreCreateInfo sem_swapchain_info;
    vkCreateSemaphore(renderer->dev, &sem_swapchain_info, nullptr, &renderer->primitives.sem_swapchain_image);
    vkCreateSemaphore(renderer->dev, &sem_swapchain_info, nullptr, &renderer->primitives.sem_tracing_done);
}

static void render() {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::AcquireNextImageInfoKHR acq_info;
    uint32_t sw_img_idx;
    vkAcquireNextImageKHR(renderer->dev, renderer->swapchain, -1ULL, renderer->primitives.sem_swapchain_image, nullptr, &sw_img_idx);

    vks::CommandBufferBeginInfo binfo;
    binfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(renderer->cmd, &binfo);

    vkCmdBindPipeline(renderer->cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, renderer->raytracing_pipeline);
    vkCmdBindDescriptorSets(renderer->cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, renderer->raytracing_layout, 0, 1, &renderer->raytracing_set, 0, nullptr);

    const uint32_t handle_size_aligned = align_up(renderer->rt_props.shaderGroupHandleSize, renderer->rt_props.shaderGroupHandleAlignment);

    vks::StridedDeviceAddressRegionKHR raygen_sbt;
    raygen_sbt.deviceAddress = renderer->sbt.bda;
    raygen_sbt.size = handle_size_aligned * 1;
    raygen_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR miss_sbt;
    miss_sbt.deviceAddress = renderer->sbt.bda;
    miss_sbt.size = handle_size_aligned * 2;
    miss_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR hit_sbt;
    hit_sbt.deviceAddress = renderer->sbt.bda;
    hit_sbt.size = handle_size_aligned * 1;
    hit_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR callable_sbt;

    VkClearColorValue clear_value{ 0.0f, 0.0f, 0.0f, 1.0f };
    VkImageSubresourceRange clear_range{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
    vkCmdTraceRaysKHR(renderer->cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, window.width, window.height, 1);

    vkEndCommandBuffer(renderer->cmd);

    vks::CommandBufferSubmitInfo cmd_info;
    cmd_info.commandBuffer = renderer->cmd;

    vks::SemaphoreSubmitInfo sem_info;
    sem_info.stageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    sem_info.semaphore = renderer->primitives.sem_tracing_done;

    vks::SubmitInfo2 sinfo;
    sinfo.commandBufferInfoCount = 1;
    sinfo.pCommandBufferInfos = &cmd_info;
    // sinfo.signalSemaphoreInfoCount = 1;
    // sinfo.pSignalSemaphoreInfos = &sem_info;
    vkQueueSubmit2(renderer->gq, 1, &sinfo, nullptr);
    vkDeviceWaitIdle(renderer->dev);

    vkBeginCommandBuffer(renderer->cmd, &binfo);
    vks::ImageMemoryBarrier2 sw_to_dst, sw_to_pres;
    sw_to_dst.image = renderer->swapchain_images.at(sw_img_idx);
    sw_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sw_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    sw_to_dst.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    sw_to_dst.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    sw_to_dst.srcAccessMask = VK_ACCESS_NONE;
    sw_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sw_to_dst.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
    sw_to_pres = sw_to_dst;
    sw_to_pres.oldLayout = sw_to_dst.newLayout;
    sw_to_pres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    sw_to_pres.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    sw_to_pres.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    sw_to_pres.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sw_to_pres.dstAccessMask = VK_ACCESS_2_NONE;

    vks::DependencyInfo sw_dep_info;
    sw_dep_info.imageMemoryBarrierCount = 1;
    sw_dep_info.pImageMemoryBarriers = &sw_to_dst;
    vkCmdPipelineBarrier2(renderer->cmd, &sw_dep_info);

    vks::ImageCopy img_copy;
    img_copy.srcOffset = {};
    img_copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    img_copy.dstOffset = {};
    img_copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    img_copy.extent = { window.width, window.height, 1 };
    vkCmdCopyImage(renderer->cmd, renderer->rt_image.image, VK_IMAGE_LAYOUT_GENERAL, sw_to_dst.image, sw_to_dst.newLayout, 1, &img_copy);

    sw_dep_info.pImageMemoryBarriers = &sw_to_pres;
    vkCmdPipelineBarrier2(renderer->cmd, &sw_dep_info);

    vkEndCommandBuffer(renderer->cmd);

    sinfo.commandBufferInfoCount = 1;
    sinfo.pCommandBufferInfos = &cmd_info;
    // sinfo.signalSemaphoreInfoCount = 1;
    // sinfo.pSignalSemaphoreInfos = &sem_info;
    vkQueueSubmit2(renderer->gq, 1, &sinfo, nullptr);
    vkDeviceWaitIdle(renderer->dev);

    vks::PresentInfoKHR pinfo;
    pinfo.swapchainCount = 1;
    pinfo.pSwapchains = &renderer->swapchain;
    pinfo.pImageIndices = &sw_img_idx;
    pinfo.waitSemaphoreCount = 1;
    pinfo.pWaitSemaphores = &renderer->primitives.sem_swapchain_image;
    vkQueuePresentKHR(renderer->gq, &pinfo);
    vkDeviceWaitIdle(renderer->dev);
}

int main() {
    try {
        Engine::init();
        init_vulkan();
        create_swapchain();
        create_rt_output_image();
        compile_shaders();
        build_rtpp();

        ModelLoader loader;
        Model model = loader.load_model("cornell/cornell.glb");
        build_blas(model);
        build_tlas(model);
        build_shader_binding_tables();
        build_descriptor_sets();

		RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
        const glm::mat4 projection = glm::perspectiveFov(glm::radians(90.0f), (float)window.width, (float)window.height, 0.01f, 1000.0f);
        const glm::mat4 view = camera.get_view();
        const glm::mat4 inv_projection = glm::inverse(projection);
        const glm::mat4 inv_view = glm::inverse(view);
        memcpy(renderer->ubo.mapped, &inv_view[0][0], sizeof(inv_view));
        memcpy(static_cast<std::byte*>(renderer->ubo.mapped) + sizeof(inv_view), &inv_projection[0][0], sizeof(inv_projection));

        while(!glfwWindowShouldClose(window.window)) {
            render();
            glfwPollEvents();
        }

        int x = 1;
    } catch(const std::exception& err) {
        std::cerr << err.what();
        return -1;
    }
}