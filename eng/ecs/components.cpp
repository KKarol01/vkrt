#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
static ecs::ComponentId counter = 0;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Node>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Transform>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Material>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Geometry>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Mesh>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecs::Light>::id = counter++;
} // namespace eng