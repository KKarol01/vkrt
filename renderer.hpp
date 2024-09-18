#pragma once

#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "flags.hpp"

enum class BatchFlags : uint32_t { RAY_TRACED_BIT = 0x1 };
enum class InstanceFlags : uint32_t { RAY_TRACED_BIT = 0x1 };

struct BatchSettings {
    Flags<BatchFlags> flags;
};

struct InstanceSettings {
    Flags<InstanceFlags> flags;
    glm::mat4x3 transform{ 1.0f };
    uint32_t tlas_instance_mask : 8 { 0xFF };
};

struct ScreenRect {
    int offset_x, offset_y;
    uint32_t width, height;
};

struct BatchedModel;
typedef Handle<BatchedModel> HandleBatchedModel;
struct InstancedModel;
typedef Handle<InstancedModel> HandleInstancedModel;

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void render() = 0;
    virtual void set_screen_rect(ScreenRect rect) = 0;
    virtual HandleBatchedModel batch_model(const ImportedModel& model, BatchSettings settings) = 0;
    virtual HandleInstancedModel instance_model(HandleBatchedModel model, InstanceSettings settings) = 0;
};
