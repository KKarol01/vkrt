#include <eng/engine.hpp>
#include <app/app.hpp>

int main(int argc, char* argv[])
{
    eng::Engine::get().init(argc, argv);
    app::App app;
    app.start();
    eng::Engine::get().start();

    return 0;
}
