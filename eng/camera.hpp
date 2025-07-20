#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

class Camera
{
  public:
    Camera(float fov_radians, float min_dist, float max_dist);

    void update();
    void update_projection(const glm::mat4& projection) { this->projection = projection; }
    glm::mat4 get_view() const { return view; }
    glm::mat4 get_projection() const { return projection; }
    void on_mouse_move(float px, float py);

    glm::vec3 pos{ 0.24723034, 0.3212749, 2.013261 };

  private:
    glm::mat4 projection{ 1.0f };
    glm::mat4 view{ 1.0f };
    float pitch{ 0.0f }, yaw{ 0.0f };
    float lpx{ -1.0f }, lpy{ -1.0f };
    float last_press_time{ 0.0f };
    bool enabled{ false };
};