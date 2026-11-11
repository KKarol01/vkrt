#include <eng/ecs/components.hpp>
#include <eng/ecs/ecs.hpp>

static size_t counter = 0;
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Node, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Transform, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Material, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Geometry, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Mesh, counter++);
ENG_ECS_DEFINE_COMPONENT_ID(ecsc::Light, counter++);

namespace eng::serialization
{
template <>
void serialize<ecsc::Transform>(std::span<std::byte> dst, const ecsc::Transform& src, size_t& out_bytes_written)
{
    const auto mat = src.to_mat4();
    safe_write(dst.data(), &mat, out_bytes_written, dst.size_bytes(), sizeof(mat));
}

template <>
void deserialize<ecsc::Transform>(ecsc::Transform& dst, std::span<const std::byte> src, size_t& out_bytes_written)
{
    glm::mat4 mat;
    safe_read(&mat, src.data(), out_bytes_written, sizeof(mat), src.size_bytes());
    dst.init(mat);
}
} // namespace eng::serialization
