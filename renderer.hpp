#pragma once

#include "model_loader.hpp"

class Renderer {
  public:
    virtual void render_model(Model& model) {}
};
