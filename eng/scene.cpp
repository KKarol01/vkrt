#include <stack>
#include <functional>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image.h>
#include <eng/scene.hpp>
#include <eng/model_importer.hpp>
#include <eng/engine.hpp>
#include <eng/common/components.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>

static Handle<gfx::Image> scene_load_image(fastgltf::Asset& asset, fastgltf::Image& img, gfx::ImageFormat format) {
    std::byte* img_data{};
    int width{}, height{}, ch{};
    // clang-format off
    const auto result = std::visit(fastgltf::visitor{
        [](auto& source) { return false; },
        [&](fastgltf::sources::BufferView& source) {
            auto& bview = asset.bufferViews.at(source.bufferViewIndex);
            auto& buff = asset.buffers.at(bview.bufferIndex);
            return std::visit(fastgltf::visitor{
                        [](auto&){ return false; },
                        [&](fastgltf::sources::Array& vector) {
                        auto* data = stbi_load_from_memory(reinterpret_cast<stbi_uc*>(vector.bytes.data() + bview.byteOffset), bview.byteLength, &width, &height, &ch, 4);
                        img_data = reinterpret_cast<std::byte*>(data);
                        return true;
                    }}, buff.data);
        }
    }, img.data);
    // clang-format on
    if(!result) { return Handle<gfx::Image>{}; }
    return Engine::get().renderer->batch_texture(gfx::ImageDescriptor{
        .name = img.name.c_str(),
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = format,
        .data = { img_data, img_data + width * height * ch },
    });
}

struct SceneLoadingState {
    struct MaterialDescriptor {
        std::string name;
        Handle<gfx::Material> handle{};
        Handle<gfx::Image> base_color_image_handle{};
        Handle<gfx::Image> normal_image_handle{};
        Handle<gfx::Image> metallic_roughness_handle{};
    };
    std::vector<Handle<gfx::Image>> images;
    std::vector<MaterialDescriptor> materials;
};

static Handle<gfx::Image> scene_get_or_load_image(SceneLoadingState& state, fastgltf::Asset& asset,
                                                  uint32_t texture_index, gfx::ImageFormat format) {
    if(state.images.size() <= texture_index) {
        state.images.resize(texture_index + 1);
        auto& texture = asset.textures.at(texture_index);
        auto& image = asset.images.at(*texture.imageIndex);
        state.images.at(texture_index) = scene_load_image(asset, image, format);
    }
    return state.images.at(texture_index);
}

static void scene_load_mesh(SceneLoadingState& state, fastgltf::Asset& asset, fastgltf::Primitive& prim,
                            scene::Scene* scene, scene::Node* node) {
    static const std::array<std::string, 4> attribs{ "POSITION", "NORMAL", "TEXCOORD_0", "TANGENT" };
    static const std::array<uint32_t, 4> attrib_offsets{ offsetof(gfx::Vertex, pos), offsetof(gfx::Vertex, nor),
                                                         offsetof(gfx::Vertex, uv), offsetof(gfx::Vertex, tang) };
    static const std::array<uint32_t, 4> attrib_sizes{ sizeof(gfx::Vertex::pos), sizeof(gfx::Vertex::nor),
                                                       sizeof(gfx::Vertex::uv), sizeof(gfx::Vertex::tang) };

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    for(int i = 0; i < attribs.size(); ++i) {
        const auto set_vertex_component = [&](auto val, size_t idx) {
            memcpy((std::byte*)&vertices.at(idx) + attrib_offsets.at(i), &val, attrib_sizes.at(i));
        };

        auto attrib_it = prim.findAttribute(attribs[i]);
        if(i == 0 && attrib_it == prim.attributes.end()) { break; }
        if(attrib_it != prim.attributes.end()) {
            auto& accessor = asset.accessors.at(attrib_it->accessorIndex);
            if(i == 0) { vertices.resize(accessor.count); }
            if(accessor.type == fastgltf::AccessorType::Vec2) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, accessor, set_vertex_component);
            } else if(accessor.type == fastgltf::AccessorType::Vec3) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor, set_vertex_component);
            } else if(accessor.type == fastgltf::AccessorType::Vec4) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, accessor, set_vertex_component);
            }
        }
    }

    auto& indices_accesor = asset.accessors.at(*prim.indicesAccessor);
    indices.resize(indices_accesor.count);
    fastgltf::copyFromAccessor<uint32_t>(asset, indices_accesor, indices.data());

    auto& node_primitive = node->primitives.emplace_back();
    node_primitive.geometry_handle = Engine::get().renderer->batch_geometry({ .vertices = vertices, .indices = indices });
    node_primitive.mesh_handle =
        Engine::get().renderer->batch_mesh(gfx::MeshDescriptor{ .geometry = node_primitive.geometry_handle });

    if(prim.materialIndex) {
        if(state.materials.size() > *prim.materialIndex) {
            node_primitive.material_handle = state.materials.at(*prim.materialIndex).handle;
        } else {
            state.materials.resize(*prim.materialIndex + 1);
            auto& material = asset.materials.at(*prim.materialIndex);
            auto& state_material = state.materials.at(*prim.materialIndex);
            if(material.pbrData.baseColorTexture) {
                state_material.base_color_image_handle =
                    scene_get_or_load_image(state, asset, material.pbrData.baseColorTexture->textureIndex,
                                            gfx::ImageFormat::R8G8B8A8_SRGB);
            }
            if(material.normalTexture) {
                state_material.normal_image_handle =
                    scene_get_or_load_image(state, asset, material.normalTexture->textureIndex, gfx::ImageFormat::R8G8B8A8_UNORM);
            }
            if(material.pbrData.metallicRoughnessTexture) {
                state_material.metallic_roughness_handle =
                    scene_get_or_load_image(state, asset, material.pbrData.metallicRoughnessTexture->textureIndex,
                                            gfx::ImageFormat::R8G8B8A8_UNORM);
            }
            state_material.handle = Engine::get().renderer->batch_material(gfx::MaterialDescriptor{
                .base_color_texture = state_material.base_color_image_handle,
                .normal_texture = state_material.normal_image_handle,
                .metallic_roughness_texture = state_material.metallic_roughness_handle,
            });
            node_primitive.material_handle = state_material.handle;
        }
    }
}

