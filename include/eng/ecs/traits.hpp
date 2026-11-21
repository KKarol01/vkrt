#pragma once

#include <cstdint>
#include <limits>
#include <bitset>
#include <type_traits>

namespace eng
{
namespace ecs
{
using ComponentId = u32;
inline static constexpr u32 MAX_COMPONENTS = std::numeric_limits<ComponentId>::digits;
using Signature = std::bitset<MAX_COMPONENTS>;

struct ComponentTraits
{
    template <typename Component> struct Id
    {
        static_assert(!std::is_reference_v<Component> && !std::is_pointer_v<Component>);
        static_assert(!std::is_const_v<Component> && !std::is_volatile_v<Component>);
        static_assert(std::is_object_v<Component>);
        static const ComponentId id;
    };

    // Gets stable unique 0-based index for component
    template <typename Component> static ComponentId get_id()
    {
        using RawComp = std::remove_cvref_t<Component>;
        return Id<RawComp>::id;
    }
    // Returns bit mask of given components
    template <typename... Components> static Signature get_signature()
    {
        return Signature{ (0 | ... | (1ull << get_id<Components>())) };
    }
};
} // namespace ecs
} // namespace eng