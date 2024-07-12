#pragma once

#include "model_importer.hpp"
#include "handle.hpp"
#include "flags.hpp"

enum class BatchFlags : uint32_t { RAY_TRACED_BIT = 0x1 };
inline Flags<BatchFlags> operator|(BatchFlags a, BatchFlags b) { return Flags{ a } | b; }
inline Flags<BatchFlags> operator&(BatchFlags a, BatchFlags b) { return Flags{ a } & b; }

struct BatchSettings {
    Flags<BatchFlags> flags;
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
    virtual HandleBatchedModel batch_model(ImportedModel& model, BatchSettings settings) = 0;
    virtual HandleInstancedModel instance_model(HandleBatchedModel model) = 0;
};
