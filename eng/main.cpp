#include <volk/volk.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <GLFW/glfw3native.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "./engine.hpp"
#include <eng/assets/importer.hpp>

int main() {
    Engine::get().init();
    {
        // Handle<scene::Node> model = Engine::get().scene->load_from_file("plane.glb");
        assets::Asset plane;
        {
            const auto max_i = 128.0f;
            const auto max_j = max_i;
            const auto width = 20.0f;
            const auto height = width;
            plane.vertices.reserve(max_i * max_j);
            plane.indices.reserve(((max_i - 1) * (max_j - 1)) * 2 * 3);
            for(auto i = 0; i < max_i; ++i) {
                for(auto j = 0; j < max_j; ++j) {
                    const float u = static_cast<float>(i) / static_cast<float>(max_i - 1);
                    const float v = static_cast<float>(j) / static_cast<float>(max_j - 1);
                    plane.vertices.push_back(assets::Vertex{
                        .position = { u * width - width * 0.5f, 0.0f, v * height - height * 0.5f }, .uv = { u, v } });
                }
            }

            for(auto i = 0; i < max_i - 1; ++i) {
                for(auto j = 0; j < max_j - 1; ++j) {
                    const uint32_t tl = i * max_j + j;
                    const uint32_t tr = tl + 1;
                    const uint32_t bl = (i + 1) * max_j + j;
                    const uint32_t br = (i + 1) * max_j + j + 1;
                    plane.indices.insert(plane.indices.end(), { tl, bl, br, tl, br, tr });
                }
            }

            plane.path = "";
            plane.scene = { 0 };
            plane.nodes.push_back(assets::Node{
                .name = "plane",
                .mesh = 0,
            });
            plane.meshes.push_back(assets::Mesh{ .name = "plane mesh", .submeshes = { 0 } });
            plane.submeshes.push_back(assets::Submesh{ .geometry = 0, .material = 0 });
            plane.geometries.push_back(assets::Geometry{ .vertex_range = { 0, (uint32_t)plane.vertices.size() },
                                                         .index_range = { 0, (uint32_t)plane.indices.size() } });
            plane.materials.push_back(assets::Material{ .name = "plane mat", .color_texture = 0 });
            plane.textures.push_back(assets::Texture{
                .image = 0, .filtering = gfx::ImageFiltering::LINEAR, .addressing = gfx::ImageAddressing::CLAMP_EDGE });
            const std::vector<std::byte> plane_white_color_data{ (std::byte)255, (std::byte)255, (std::byte)255, (std::byte)255 };
            plane.images.push_back(assets::Image{
                .name = "plane white color", .width = 1u, .height = 1u, .format = gfx::ImageFormat::R8G8B8A8_SRGB, .data = plane_white_color_data });
            plane.transforms.push_back(glm::mat4{ 1.0f });
        }
        const auto plane_model = Engine::get().scene->load_from_asset(plane);
        // const auto plane_instance3 = Engine::get().scene->instance_model(plane_model);
        for(int i = -5; i < 11; ++i) {
            for(int j = -5; j < 11; ++j) {
                const auto plane_instance = Engine::get().scene->instance_model(plane_model);
                Engine::get().scene->update_transform(plane_instance,
                                                      glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 20.0f * i, 0.0f, 20.0f * j }));
            }
        }
        // Engine::get().set_on_update_callback([&]() {
        //     auto& ni = Engine::get().scene->get_instance(cornell_instance);
        //     // Engine::get().scene->update_transform(cornell_instance, ni.transform * glm::rotate(glm::radians(1.0f), glm::vec3{ 0.0f, 1.0f, 0.0f }));
        // });
    }
    Engine::get().start();
}
