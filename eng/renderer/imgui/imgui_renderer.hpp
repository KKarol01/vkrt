#pragma once

#include <vector>
#include <functional>
#include <eng/common/handle.hpp>

struct ImTextureData;

namespace eng
{
namespace gfx
{

class CommandPool;
class CommandBuffer;
struct Buffer;
struct Pipeline;
struct Sampler;
struct Texture;
struct Image;

class ImGuiRenderer
{
  public:
    using callback_t = std::function<void()>;

    void initialize();
    void render(CommandBuffer* cmd);
    uint32_t add_ui_callback(const callback_t& cb);
    void remove_ui_callback(uint32_t idx);

  private:
    void handle_imtexture(ImTextureData* imtex);

    Handle<Pipeline> pipeline;
    Handle<Sampler> sampler;
    Handle<Buffer> vertex_buffer;
    Handle<Buffer> index_buffer;
    std::vector<Handle<Image>> images;
    std::vector<Handle<Texture>> textures;
    std::vector<std::function<void()>> ui_callbacks;
    std::deque<uint32_t> free_ui_callbacks;
};

} // namespace gfx
} // namespace eng