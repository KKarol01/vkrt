#pragma once
#include <filesystem>
#include <expected>
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
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/physics/bvh.hpp>

namespace eng
{

namespace asset
{
struct Geometry
{
    Handle<gfx::Geometry> render_geometry;
    physics::BVH bvh;
};

struct Image
{
    std::string name;
    Handle<gfx::Image> render_image;
};

struct Texture
{
    std::string name;
    gfx::ImageView view;
};

struct Material
{
    std::string name;
    Handle<gfx::Material> render_material;
};

struct Mesh
{
    std::string name;
    std::vector<Handle<gfx::Mesh>> render_meshes;
    Range32u geometries;
};

struct Model
{
    struct Node
    {
        std::string name;
        glm::mat4 transform{ 1.0f };
        uint32_t mesh{ ~0ul };
        std::vector<uint32_t> children;
    };
    std::vector<Geometry> geometries;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    uint32_t root_node{ ~0u }; // usually it's the last node.
};

namespace import
{

class IModelImporter
{
  public:
    virtual ~IModelImporter() = default;
    virtual std::expected<asset::Model, std::string> load_model(const std::filesystem::path& path) = 0;
};

class GLTFModelImporter : public IModelImporter
{
  public:
    ~GLTFModelImporter() override = default;
    std::expected<asset::Model, std::string> load_model(const std::filesystem::path& path) override;
};

using file_extension_t = std::filesystem::path;
inline std::unordered_map<file_extension_t, std::unique_ptr<IModelImporter>> file_importers;

} // namespace import
} // namespace asset

class Scene
{
    struct UIState
    {
        struct Scene
        {
            struct Node
            {
                bool expanded{};

                bool bvh_level_exclusive{};
                int bvh_level{};
                // bool selected{};
            };
            ecs::EntityId sel_entity{};
            std::unordered_map<ecs::EntityId, Node> nodes;
        };

        Scene scene;
    };

  public:
    void init();

    asset::Model* load_from_file(const std::filesystem::path& path);
    ecs::EntityId instance_model(const asset::Model* model);

    void update_transform(ecs::EntityId entity);

  public:
    void update();
    void ui_draw_scene();
    void ui_draw_inspector();
    void ui_draw_manipulate();

    std::unordered_map<std::filesystem::path, asset::Model> loaded_models;
    std::vector<ecs::EntityId> scene;
    std::vector<ecs::EntityId> pending_transforms;

    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Image>> images;
    std::vector<Handle<gfx::Texture>> textures;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<std::vector<Handle<gfx::Mesh>>> meshes;
    UIState ui;
};

} // namespace eng