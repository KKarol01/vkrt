#include <eng/engine.hpp>
#include <app/app.hpp>

#include <type_traits>

int main(int argc, char* argv[])
{
    eng::get_engine().init(argc, argv);
    app::App app;
    app.start();
    eng::get_engine().start();

    return 0;
}
