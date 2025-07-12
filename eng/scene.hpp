#pragma once
#include <filesystem>
#include <deque>
#include <stack>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "./common/types.hpp"
#include "./common/spatial.hpp"
#include <eng/common/components.hpp>
#include <eng/common/handle.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/common/handlemap.hpp>

namespace scene
{

class Scene
{
  public:
    ecs::Entity load_from_file(const std::filesystem::path& path);
    ecs::Entity instance_entity(ecs::Entity node);

    void update_transform(ecs::Entity entity, glm::mat4 transform);

  public:
    std::unordered_map<std::filesystem::path, ecs::Entity> nodes;
    std::vector<ecs::Entity> scene;

    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Image>> images;
    std::vector<Handle<gfx::Texture>> textures;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<std::vector<Handle<gfx::Mesh>>> meshes;
};
} // namespace scene