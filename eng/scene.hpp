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
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/physics/bvh.hpp>
#include <eng/common/indexed_hierarchy.hpp>

namespace eng
{

struct SceneNode
{
    std::string name;
    Range32u meshes{};
    uint32_t transform{ ~0u };
};

struct SceneNodeId : public TypedId<SceneNode, uint32_t>
{
    using TypedId::TypedId;
    operator IndexedHierarchy::NodeId() const { return IndexedHierarchy::NodeId{ handle }; }
};

class Scene
{
  public:
    void init();

    SceneNodeId load_from_file(const std::filesystem::path& model_path);
    ecs::EntityId instance_model(SceneNodeId nodeid);

  public:
    SceneNodeId make_node(const std::string& name);
    SceneNode& get_node(SceneNodeId id);

    std::unordered_map<std::filesystem::path, SceneNodeId> loaded_models;
    std::vector<ecs::EntityId> scene;

    std::vector<ecs::Transform> transforms;
    std::vector<ecs::Geometry> geometries;
    std::vector<Handle<gfx::Image>> images;
    std::vector<gfx::ImageView> textures;
    std::vector<ecs::Material> materials;
    std::vector<ecs::Mesh> meshes;

    std::vector<SceneNode> nodes;
    inline static SceneNode null_node{};
    IndexedHierarchy hierarchy;
};

} // namespace eng