#pragma once

#include <eng/renderer/passes/common.hpp>

namespace rendergraph {

class RenderGraph;

class VsmClearPagesPass final : public RenderPass {
  public:
    VsmClearPagesPass(RenderGraph* rg) noexcept;
    ~VsmClearPagesPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    VkImageView vsm_dir_light_page_table{};
};

class VsmPageAllocPass final : public RenderPass {
  public:
    VsmPageAllocPass(RenderGraph* rg) noexcept;
    ~VsmPageAllocPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    SwappableResource<VkImageView> depth_buffer;
    VkImageView vsm_dir_light_page_table{};
};

class ZPrepassPass final : public RenderPass {
  public:
    ZPrepassPass(RenderGraph* rg) noexcept;
    ~ZPrepassPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class VsmShadowPass final : public RenderPass {
  public:
    VsmShadowPass(RenderGraph* rg) noexcept;
    ~VsmShadowPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    SwappableResource<VkImageView> depth_buffer;
    VkImageView vsm_dir_light_page_table{};
    VkImageView vsm_shadow_map_0{};
};

class DefaultUnlitPass final : public RenderPass {
  public:
    DefaultUnlitPass(RenderGraph* rg) noexcept;
    ~DefaultUnlitPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    VkImageView vsm_dir_light_page_table{};
    VkImageView vsm_shadow_map_0{};
};

class VsmDebugPageAllocCopy final : public RenderPass {
  public:
    VsmDebugPageAllocCopy(RenderGraph* rg) noexcept;
    ~VsmDebugPageAllocCopy() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
    VkImageView vsm_dir_light_page_table{};
    VkImageView vsm_dir_light_page_table_rgba8{};
};

class ImguiPass final : public RenderPass {
  public:
    ImguiPass(RenderGraph* rg) noexcept;
    ~ImguiPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

class SwapchainPresentPass final : public RenderPass {
  public:
    SwapchainPresentPass(RenderGraph* rg) noexcept;
    ~SwapchainPresentPass() noexcept final = default;
    void render(VkCommandBuffer cmd) final;
};

} // namespace rendergraph
