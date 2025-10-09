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
struct ImageView;

class ImGuiRenderer
{
  public:
    void init();
    void update(CommandBuffer* cmd, Handle<ImageView> output);

  private:
    void handle_imtexture(ImTextureData* imtex);

  public:
    Signal<void()> ui_callbacks;

  private:
    Handle<Pipeline> pipeline;
    Handle<Sampler> sampler;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
    std::vector<Handle<Texture>> textures;
};

} // namespace gfx
} // namespace eng