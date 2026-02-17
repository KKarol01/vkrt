#include <eng/engine.hpp>
#include <app/app.hpp>
#include <eng/ecs/ecs.hpp>
#include <random>
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
        // reg.add_components<A>(e1, A{});
        // reg.erase(e1);
        // e1 = reg.create();
        // reg.add_components<A>(e1, A{});
        // reg.erase_components<A>(e1);

        // auto e1 = reg.create();
        // auto e2 = reg.create();
        // auto e3 = reg.create();

        // reg.erase(e3);
        // reg.erase(e1);

        // auto e4 = reg.create();
        // auto e5 = reg.create();
        // auto e6 = reg.create();

        // std::set<entity_id> eids;
        // std::vector<entity_id> eidsvec;
        // auto count = 100000;

        // struct A
        //{
        //     uint32_t val;
        // };
        // struct B
        //{
        //     uint32_t val;
        // };
        // struct C
        //{
        //     uint32_t val;
        // };

        // for(auto i = 0; i < count; ++i)
        //{
        //     auto e = reg.create();
        //     ENG_ASSERT(eids.insert(e).second);
        //     eidsvec.push_back(e);
        //     if(*e.get_slot() % 3 == 0) { reg.add_components<A>(e, A{ *e.get_slot() }); }
        //     if(*e.get_slot() % 3 == 1) { reg.add_components<B>(e, B{ *e.get_slot() }); }
        //     if(*e.get_slot() % 3 == 2) { reg.add_components<C>(e, C{ *e.get_slot() }); }
        // }

        // std::mt19937 gen{ 0 };
        // std::uniform_int_distribution dist{ 0, count - 1 };
        // std::set<entity_id> removed;
        // for(auto i = 0; i < count / 5; ++i)
        //{
        //     auto rnd = dist(gen);
        //     auto rem = eidsvec[rnd];
        //     if(eids.erase(rem)) { reg.erase(rem); }
        //     removed.insert(rem);
        // }

        // eidsvec = { eids.begin(), eids.end() };

        // for(auto i = 0ull; i < removed.size(); ++i)
        //{
        //     auto e = reg.create();
        //     ENG_ASSERT(e.get_version() == 1 && removed.contains(entity_id{ e.get_slot(), 0 }));
        // }

        // for(auto i = 0u; i < reg.metadatas.size(); ++i)
        //{
        //     if(!reg.has(entity_id{ slot_id{ i }, 0 })) { continue; }
        //     if(i % 3 == 0)
        //     {
        //         ENG_ASSERT(reg.has<A>(entity_id{ slot_id{ i }, 0 }) && reg.get<A>(entity_id{ slot_id{ i }, 0 }).val == i);
        //     }
        //     else if(i % 3 == 1)
        //     {
        //         ENG_ASSERT(reg.has<B>(entity_id{ slot_id{ i }, 0 }) && reg.get<B>(entity_id{ slot_id{ i }, 0 }).val == i);
        //     }
        //     else if(i % 3 == 2)
        //     {
        //         ENG_ASSERT(reg.has<C>(entity_id{ slot_id{ i }, 0 }) && reg.get<C>(entity_id{ slot_id{ i }, 0 }).val == i);
        //     }
        // }

        //{
        //    auto e = reg.create();
        //    ENG_ASSERT(e.get_version() == 0 && !removed.contains(e));
        //}

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

        // reg.traverse_hierarchy(e4, [&reg](entity_id id) {
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
