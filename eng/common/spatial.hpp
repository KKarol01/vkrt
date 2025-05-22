#pragma once

struct BoundingBox {
    glm::vec3 center() const { return (max + min) * 0.5f; }
    glm::vec3 size() const { return (max - min); }
    glm::vec3 extent() const { return glm::abs(size() * 0.5f); }
    glm::vec3 min{ 0.0f }, max{ 0.0f };
};