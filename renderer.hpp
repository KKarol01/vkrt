#pragma once

#include "model_importer.hpp"
#include "handle.hpp"

struct A {
    std::string asdf;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void batch_model(ImportedModel& model) = 0;
    virtual Handle<A> build_blas() { return {}; }
};
