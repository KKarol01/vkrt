#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/ecs/ecs.hpp>
#include <random>
#include <type_traits>
#include <eng/common/callback.hpp>
#include <eng/renderer/rendergraph.hpp>

int main(int argc, char* argv[])
{
    {
        using namespace eng::ecs;

        Registry reg;

        // struct A
        //{
        // };

        // auto e1 = reg.create();
        //  reg.add_components<A>(e1, A{});
        // reg.erase(e1);
        // auto e1_2 = reg.create();
        // ENG_ASSERT(!reg.has(e1) && reg.has(e1_2));
        //  reg.add_components<A>(e1, A{});
        //  reg.erase_components<A>(e1);

        // auto e1 = reg.create();
        // auto e2 = reg.create();
        // auto e3 = reg.create();

        // reg.erase(e3);
        // reg.erase(e1);

        // auto e4 = reg.create();
        // auto e5 = reg.create();
        // auto e6 = reg.create();

        struct A
        {
            uint32_t val;
        };
        struct B
        {
            uint32_t val;
        };
        struct C
        {
            uint32_t val;
        };
        auto count = 100000;

        {
            std::set<EntityId> eids;
            std::vector<EntityId> eidsvec;

            for(auto i = 0; i < count; ++i)
            {
                auto e = reg.create();
                ENG_ASSERT(eids.insert(e).second);
                eidsvec.push_back(e);
                if(*e.get_slot() % 3 == 0) { reg.add_components<A>(e, A{ *e.get_slot() }); }
                if(*e.get_slot() % 3 == 1) { reg.add_components<B>(e, B{ *e.get_slot() }); }
                if(*e.get_slot() % 3 == 2) { reg.add_components<C>(e, C{ *e.get_slot() }); }
            }

            std::mt19937 gen{ 0 };
            std::uniform_int_distribution dist{ 0, count - 1 };
            std::set<EntityId> removed;
            for(auto i = 0; i < count / 5; ++i)
            {
                auto rnd = dist(gen);
                auto rem = eidsvec[rnd];
                if(eids.erase(rem)) { reg.erase(rem); }
                removed.insert(rem);
            }

            eidsvec = { eids.begin(), eids.end() };

            for(auto i = 0ull; i < removed.size(); ++i)
            {
                auto e = reg.create();
                ENG_ASSERT(e.get_version() == 1 && removed.contains(EntityId{ e.get_slot(), 0 }));
            }

            for(auto i = 0u; i < reg.metadatas.size(); ++i)
            {
                if(!reg.has(EntityId{ SlotId{ i }, 0 })) { continue; }
                if(i % 3 == 0)
                {
                    ENG_ASSERT(reg.has<A>(EntityId{ SlotId{ i }, 0 }) && reg.get<A>(EntityId{ SlotId{ i }, 0 }).val == i);
                }
                else if(i % 3 == 1)
                {
                    ENG_ASSERT(reg.has<B>(EntityId{ SlotId{ i }, 0 }) && reg.get<B>(EntityId{ SlotId{ i }, 0 }).val == i);
                }
                else if(i % 3 == 2)
                {
                    ENG_ASSERT(reg.has<C>(EntityId{ SlotId{ i }, 0 }) && reg.get<C>(EntityId{ SlotId{ i }, 0 }).val == i);
                }
            }
            auto e = reg.create();
            ENG_ASSERT(e.get_version() == 0 && !removed.contains(e));
        }

        {
            reg = Registry{};
            std::vector<EntityId> entities;
            for(int i = 0; i < count; ++i)
            {
                auto e = reg.create();
                entities.push_back(e);

                if(i % 3 == 0) { reg.add_components(e, A{}); }
                else if(i % 3 == 1) { reg.add_components(e, A{}, B{}); }
                else { reg.add_components(e, A{}, B{}, C{}); }
            }

            int aa{}, bb{}, cc{};
            reg.iterate_over_components<A>([&](EntityId, const A&) { ++aa; });
            reg.iterate_over_components<A, B>([&](EntityId, const A&, const B&) { ++bb; });
            reg.iterate_over_components<A, B, C>([&](EntityId, const A&, const B&, const C&) { ++cc; });

            ENG_ASSERT(aa == count);
            ENG_ASSERT(bb == 2 * count / 3);
            ENG_ASSERT(cc == count / 3);

            for(int i = 0; i < count / 2; ++i)
            {
                const auto e = entities[i];

                ENG_ASSERT(reg.has(e));

                switch(i % 3)
                {
                case 0: // A
                    --aa;
                    break;

                case 1: // A,B
                    --aa;
                    --bb;
                    break;

                case 2: // A,B,C
                    --aa;
                    --bb;
                    --cc;
                    break;
                }

                reg.erase(e);
            }

            int _aa{}, _bb{}, _cc{};
            reg.iterate_over_components<A>([&](EntityId, const A&) { ++_aa; });
            reg.iterate_over_components<A, B>([&](EntityId, const A&, const B&) { ++_bb; });
            reg.iterate_over_components<A, B, C>([&](EntityId, const A&, const B&, const C&) { ++_cc; });
            ENG_ASSERT(aa == _aa);
            ENG_ASSERT(bb == _bb);
            ENG_ASSERT(cc == _cc);

            int yy = 1;
        }

        int x = 1;

        // reg.erase(e4);
        //  reg.erase(e1);

        // struct A
        //{
        //     std::string a;
        // };
        // struct B
        //{
        //     B(float b) { this->b = b; }
        //     B(B&& b) noexcept { this->b = std::exchange(b.b, 0.0f); }
        //     B& operator=(B&& b) noexcept
        //     {
        //         this->b = std::exchange(b.b, 0.0f);
        //         return *this;
        //     }
        //     ~B() {}
        //     float b;
        // };
        // struct C
        //{
        //     int x;
        // };
        //  A a{ "asdf" };
        //  reg.add_components(e5, A{ "asd" }, B{ 4.0f });
        //  reg.add_components(e6, A{ "asd1" }, B{ 5.0f }, C{ 1 });
        //  reg.add_components(e5, C{ 2 });
        //  reg.add_components(e2, A{ "asd1" }, B{ 5.0f }, C{ 3 });
        //{
        //      A& e2a = reg.get<A>(e2);
        //      C& e2c = reg.get<C>(e2);
        //  }
        //  reg.erase_components<A, B>(e2);
        //{
        //      C& e2c = reg.get<C>(e2);
        //  }
        //  reg.erase(e5);
        //{
        //      C& e2c = reg.get<C>(e2);
        //  }

        // e4 = reg.create();
        // e5 = reg.create();
        // e6 = reg.create();
        // auto e7 = reg.create();
        // auto e8 = reg.create();

        // reg.make_child(e4, e5);
        // reg.make_child(e4, e6);
        // reg.make_child(e6, e7);
        // reg.make_child(e7, e8);

        // reg.traverse_hierarchy(e4, [&reg](EntityId id) {
        //     auto p = reg.get_parent(id);
        //     if(p) { ENG_LOG("parent {} id {}", *p, *id); }
        //     else { ENG_LOG("id {}", *id); }
        // });
    }

    eng::Engine::get().init(argc, argv);
    app::App app;
    app.start();
    eng::Engine::get().start();

    return 0;
}
