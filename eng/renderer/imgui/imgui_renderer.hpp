#pragma once

#include <vector>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/renderer/renderer_fwd.hpp>

struct ImTextureData;

namespace eng
{
namespace gfx
{

class ImGuiRenderer
{
  public:
    void init();
    void update(RenderGraph* graph, Handle<RenderGraph::ResourceAccess> output);

  private:
    void handle_imtexture(ImTextureData* imtex);

  public:
    Signal<void()> ui_callbacks;

  private:
    Handle<Pipeline> pipeline;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
    uint32_t output;
};

} // namespace gfx
} // namespace eng