#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/rendergraph.hpp>

int main(int argc, char* argv[])
{
    {
        using namespace eng::ecs;

        Registry reg;

        auto e1 = reg.create();
        auto e2 = reg.create();
        auto e3 = reg.create();

        reg.erase(e3);
        reg.erase(e1);

        auto e4 = reg.create();
        auto e5 = reg.create();
        auto e6 = reg.create();

        reg.erase(e4);
        // reg.erase(e1);

        struct A
        {
            std::string a;
        };
        struct B
        {
            B(float b) { this->b = b; }
            B(B&& b) noexcept { this->b = std::exchange(b.b, 0.0f); }
            B& operator=(B&& b) noexcept
            {
                this->b = std::exchange(b.b, 0.0f);
                return *this;
            }
            ~B() {}
            float b;
        };
        struct C
        {
            int x;
        };
        A a{ "asdf" };
        reg.add_components(e5, A{ "asd" }, B{ 4.0f });
        reg.add_components(e6, A{ "asd1" }, B{ 5.0f }, C{ 1 });
        reg.add_components(e5, C{ 2 });
        reg.add_components(e2, A{ "asd1" }, B{ 5.0f }, C{ 3 });
        {
            A& e2a = reg.get<A>(e2);
            C& e2c = reg.get<C>(e2);
        }
        reg.erase_components<A, B>(e2);
        {
            C& e2c = reg.get<C>(e2);
        }
        reg.erase(e5);
        {
            C& e2c = reg.get<C>(e2);
        }

        e4 = reg.create();
        e5 = reg.create();
        e6 = reg.create();
        auto e7 = reg.create();
        auto e8 = reg.create();

        reg.make_child(e4, e5);
        reg.make_child(e4, e6);
        reg.make_child(e6, e7);
        reg.make_child(e7, e8);

        reg.traverse_hierarchy(e4, [&reg](entity_id id) {
            auto p = reg.get_parent(id);
            if(p) { ENG_LOG("parent {} id {}", *p, *id); }
            else { ENG_LOG("id {}", *id); }
        });
    }

    eng::Engine::get().init(argc, argv);
    app::App app;
    app.start();
    eng::Engine::get().start();

    return 0;
}
