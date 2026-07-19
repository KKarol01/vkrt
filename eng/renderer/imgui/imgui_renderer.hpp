#pragma once

#include <vector>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/rendergraph.hpp>

struct ImTextureData;

namespace eng
{
namespace gfx
{

class ImGuiRenderer
{
    struct ImPassData
    {
        RGResourceId output;
    };

  public:
    void init();
    ImPassData update(RGRenderGraph* graph);

  private:
    void handle_imtexture(ImTextureData* imtex);

  public:
    Signal<void(RGBuilder&)> ui_callbacks;

  private:
    Handle<Pipeline> pipeline;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
};

} // namespace gfx
} // namespace eng