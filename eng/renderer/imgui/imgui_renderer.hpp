#pragma once

#include <vector>
#include <functional>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>

struct ImTextureData;

namespace eng
{
namespace gfx
{

class CommandPool;
class CommandBuffer;
struct Buffer;
struct Pipeline;
struct Sampler;
struct Texture;
struct Image;

class ImGuiRenderer
{
  public:
    void init();
    void render(CommandBuffer* cmd);
    void add_ui_callback(const Callback<void()>& cb);

  private:
    void handle_imtexture(ImTextureData* imtex);

    Handle<Pipeline> pipeline;
    Handle<Sampler> sampler;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
    std::vector<Handle<Texture>> textures;
    Signal<void()> ui_callbacks;
};

} // namespace gfx
} // namespace eng