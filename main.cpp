#include <eng/engine.hpp>
#include <app/app.hpp>

#include <eng/common/callback.hpp>

static void calld(std::string a) { std::printf("custom %s\n", a.c_str()); }

struct CB
{
    ~CB() { printf("~CB()\n"); }
    void operator()(std::string) const { printf("CB()\n"); }
};

int main()
{
    eng::Engine::get().init();

    App app;

    std::string a{ "karol" };
    std::string a1{};
    {
        eng::Signal<void(std::string)> b;
        b += [](const std::string& a) { std::printf("%s\n", a.c_str()); };
        b += [](std::string a) { std::printf("%s\n", a.c_str()); };
        b += calld;
        {
            CB cb;
            b += cb;
        }
        // b += &calld;
        b.send(a);
    }

    eng::Engine::get().start();
}
