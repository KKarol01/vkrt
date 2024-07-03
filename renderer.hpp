#pragma once

#include "model_importer.hpp"

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void render_model(ImportedModel& model) = 0;
};
