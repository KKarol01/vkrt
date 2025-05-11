#include "pipeline.hpp"
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/descpool.hpp>
#include <shaderc/shaderc.hpp>
#include <stb/stb_include.h>

namespace gfx {
Shader* PipelineCompiler::get_shader(const std::filesystem::path path) {
    if(auto it = compiled_shaders.find(path); it != compiled_shaders.end()) { return it->second; }
    shaders.push_front(Shader{ .path = path });
    shaders_to_compile.push_back(&shaders.front());
    compiled_shaders[path] = &shaders.front();
    return &shaders.front();
}

Pipeline* PipelineCompiler::get_pipeline(const PipelineSettings& settings) {
    if(settings.shaders.empty()) { return nullptr; }
    for(auto& e : pipelines) {
        if(e.settings.shaders.size() == settings.shaders.size() &&
           std::equal(e.settings.shaders.begin(), e.settings.shaders.end(), settings.shaders.begin()) &&
           e.settings.settings == settings.settings) {
            return &e;
        }
    }
    pipelines.push_front(Pipeline{ .settings = settings });
    auto* p = &pipelines.front();
    for(auto& e : p->settings.shaders) {
        canonize_path(e);
        get_shader(e);
    }
    pipelines_to_compile.push_back(p);
    return p;
}

void PipelineCompiler::threaded_compile() {
    if(shaders_to_compile.empty()) { return; }

    const auto num_th = std::thread::hardware_concurrency();
    const auto sh_per_th = (uint32_t)std::ceilf((float)shaders_to_compile.size() / (float)num_th);
    const auto pp_per_th = (uint32_t)std::ceilf((float)pipelines_to_compile.size() / (float)num_th);

    std::vector<std::thread> workers(num_th);
    uint32_t running_threads = 0u;
    uint32_t items_in_flight = 0u;
    for(running_threads = 0u, items_in_flight = 0u; items_in_flight < shaders_to_compile.size();
        ++running_threads, items_in_flight += sh_per_th) {
        workers[running_threads] = std::thread{ [this, items_in_flight, sh_per_th]() {
            for(auto i = items_in_flight; i < sh_per_th + items_in_flight && i < shaders_to_compile.size(); ++i) {
                compile_shader(shaders_to_compile.at(i));
            }
        } };
    }
    for(auto i = 0u; i < running_threads; ++i) {
        workers.at(i).join();
    }
    for(running_threads = 0u, items_in_flight = 0u; items_in_flight < pipelines_to_compile.size();
        ++running_threads, items_in_flight += pp_per_th) {
        workers[running_threads] = std::thread{ [this, items_in_flight, pp_per_th]() {
            for(auto i = items_in_flight; i < pp_per_th + items_in_flight; ++i) {
                compile_pipeline(pipelines_to_compile.at(i));
            }
        } };
    }
    for(auto i = 0u; i < running_threads; ++i) {
        workers.at(i).join();
    }
    shaders_to_compile.clear();
    pipelines_to_compile.clear();
}

void PipelineCompiler::compile_shader(Shader* shader) {
    const auto& path = shader->path;
    static const auto read_file = [](const std::filesystem::path& path) {
        std::string path_str = path.string();
        std::string path_to_includes = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders").string();
        char error[256] = {};
        char* parsed_file = stb_include_file(path_str.data(), nullptr, path_to_includes.data(), error);
        if(!parsed_file) {
            ENG_WARN("STBI_INCLUDE cannot parse file [{}]: {}", path_str, error);
            return std::string{};
        }
        std::string parsed_file_str{ parsed_file };
        free(parsed_file);
        return parsed_file_str;
    };

    const auto stage = get_shader_stage(path);
    shader->stage = stage;

    const auto kind = [stage] {
        if(stage == VK_SHADER_STAGE_VERTEX_BIT) { return shaderc_vertex_shader; }
        if(stage == VK_SHADER_STAGE_FRAGMENT_BIT) { return shaderc_fragment_shader; }
        if(stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { return shaderc_raygen_shader; }
        if(stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) { return shaderc_closesthit_shader; }
        if(stage == VK_SHADER_STAGE_MISS_BIT_KHR) { return shaderc_miss_shader; }
        if(stage == VK_SHADER_STAGE_COMPUTE_BIT) { return shaderc_compute_shader; }
        return static_cast<decltype(shaderc_vertex_shader)>(~(std::underlying_type_t<decltype(shaderc_vertex_shader)>{}));
    }();

    if(~std::underlying_type_t<decltype(shaderc_vertex_shader)>(kind) == 0) { return; }

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetGenerateDebugInfo();
    // options.AddMacroDefinition("ASDF");
    shaderc::Compiler c;
    std::string file_str = read_file(path);
    const auto res = c.CompileGlslToSpv(file_str, kind, path.filename().string().c_str(), options);
    if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
        ENG_WARN("Could not compile shader : {}, because : \"{}\"", path.string(), res.GetErrorMessage());
        return;
    }

    const auto module_info = Vks(VkShaderModuleCreateInfo{
        .codeSize = (res.end() - res.begin()) * sizeof(uint32_t),
        .pCode = res.begin(),
    });
    VK_CHECK(vkCreateShaderModule(RendererVulkan::get_instance()->dev, &module_info, nullptr, &shader->shader));
}

void PipelineCompiler::compile_pipeline(Pipeline* pipeline) {
    if(pipeline->settings.shaders.empty()) { return; }

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for(const auto& p : pipeline->settings.shaders) {
        const auto stage =
            Vks(VkPipelineShaderStageCreateInfo{ .stage = get_shader(p)->stage, .module = get_shader(p)->shader, .pName = "main" });
        stages.push_back(stage);
    }

    if(std::holds_alternative<RasterizationSettings>(pipeline->settings.settings)) {
        auto& rasterization_settings = std::get<RasterizationSettings>(pipeline->settings.settings);

        auto pVertexInputState = Vks(VkPipelineVertexInputStateCreateInfo{});

        auto pInputAssemblyState = Vks(VkPipelineInputAssemblyStateCreateInfo{
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = false,
        });

        auto pTessellationState = Vks(VkPipelineTessellationStateCreateInfo{});

        auto pViewportState = Vks(VkPipelineViewportStateCreateInfo{});

        auto pRasterizationState = Vks(VkPipelineRasterizationStateCreateInfo{
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = rasterization_settings.culling,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        });

        auto pMultisampleState = Vks(VkPipelineMultisampleStateCreateInfo{
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        });

        auto pDepthStencilState = Vks(VkPipelineDepthStencilStateCreateInfo{
            .depthTestEnable = rasterization_settings.depth_test,
            .depthWriteEnable = rasterization_settings.depth_write,
            .depthCompareOp = rasterization_settings.depth_op,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = {},
            .back = {},
        });

        std::array<VkPipelineColorBlendAttachmentState, 4> blends;
        for(uint32_t i = 0; i < rasterization_settings.num_col_formats; ++i) {
            blends[i] = { .colorWriteMask = 0b1111 /*RGBA*/ };
        }
        auto pColorBlendState = Vks(VkPipelineColorBlendStateCreateInfo{
            .attachmentCount = rasterization_settings.num_col_formats,
            .pAttachments = blends.data(),
        });

        VkDynamicState dynstates[]{
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        };
        auto pDynamicState = Vks(VkPipelineDynamicStateCreateInfo{
            .dynamicStateCount = sizeof(dynstates) / sizeof(dynstates[0]),
            .pDynamicStates = dynstates,
        });

        auto pDynamicRendering = Vks(VkPipelineRenderingCreateInfo{
            .colorAttachmentCount = rasterization_settings.num_col_formats,
            .pColorAttachmentFormats = rasterization_settings.col_formats.data(),
            .depthAttachmentFormat = rasterization_settings.dep_format,
        });
        // pDynamicRendering.stencilAttachmentFormat = rasterization_settings.st_format;

        auto vk_info = Vks(VkGraphicsPipelineCreateInfo{
            .pNext = &pDynamicRendering,
            .stageCount = (uint32_t)stages.size(),
            .pStages = stages.data(),
            .pVertexInputState = &pVertexInputState,
            .pInputAssemblyState = &pInputAssemblyState,
            .pTessellationState = &pTessellationState,
            .pViewportState = &pViewportState,
            .pRasterizationState = &pRasterizationState,
            .pMultisampleState = &pMultisampleState,
            .pDepthStencilState = &pDepthStencilState,
            .pColorBlendState = &pColorBlendState,
            .pDynamicState = &pDynamicState,
            .layout = RendererVulkan::get_instance()->bindless_pool->get_pipeline_layout(),
        });
        VK_CHECK(vkCreateGraphicsPipelines(RendererVulkan::get_instance()->dev, nullptr, 1, &vk_info, nullptr, &pipeline->pipeline));
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    } else if(std::holds_alternative<std::monostate>(pipeline->settings.settings)) {
        assert(stages.size() == 1);
        auto vk_info = Vks(VkComputePipelineCreateInfo{
            .stage = stages.at(0),
            .layout = RendererVulkan::get_instance()->bindless_pool->get_pipeline_layout(),
        });
        VK_CHECK(vkCreateComputePipelines(RendererVulkan::get_instance()->dev, nullptr, 1, &vk_info, nullptr, &pipeline->pipeline));
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    } else {
        assert(false);
    }
}

VkShaderStageFlagBits PipelineCompiler::get_shader_stage(std::filesystem::path path) const {
    if(path.extension() == ".glsl") { path.replace_extension(); }
    const auto ext = path.extension();
    if(ext == ".vert") { return VK_SHADER_STAGE_VERTEX_BIT; }
    if(ext == ".frag") { return VK_SHADER_STAGE_FRAGMENT_BIT; }
    if(ext == ".rgen") { return VK_SHADER_STAGE_RAYGEN_BIT_KHR; }
    if(ext == ".rchit") { return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; }
    if(ext == ".rmiss") { return VK_SHADER_STAGE_MISS_BIT_KHR; }
    if(ext == ".comp") { return VK_SHADER_STAGE_COMPUTE_BIT; }
    if(path.extension() != ".inc") { ENG_WARN("Unrecognized shader extension {}", path.string()); }
    return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
}

void PipelineCompiler::canonize_path(std::filesystem::path& p) {
    static const auto prefix = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders");
    if(!p.string().starts_with(prefix.string())) { p = prefix / p; }
    p.make_preferred();
}

} // namespace gfx