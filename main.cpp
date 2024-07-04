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
#include "model_importer.hpp"

struct Camera {
    glm::mat4 get_view() const { return glm::lookAt(position, position + direction, glm::vec3{ 0.0f, 1.0f, 0.0f }); }

    glm::vec3 position{ 0.0f, 0.0f, 2.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

static Camera camera;

#include "engine.hpp"
#include "renderer_vulkan.hpp"

// static void build_blas(Model& model) {
// }
//
// static void build_tlas(Model& model) { }
//
// static void compile_shaders() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     shaderc::Compiler c;
//
//     static const auto read_file = [](const std::filesystem::path& path) {
//         std::ifstream file{ path };
//         std::stringstream buffer;
//         buffer << file.rdbuf();
//         return buffer.str();
//     };
//
//     std::filesystem::path files[]{
//         std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "closest_hit.glsl",
//         std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss.glsl",
//         std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss2.glsl",
//         std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "raygen.glsl",
//     };
//     shaderc_shader_kind kinds[]{ shaderc_closesthit_shader, shaderc_miss_shader, shaderc_miss_shader, shaderc_raygen_shader };
//     std::vector<std::vector<uint32_t>> compiled_modules;
//     std::vector<VkShaderModule> modules;
//
//     shaderc::CompileOptions options;
//     options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
//     options.SetTargetSpirv(shaderc_spirv_version_1_6);
//     for(int i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
//         auto res = c.CompileGlslToSpv(read_file(files[i]), kinds[i], files[i].filename().string().c_str(), options);
//
//         if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
//             throw std::runtime_error{ std::format("Could not compile shader: {}, because: \"{}\"", files[i].string(), res.GetErrorMessage()) };
//         }
//
//         compiled_modules.emplace_back(res.begin(), res.end());
//
//         vks::ShaderModuleCreateInfo module_info;
//         module_info.codeSize = compiled_modules.back().size() * sizeof(compiled_modules.back()[0]);
//         module_info.pCode = compiled_modules.back().data();
//         vkCreateShaderModule(renderer->dev, &module_info, nullptr, &modules.emplace_back());
//     }
//
//     renderer->shader_modules = modules;
// }
//
// static void build_rtpp() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
//     accelerationStructureLayoutBinding.binding = 0;
//     accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
//     accelerationStructureLayoutBinding.descriptorCount = 1;
//     accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
//
//     VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
//     resultImageLayoutBinding.binding = 1;
//     resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
//     resultImageLayoutBinding.descriptorCount = 1;
//     resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
//
//     VkDescriptorSetLayoutBinding uniformBufferBinding{};
//     uniformBufferBinding.binding = 2;
//     uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//     uniformBufferBinding.descriptorCount = 1;
//     uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
//
//     std::vector<VkDescriptorSetLayoutBinding> bindings({ accelerationStructureLayoutBinding, resultImageLayoutBinding, uniformBufferBinding });
//
//     VkDescriptorSetLayout descriptorSetLayout;
//     VkPipelineLayout pipelineLayout;
//
//     VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI{};
//     descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//     descriptorSetlayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
//     descriptorSetlayoutCI.pBindings = bindings.data();
//     vkCreateDescriptorSetLayout(renderer->dev, &descriptorSetlayoutCI, nullptr, &descriptorSetLayout);
//
//     VkPipelineLayoutCreateInfo pipelineLayoutCI{};
//     pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
//     pipelineLayoutCI.setLayoutCount = 1;
//     pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
//     vkCreatePipelineLayout(renderer->dev, &pipelineLayoutCI, nullptr, &pipelineLayout);
//
//     /*
//             Setup ray tracing shader groups
//     */
//     std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
//     std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
//
//     // Ray generation group
//     {
//         vks::PipelineShaderStageCreateInfo stage;
//         stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
//         stage.module = renderer->shader_modules.at(3);
//         stage.pName = "main";
//         shaderStages.push_back(stage);
//         VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
//         shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
//         shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
//         shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
//         shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
//         shaderGroups.push_back(shaderGroup);
//     }
//
//     // Miss group
//     {
//         vks::PipelineShaderStageCreateInfo stage;
//         stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
//         stage.module = renderer->shader_modules.at(1);
//         stage.pName = "main";
//         shaderStages.push_back(stage);
//         VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
//         shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
//         shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
//         shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
//         shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
//         shaderGroups.push_back(shaderGroup);
//     }
//
//     // Miss group
//     {
//         vks::PipelineShaderStageCreateInfo stage;
//         stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
//         stage.module = renderer->shader_modules.at(2);
//         stage.pName = "main";
//         shaderStages.push_back(stage);
//         VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
//         shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
//         shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
//         shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
//         shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
//         shaderGroups.push_back(shaderGroup);
//     }
//
//     // Closest hit group
//     {
//         vks::PipelineShaderStageCreateInfo stage;
//         stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
//         stage.module = renderer->shader_modules.at(0);
//         stage.pName = "main";
//         shaderStages.push_back(stage);
//         VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
//         shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
//         shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
//         shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
//         shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
//         shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
//         shaderGroups.push_back(shaderGroup);
//     }
//
//     /*
//             Create the ray tracing pipeline
//     */
//     VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
//     rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
//     rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
//     rayTracingPipelineCI.pStages = shaderStages.data();
//     rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
//     rayTracingPipelineCI.pGroups = shaderGroups.data();
//     rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
//     rayTracingPipelineCI.layout = pipelineLayout;
//     VkPipeline pipeline;
//     vkCreateRayTracingPipelinesKHR(renderer->dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline);
//
//     renderer->raytracing_pipeline = pipeline;
//     renderer->raytracing_layout = pipelineLayout;
//     renderer->shaderGroups = shaderGroups;
//     renderer->raytracing_set_layout = descriptorSetLayout;
// }
//
// template <typename T> static T align_up(T a, T b) { return (a + b - 1) & -b; }
//
// static void build_shader_binding_tables() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     const uint32_t handleSize = renderer->rt_props.shaderGroupHandleSize;
//     const uint32_t handleSizeAligned = align_up(renderer->rt_props.shaderGroupHandleSize, renderer->rt_props.shaderGroupHandleAlignment);
//     const uint32_t groupCount = static_cast<uint32_t>(renderer->shaderGroups.size());
//     const uint32_t sbtSize = groupCount * handleSizeAligned;
//
//     std::vector<uint8_t> shaderHandleStorage(sbtSize);
//     vkGetRayTracingShaderGroupHandlesKHR(renderer->dev, renderer->raytracing_pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());
//
//     const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
//     Buffer raygenShaderBindingTable = Buffer{ "buffer_sbt", handleSize * 4, bufferUsageFlags, true };
//
//     // Copy handles
//     memcpy(raygenShaderBindingTable.mapped, shaderHandleStorage.data(), handleSize * 4);
//
//     renderer->sbt = Buffer{ raygenShaderBindingTable };
// }
//
// static void build_descriptor_sets() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//
//     std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2 },
//                                                     { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
//                                                     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 } };
//     vks::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
//     descriptorPoolCreateInfo.poolSizeCount = 3;
//     descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
//     descriptorPoolCreateInfo.maxSets = 2;
//     VkDescriptorPool descriptorPool;
//     vkCreateDescriptorPool(renderer->dev, &descriptorPoolCreateInfo, nullptr, &descriptorPool);
//
//     VkDescriptorSetLayout descriptorSetLayout;
//     vks::DescriptorSetAllocateInfo descriptorSetAllocateInfo;
//     descriptorSetAllocateInfo.descriptorPool = descriptorPool;
//     descriptorSetAllocateInfo.descriptorSetCount = 1;
//     descriptorSetAllocateInfo.pSetLayouts = &renderer->raytracing_set_layout;
//     VkDescriptorSet descriptorSet;
//     vkAllocateDescriptorSets(renderer->dev, &descriptorSetAllocateInfo, &descriptorSet);
//
//     vks::WriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
//     descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
//     descriptorAccelerationStructureInfo.pAccelerationStructures = &renderer->tlas;
//
//     vks::WriteDescriptorSet accelerationStructureWrite{};
//     // The specialized acceleration structure descriptor has to be chained
//     accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
//     accelerationStructureWrite.dstSet = descriptorSet;
//     accelerationStructureWrite.dstBinding = 0;
//     accelerationStructureWrite.descriptorCount = 1;
//     accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
//
//     VkDescriptorImageInfo storageImageDescriptor{};
//     storageImageDescriptor.imageView = renderer->rt_image.view;
//     storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
//
//     VkDescriptorBufferInfo ubo_descriptor{};
//     ubo_descriptor.buffer = renderer->ubo.buffer;
//     ubo_descriptor.offset = 0;
//     ubo_descriptor.range = 128;
//
//     vks::WriteDescriptorSet resultImageWrite;
//     resultImageWrite.dstSet = descriptorSet;
//     resultImageWrite.dstBinding = 1;
//     resultImageWrite.dstArrayElement = 0;
//     resultImageWrite.descriptorCount = 1;
//     resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
//     resultImageWrite.pImageInfo = &storageImageDescriptor;
//
//     vks::WriteDescriptorSet uniformBufferWrite;
//     uniformBufferWrite.dstSet = descriptorSet;
//     uniformBufferWrite.dstBinding = 2;
//     uniformBufferWrite.dstArrayElement = 0;
//     uniformBufferWrite.descriptorCount = 1;
//     uniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//     uniformBufferWrite.pBufferInfo = &ubo_descriptor;
//
//     std::vector<VkWriteDescriptorSet> writeDescriptorSets = { accelerationStructureWrite, resultImageWrite, uniformBufferWrite };
//     vkUpdateDescriptorSets(renderer->dev, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
//
//     renderer->raytracing_set = descriptorSet;
// }
//
static void create_swapchain() {}
//
// static void create_rt_output_image() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     renderer->rt_image = Image{
//         "rt_image", window.width, window.height, 1, 1, 1, renderer->swapchain_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
//     };
//     renderer->rt_image.transition_layout(VK_IMAGE_LAYOUT_GENERAL, true);
// }
//
//
// static void render() {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     vks::AcquireNextImageInfoKHR acq_info;
//     uint32_t sw_img_idx;
//     vkAcquireNextImageKHR(renderer->dev, renderer->swapchain, -1ULL, renderer->primitives.sem_swapchain_image, nullptr, &sw_img_idx);
//
//     vks::CommandBufferBeginInfo binfo;
//     binfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//     vkBeginCommandBuffer(renderer->cmd, &binfo);
//
//     vkCmdBindPipeline(renderer->cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, renderer->raytracing_pipeline);
//     vkCmdBindDescriptorSets(renderer->cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, renderer->raytracing_layout, 0, 1,
//                             &renderer->raytracing_set, 0, nullptr);
//
//     const uint32_t handle_size_aligned = align_up(renderer->rt_props.shaderGroupHandleSize, renderer->rt_props.shaderGroupHandleAlignment);
//
//     vks::StridedDeviceAddressRegionKHR raygen_sbt;
//     raygen_sbt.deviceAddress = renderer->sbt.bda;
//     raygen_sbt.size = handle_size_aligned * 1;
//     raygen_sbt.stride = handle_size_aligned;
//
//     vks::StridedDeviceAddressRegionKHR miss_sbt;
//     miss_sbt.deviceAddress = renderer->sbt.bda;
//     miss_sbt.size = handle_size_aligned * 2;
//     miss_sbt.stride = handle_size_aligned;
//
//     vks::StridedDeviceAddressRegionKHR hit_sbt;
//     hit_sbt.deviceAddress = renderer->sbt.bda;
//     hit_sbt.size = handle_size_aligned * 1;
//     hit_sbt.stride = handle_size_aligned;
//
//     vks::StridedDeviceAddressRegionKHR callable_sbt;
//
//     VkClearColorValue clear_value{ 0.0f, 0.0f, 0.0f, 1.0f };
//     VkImageSubresourceRange clear_range{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
//     vkCmdTraceRaysKHR(renderer->cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, window.width, window.height, 1);
//
//     vkEndCommandBuffer(renderer->cmd);
//
//     vks::CommandBufferSubmitInfo cmd_info;
//     cmd_info.commandBuffer = renderer->cmd;
//
//     vks::SemaphoreSubmitInfo sem_info;
//     sem_info.stageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
//     sem_info.semaphore = renderer->primitives.sem_tracing_done;
//
//     vks::SubmitInfo2 sinfo;
//     sinfo.commandBufferInfoCount = 1;
//     sinfo.pCommandBufferInfos = &cmd_info;
//     // sinfo.signalSemaphoreInfoCount = 1;
//     // sinfo.pSignalSemaphoreInfos = &sem_info;
//     vkQueueSubmit2(renderer->gq, 1, &sinfo, nullptr);
//     vkDeviceWaitIdle(renderer->dev);
//
//     vkBeginCommandBuffer(renderer->cmd, &binfo);
//     vks::ImageMemoryBarrier2 sw_to_dst, sw_to_pres;
//     sw_to_dst.image = renderer->swapchain_images.at(sw_img_idx);
//     sw_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//     sw_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//     sw_to_dst.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
//     sw_to_dst.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
//     sw_to_dst.srcAccessMask = VK_ACCESS_NONE;
//     sw_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//     sw_to_dst.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
//     sw_to_pres = sw_to_dst;
//     sw_to_pres.oldLayout = sw_to_dst.newLayout;
//     sw_to_pres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//     sw_to_pres.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
//     sw_to_pres.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
//     sw_to_pres.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//     sw_to_pres.dstAccessMask = VK_ACCESS_2_NONE;
//
//     vks::DependencyInfo sw_dep_info;
//     sw_dep_info.imageMemoryBarrierCount = 1;
//     sw_dep_info.pImageMemoryBarriers = &sw_to_dst;
//     vkCmdPipelineBarrier2(renderer->cmd, &sw_dep_info);
//
//     vks::ImageCopy img_copy;
//     img_copy.srcOffset = {};
//     img_copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
//     img_copy.dstOffset = {};
//     img_copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
//     img_copy.extent = { window.width, window.height, 1 };
//     vkCmdCopyImage(renderer->cmd, renderer->rt_image.image, VK_IMAGE_LAYOUT_GENERAL, sw_to_dst.image, sw_to_dst.newLayout, 1, &img_copy);
//
//     sw_dep_info.pImageMemoryBarriers = &sw_to_pres;
//     vkCmdPipelineBarrier2(renderer->cmd, &sw_dep_info);
//
//     vkEndCommandBuffer(renderer->cmd);
//
//     sinfo.commandBufferInfoCount = 1;
//     sinfo.pCommandBufferInfos = &cmd_info;
//     // sinfo.signalSemaphoreInfoCount = 1;
//     // sinfo.pSignalSemaphoreInfos = &sem_info;
//     vkQueueSubmit2(renderer->gq, 1, &sinfo, nullptr);
//     vkDeviceWaitIdle(renderer->dev);
//
//     vks::PresentInfoKHR pinfo;
//     pinfo.swapchainCount = 1;
//     pinfo.pSwapchains = &renderer->swapchain;
//     pinfo.pImageIndices = &sw_img_idx;
//     pinfo.waitSemaphoreCount = 1;
//     pinfo.pWaitSemaphores = &renderer->primitives.sem_swapchain_image;
//     vkQueuePresentKHR(renderer->gq, &pinfo);
//     vkDeviceWaitIdle(renderer->dev);
// }

struct A {};
struct B {};

int main() {
    try {
        Engine::init();
        // create_swapchain();
        // create_rt_output_image();
        // compile_shaders();
        // build_rtpp();

        ImportedModel model = ModelImporter::import_model("cornell_box", "cornell/cornell.glb");

        Engine::renderer()->batch_model(model, { .flags = BatchFlags::RAY_TRACED_BIT });

        int x = 1;
        // auto handle = Engine::renderer()->build_blas();

        // build_blas(model);
        // build_tlas(model);
        // build_shader_binding_tables();
        // build_descriptor_sets();

        // RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
        // const glm::mat4 projection = glm::perspectiveFov(glm::radians(90.0f), (float)window.width, (float)window.height, 0.01f, 1000.0f);
        // const glm::mat4 view = camera.get_view();
        // const glm::mat4 inv_projection = glm::inverse(projection);
        // const glm::mat4 inv_view = glm::inverse(view);
        // memcpy(renderer->ubo.mapped, &inv_view[0][0], sizeof(inv_view));
        // memcpy(static_cast<std::byte*>(renderer->ubo.mapped) + sizeof(inv_view), &inv_projection[0][0], sizeof(inv_projection));

        while(!glfwWindowShouldClose(Engine::window()->window)) {
            glfwPollEvents();
        }

    } catch(const std::exception& err) {
        std::cerr << err.what();
        return -1;
    }
}