#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/common/callback.hpp>

int main()
{
    eng::Engine::get().init();
    app::App app;
    app.start();
    eng::Engine::get().start();
}