static glm::mat4 scene_compose_matrix(const auto& fastgltf_transform) {
    fastgltf::math::fmat4x4 gltf_mat;
    if(fastgltf_transform.index() == 0) {
        const auto T = std::get<0>(fastgltf_transform).translation;
        const auto R = std::get<0>(fastgltf_transform).rotation;
        const auto S = std::get<0>(fastgltf_transform).scale;
        gltf_mat = fastgltf::math::scale(fastgltf::math::fmat4x4{ 1.0f }, S);
        gltf_mat = fastgltf::math::rotate(fastgltf::math::fmat4x4{ 1.0f }, R) * gltf_mat;
        gltf_mat = fastgltf::math::translate(fastgltf::math::fmat4x4{ 1.0f }, T) * gltf_mat;

    } else {
        gltf_mat = std::get<1>(fastgltf_transform);
    }
    glm::mat4 glm_mat;
    memcpy(&glm_mat, &gltf_mat, sizeof(glm_mat));
    return glm_mat;
}

static void scene_load_nodes(SceneLoadingState& state, fastgltf::Asset& asset, fastgltf::Node& node,
                             scene::Scene* scene, scene::Node* parent_node, glm::mat4 transform) {
    scene::Node* scene_node = scene->add_node();
    scene_node->name = node.name;
    scene_node->transform = scene_compose_matrix(node.transform);
    scene_node->final_transform = scene_node->transform * transform;
    parent_node->children.push_back(scene_node);

    if(node.meshIndex) {
        auto& mesh = asset.meshes.at(*node.meshIndex);
        for(auto& p : mesh.primitives) {
            if(p.type != fastgltf::PrimitiveType::Triangles) { continue; }
            scene_load_mesh(state, asset, p, scene, scene_node);
        }
    }
    for(auto child_idx : node.children) {
        scene_load_nodes(state, asset, asset.nodes.at(child_idx), scene, scene_node, scene_node->final_transform);
    }
}

Handle<scene::Node> scene::Scene::load_from_file(const std::filesystem::path& path) {
    const std::filesystem::path full_path = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path;

    fastgltf::Parser parser;
    auto glbbuffer = fastgltf::GltfDataBuffer::FromPath(full_path);
    if(!glbbuffer) { return Handle<Node>{}; }

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                 fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    auto expected_asset = parser.loadGltfBinary(glbbuffer.get(), full_path.parent_path(), gltfOptions);
    if(!expected_asset) { return Handle<Node>{}; }
    auto& asset = expected_asset.get();

    std::vector<Handle<gfx::Image>> images;
    images.reserve(asset.images.size());

    auto& scene = asset.scenes[0];
    Node* scene_node = &nodes.emplace_back();
    scene_node->name = scene.name.c_str();
    scene_node->handle = Handle<Node>{ generate_handle };
    SceneLoadingState state;
    for(auto idx : scene.nodeIndices) {
        scene_load_nodes(state, asset, asset.nodes.at(idx), this, scene_node, scene_compose_matrix(asset.nodes.at(idx).transform));
    }
    node_handles[scene_node->handle] = scene_node;
    return scene_node->handle;
}

