#pragma once

#include <eng/common/handle.hpp>

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
    Handle<Pipeline> pipeline;
    //CommandPool* cmdpool;
    Handle<Sampler> sampler;
    Handle<Image> font_image;
    Handle<Texture> font_texture;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
};
} // namespace gfx