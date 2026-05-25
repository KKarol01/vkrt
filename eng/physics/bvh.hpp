#pragma once

#include <span>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <glm/common.hpp>
#include <eng/common/types.hpp>

namespace eng
{
namespace physics
{

struct AABB
{
    glm::vec3 extent() const { return max - min; }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 min{ FLT_MAX };
    glm::vec3 max{ -FLT_MAX };
};

struct Triangle
{
    glm::vec3 centroid() const { return (a + b + c) / 3.0f; }
    AABB aabb() const { return AABB{ glm::min(glm::min(a, b), c), glm::max(glm::max(a, b), c) }; }
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
};

class BVH
{
    inline static constexpr u32 INVALID_CHILD = ~0u;
    struct Node
    {
        struct Metadata
        {
            u32 level{ ~0u };
        };

        bool is_leaf() const { return pcount > 0; }
        AABB aabb{};
        u32 left_or_pstart{ INVALID_CHILD }; // if pcount==0, it's the index of the left child, and the right child
                                                  // is 1 added to this; if pcount > 0, it's the offset into primitives array.
        u32 pcount{}; // number of primitives. if 0, this is not a leaf node.
    };

  public:
    struct Stats
    {
        size_t size{};
        u32 levels{};
        std::span<const Triangle> tris;
        std::span<const Node> nodes;
        std::span<const Node::Metadata> metadatas;
    };

    BVH() = default;
    BVH(std::span<const std::byte> vertices, size_t stride, std::span<const std::byte> indices = {},
        gfx::IndexFormat index_format = gfx::IndexFormat::U32);

    Stats get_stats() const;

  private:
    void subdivide(u32 node);
    void update_bounds(u32 node);

    u32 levels{};
    std::vector<Triangle> tris;
    std::vector<Node> nodes;
    std::vector<Node::Metadata> metadatas;
};
} // namespace physics
} // namespace eng