Handle<scene::NodeInstance> scene::Scene::instance_model(Handle<scene::Node> entity) {
    Node* n = node_handles.at(entity);
    NodeInstance* i = add_instance();
    std::stack<NodeInstance*> i_stack;
    i_stack.push(i);
    traverse_node_hierarchy_indexed(n, [&](Node* n, uint32_t idx) {
        NodeInstance* ni = i_stack.top();
        i_stack.pop();
        ni->name = n->name;
        ni->node_handle = n->handle;
        ni->instance_handle = Handle<NodeInstance>{ generate_handle };
        ni->transform = n->transform;
        ni->final_transform = n->transform;
        ni->children.reserve(n->children.size());
        ni->primitives.reserve(n->primitives.size());
        instance_handles[ni->instance_handle] = ni;
        for(auto& p : n->primitives) {
            auto pi = ni->primitives.emplace_back(Engine::get().ecs_storage->create());
            Engine::get().ecs_storage->emplace<components::Transform>(pi, ni->final_transform);
            Engine::get().ecs_storage->emplace<components::Renderable>(
                pi, components::Renderable{ .mesh_handle = p.mesh_handle, .material_handle = p.material_handle });
            Engine::get().renderer->instance_mesh(gfx::InstanceSettings{ .entity = pi });
            Engine::get().renderer->instance_blas(gfx::BLASInstanceSettings{ .entity = pi });
        }
        for(auto& c : n->children) {
            ni->children.push_back(add_instance());
        }
        for(auto it = ni->children.rbegin(); it != ni->children.rend(); ++it) {
            i_stack.push(*it);
        }
    });
    scene.push_back(i);
    return i->instance_handle;
}

void scene::Scene::update_transform(Handle<scene::NodeInstance> entity, glm::mat4 transform) {
    auto* instance = instance_handles.at(entity);
    std::stack<glm::mat4> stack;
    // stack.push(glm::mat4{1.0f});
    stack.push(transform);
    traverse_node_hierarchy_indexed(instance, [&stack](auto* node, auto idx) {
        // remove node's transform, leave parent transform.
        node->final_transform = glm::inverse(node->transform) * node->final_transform;
        // update to new transform, calc new final transform, pass down.
        node->transform = stack.top();
        stack.pop();
        node->final_transform = node->transform * node->final_transform;
        for(auto* c : node->children) {
            if(c) { stack.push(node->final_transform); }
        }
        for(auto p : node->primitives) {
            if(p != components::s_max_entity) {
                Engine::get().ecs_storage->get<components::Transform>(p).transform = node->final_transform;
                Engine::get().renderer->update_transform(p);
            }
        }
    });
}

scene::Node* scene::Scene::add_node() {
    auto& n = nodes.emplace_back();
    n.handle = Handle<Node>{ generate_handle };
    node_handles[n.handle] = &n;
    return &n;
}

scene::NodeInstance* scene::Scene::add_instance() {
    auto& n = node_instances.emplace_back();
    n.instance_handle = Handle<NodeInstance>{ generate_handle };
    return &n;
}

// void scene::Scene::update_transform(Handle<Entity> entity) {
//  uint32_t idx = entity_node_idxs.at(entity);
//  glm::mat4 tr = glm::mat4{ 1.0f };
//  if(nodes.at(idx).parent != ~0u) { tr = final_transforms.at(nodes.at(idx).parent); }
//_update_transform(idx, tr);
//}

// void scene::Scene::_update_transform(uint32_t idx, glm::mat4 t) {
//  Node& node = nodes.at(idx);
//  cmps::Transform& tr = Engine::ec()->get<cmps::Transform>(node.handle);
//  final_transforms.at(idx) = tr.transform * t;
//  if(node.has_component<cmps::Mesh>()) {
//      Engine::get().renderer->update_transform(Engine::ec()->get<cmps::Mesh>(node.handle).ri_handle);
//  }
//  for(uint32_t i = 0; i < node.children_count; ++i) {
//      _update_transform(node.children_offset + i, final_transforms.at(idx));
//  }
//}

bool scene::NodeInstance::has_children() const {
    for(const auto& e : children) {
        if(e) { return true; }
    }
    return false;
}
