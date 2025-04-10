#pragma once

#include <eng/renderer/common.hpp>
#include <vulkan/vulkan.h>

namespace rendergraph2 {

class RenderGraph;
struct Resource;

enum class ResourceFlags : uint32_t { FROM_UNDEFINED_LAYOUT_BIT = 0x1 };
ENG_ENABLE_FLAGS_OPERATORS(ResourceFlags);

enum class AccessFlags : uint32_t {
    NONE_BIT = 0x0,
    READ_BIT = 0x1,
    WRITE_BIT = 0x2,
    READ_WRITE_BIT = 0x3,
};
ENG_ENABLE_FLAGS_OPERATORS(AccessFlags)

struct Access {
    Handle<Resource> resource{};
    Flags<ResourceFlags> flags{};
    Flags<AccessFlags> type{};
    VkAccessFlags2 access{};
    VkPipelineStageFlags2 stage{};
    VkImageLayout dst_layout{};
};

struct RasterizationPipelineSettings {};

using pipeline_settings_t = std::variant<RasterizationPipelineSettings>;

class RenderPass {
  public:
    explicit RenderPass(const std::string& name, const std::vector<std::filesystem::path>& shaders,
                        const pipeline_settings_t& pipeline_settings = {});
    virtual ~RenderPass() noexcept = default;
    virtual void render(VkCommandBuffer cmd) = 0;
    std::span<const Access> get_accesses() const { return accesses; }

    std::string name;
    std::vector<Access> accesses;
};

class VsmClearPagesPass final : public RenderPass {
  public:
    VsmClearPagesPass(RenderGraph* rg);
    ~VsmClearPagesPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    VkImageView depth_buffer{};
};

} // namespace rendergraph2