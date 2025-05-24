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

#include <meshoptimizer/src/meshoptimizer.h>

int main() {
    Engine::get().init();

    auto bunny = assets::Importer::import_glb("sphere.glb", assets::ImportSettings{ .flags = assets::ImportFlags::MESHLETIZE_BIT });
    for(auto& g : bunny.geometries) {
        std::span<uint32_t> indices = { bunny.indices.data() + g.index_range.offset,
                                        bunny.indices.data() + g.index_range.offset + g.index_range.size };
        std::span<assets::Vertex> vertices = { bunny.vertices.data() + g.vertex_range.offset,
                                               bunny.vertices.data() + g.vertex_range.offset + g.vertex_range.size };
        const auto face_count = indices.size() / 3;

        const auto meshlet_max_verts = 64u;
        const auto meshlet_max_tris = 124u;
        const auto meshlet_cone_weight = 0.0f;

        const auto max_meshlets = meshopt_buildMeshletsBound(bunny.indices.size(), meshlet_max_verts, meshlet_max_tris);
        std::vector<meshopt_Meshlet> meshlets(max_meshlets);
        std::vector<uint32_t> meshlets_verts(max_meshlets * meshlet_max_verts);
        std::vector<uint8_t> meshlets_triangles(max_meshlets * meshlet_max_tris * 3);

        const auto meshlet_count =
            meshopt_buildMeshlets(meshlets.data(), meshlets_verts.data(), meshlets_triangles.data(), bunny.indices.data(),
                                  bunny.indices.size(), &bunny.vertices.at(0).position.x, bunny.vertices.size(),
                                  sizeof(bunny.vertices.at(0)), meshlet_max_verts, meshlet_max_tris, meshlet_cone_weight);

        // downsizing from pessimistic case
        const auto& last_meshlet = meshlets.at(meshlet_count - 1);
        meshlets_verts.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
        meshlets_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshlet_count);

        for(auto& m : meshlets) {
            meshopt_optimizeMeshlet(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                    m.triangle_count, m.vertex_count);
        }

        std::move(bunny.indices.begin(), bunny.indices.end(), bunny.indices.begin() + g.index_range.offset);
        std::move(bunny.vertices.begin(), bunny.vertices.end(), bunny.vertices.begin() + g.vertex_range.offset);

        g.meshlets_range = { .offset = bunny.meshlets.size(), .size = meshlet_count };
        bunny.meshlets.reserve(bunny.meshlets.size() + meshlets.size());
        for(const auto& m : meshlets) {
            const auto am = assets::Meshlet{ .vertex_range = { bunny.meshlets_vertices.size(), m.vertex_count },
                                             .triangle_range = { bunny.meshlets_triangles.size(), m.triangle_count } };
            bunny.meshlets.push_back(am);
            bunny.meshlets_vertices.insert(bunny.meshlets_vertices.end(), meshlets_verts.begin() + m.vertex_offset,
                                           meshlets_verts.begin() + m.vertex_offset + m.vertex_count);
            bunny.meshlets_triangles.insert(bunny.meshlets_triangles.end(), meshlets_triangles.begin() + m.triangle_offset,
                                            meshlets_triangles.begin() + m.triangle_offset + m.triangle_count * 3);
        }

        // bunny.meshlets_vertices.insert(bunny.meshlets_vertices.end(), meshlets_verts.begin(), meshlets_verts.end());
        // bunny.meshlets_triangles.insert(bunny.meshlets_triangles.end(), meshlets_triangles.begin(), meshlets_triangles.end());
    }

    const auto scene_bunny = Engine::get().scene->load_from_asset(bunny);
    const auto bunny_instance = Engine::get().scene->instance_model(scene_bunny);

    Engine::get().start();
}
