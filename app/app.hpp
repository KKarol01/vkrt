#pragma once

#include <array>
#include <vector>
#include <eng/common/handle.hpp>
#include <eng/renderer/renderer.hpp>

namespace app
{

struct Renderer
{
    eng::Handle<eng::gfx::Image> gfx;
};

class App
{
  public:
    void start();
    void update();
};

} // namespace app