#pragma once

#include "model_loader.hpp"

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void render_model(Model& model) = 0;
};
