#pragma once

#include <eng/common/handle.hpp>

namespace gfx
{
class CommandPool;
struct Pipeline;
struct Sampler;
struct Texture;

class ImGuiRenderer
{
  public:
    void initialize();
    void begin();
    void render();

  private:
    Handle<Pipeline> pipeline;
    CommandPool* cmdpool;
    Handle<Sampler> sampler;
    Handle<Texture> font_texture;
};
} // namespace gfx