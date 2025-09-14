#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/rendergraph.hpp>

int main()
{
    eng::Engine::get().init();
    app::App app;
    app.start();
    eng::Engine::get().start();
}
