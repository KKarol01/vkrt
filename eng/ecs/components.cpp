#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
static ecs::ComponentId counter = 0;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Node>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Transform>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Material>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Geometry>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Mesh>::id = counter++;
template <> ecs::ComponentId ecs::ComponentTraits::Id<ecsc::Light>::id = counter++;
} // namespace eng