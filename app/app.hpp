#pragma once

namespace app
{

class Renderer
{
  public:
    void init();
    void update();
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