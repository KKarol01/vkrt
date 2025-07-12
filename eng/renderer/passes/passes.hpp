#pragma once

#include <vulkan/vulkan.h>
#include <eng/renderer/pipeline.hpp>
#include <eng/common/flags.hpp>

namespace gfx
{

class RenderGraph;
struct Resource;

enum class AccessFlags : uint32_t
{
    NONE_BIT = 0x0,
    READ_BIT = 0x1,
    WRITE_BIT = 0x2,
    READ_WRITE_BIT = 0x3,
    FROM_UNDEFINED_LAYOUT_BIT = 0x4,
};
ENG_ENABLE_FLAGS_OPERATORS(AccessFlags)

struct Access
{
    Handle<Resource> resource{};
    Flags<AccessFlags> flags{};
    VkPipelineStageFlags2 stage{};
    VkAccessFlags2 access{};
    VkImageLayout layout{};
};

class RenderPass
{
  public:
    RenderPass(const std::string& name, const PipelineCreateInfo& settings = {});
    virtual ~RenderPass() noexcept = default;
    virtual void render(VkCommandBuffer cmd) = 0;

    std::string name;
    std::vector<Access> accesses;
    Pipeline* pipeline{};
};

class FFTOceanDebugGenH0Pass final : public RenderPass
{
  public:
    FFTOceanDebugGenH0Pass(RenderGraph* rg);
    ~FFTOceanDebugGenH0Pass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class FFTOceanDebugGenHtPass final : public RenderPass
{
  public:
    FFTOceanDebugGenHtPass(RenderGraph* rg);
    ~FFTOceanDebugGenHtPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class FFTOceanDebugGenFourierPass final : public RenderPass
{
  public:
    FFTOceanDebugGenFourierPass(RenderGraph* rg);
    ~FFTOceanDebugGenFourierPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class FFTOceanDebugGenDisplacementPass final : public RenderPass
{
  public:
    FFTOceanDebugGenDisplacementPass(RenderGraph* rg);
    ~FFTOceanDebugGenDisplacementPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class FFTOceanDebugGenGradientPass final : public RenderPass
{
  public:
    FFTOceanDebugGenGradientPass(RenderGraph* rg);
    ~FFTOceanDebugGenGradientPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class ZPrepassPass final : public RenderPass
{
  public:
    ZPrepassPass(RenderGraph* rg);
    ~ZPrepassPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class VsmClearPagesPass final : public RenderPass
{
  public:
    VsmClearPagesPass(RenderGraph* rg);
    ~VsmClearPagesPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class VsmPageAllocPass final : public RenderPass
{
  public:
    VsmPageAllocPass(RenderGraph* rg);
    ~VsmPageAllocPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class VsmShadowsPass final : public RenderPass
{
  public:
    VsmShadowsPass(RenderGraph* rg);
    ~VsmShadowsPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class VsmDebugPageCopyPass final : public RenderPass
{
  public:
    VsmDebugPageCopyPass(RenderGraph* rg);
    ~VsmDebugPageCopyPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class DefaultUnlitPass final : public RenderPass
{
  public:
    DefaultUnlitPass(RenderGraph* rg);
    ~DefaultUnlitPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class ImguiPass final : public RenderPass
{
  public:
    ImguiPass(RenderGraph* rg);
    ~ImguiPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class SwapchainPresentPass final : public RenderPass
{
  public:
    SwapchainPresentPass(RenderGraph* rg);
    ~SwapchainPresentPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

} // namespace gfx