#pragma once

#include <eng/common/handle.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace app
{
using namespace ::eng;

class Renderer
{
  public:
    void init();
    void update();

    Handle<gfx::Pipeline> cull_pipeline;
    Handle<gfx::Pipeline> hiz_pipeline;
};

class App
{
  public:
    void start();
    void on_init();
    void on_update();

    Renderer renderer{};
};

} // namespace app