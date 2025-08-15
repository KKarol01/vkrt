#pragma once

#include <eng/common/handle.hpp>

struct ImTextureData;

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
    void initialize();
    void render(CommandBuffer* cmd);

  private:
    void handle_texture(ImTextureData* imtex);

    Handle<Pipeline> pipeline;
    Handle<Sampler> sampler;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
    std::vector<Handle<Texture>> textures;
};
} // namespace gfx