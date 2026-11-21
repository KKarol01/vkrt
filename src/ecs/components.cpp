#include <eng/ecs/components.hpp>
#include <eng/ecs/ecs.hpp>

static size_t counter = 0;
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Node, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Transform, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Material, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Geometry, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Mesh, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Light, counter++);