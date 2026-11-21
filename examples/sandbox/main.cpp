#include <eng/engine.hpp>
#include <app/app.hpp>

int main(int argc, char* argv[])
{
    eng::get_engine().init(argc, argv);
    app::App app;
    app.start();
    eng::get_engine().start();
    eng::get_engine().destroy();

    return 0;
}
