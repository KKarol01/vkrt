#pragma once
#include <filesystem>
#include <deque>
#include <stack>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <eng/common/types.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/physics/bvh.hpp>
#include <eng/common/indexed_hierarchy.hpp>

namespace eng
{

namespace assets
{
struct Asset;
}

class Scene
{
  public:
    ecs::EntityId instance_asset(const assets::Asset& asset);

  public:
    std::vector<ecs::EntityId> scene;
};

} // namespace eng