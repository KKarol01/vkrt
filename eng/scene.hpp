#pragma once
#include <filesystem>
#include <deque>
#include <stack>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <eng/common/types.hpp>
#include <eng/common/spatial.hpp>
#include <eng/ecs/components.hpp>
#include <eng/common/handle.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/common/handlemap.hpp>

namespace eng
{

struct LoadedNode
{
    ecs::entity root;
    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Image>> images;
    std::vector<Handle<gfx::Sampler>> samplers;
    std::vector<Handle<gfx::Texture>> textures;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<ecs::Mesh> meshes;
};

class Scene
{
    struct UIState
    {
        struct Scene
        {
            struct Node
            {
                bool expanded{};
                // bool selected{};
            };
            ecs::entity sel_entity{ ecs::INVALID_ENTITY };
            std::unordered_map<uint32_t, Node> nodes;
        };

        Scene scene;
    };

  public:
    void init();

    ecs::entity load_from_file(const std::filesystem::path& path);
    ecs::entity instance_entity(ecs::entity node);

    void update_transform(ecs::entity entity);

  public:
    void update();
    void ui_draw_scene();
    void ui_draw_inspector();
    void ui_draw_manipulate();

    std::unordered_map<std::filesystem::path, LoadedNode> nodes;
    std::vector<ecs::entity> scene;
    std::vector<ecs::entity> pending_transforms;

    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Image>> images;
    std::vector<Handle<gfx::Texture>> textures;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<std::vector<Handle<gfx::Mesh>>> meshes;
    UIState ui;
};

} // namespace eng