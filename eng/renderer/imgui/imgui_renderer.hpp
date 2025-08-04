#pragma once

#include <eng/common/handle.hpp>

namespace gfx
{
class CommandPool;
struct Pipeline;
struct Sampler;
struct Texture;
struct Image;

class ImGuiRenderer
{
  public:
    void initialize();
    void render();

  private:
    Handle<Pipeline> pipeline;
    CommandPool* cmdpool;
    Handle<Sampler> sampler;
    Handle<Image> font_image;
    Handle<Texture> font_texture;
};
} // namespace gfx