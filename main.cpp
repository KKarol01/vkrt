#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/ecs/ecs.hpp>
#include <random>
#include <type_traits>
#include <eng/common/callback.hpp>
#include <eng/renderer/rendergraph.hpp>

int main(int argc, char* argv[])
{
    eng::Engine::get().init(argc, argv);
    app::App app;
    app.start();
    eng::Engine::get().start();

    return 0;
}
