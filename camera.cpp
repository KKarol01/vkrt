#include "camera.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include "engine.hpp"

Camera::Camera(float fov_radians, float min_dist, float max_dist)
    : projection{ glm::perspectiveFov(fov_radians, static_cast<float>(Engine::window()->size[0]),
                                      static_cast<float>(Engine::window()->size[1]), min_dist, max_dist) } {}

void Camera::update() {
    GLFWwindow* window = Engine::window()->window;
    const float dt = Engine::delta_time();

    if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && glfwGetTime() - last_press_time > 0.3f) {
        last_press_time = glfwGetTime();
        enabled = !enabled;
    }

    if(!enabled) { return; }

    glm::quat rot = glm::angleAxis(yaw, glm::vec3{ 0.0f, 1.0f, 0.0f });
    rot = rot * glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });

    glm::vec3 forward = glm::normalize(rot * glm::vec3{ 0.0f, 0.0f, -1.0f });
    glm::vec3 right = glm::normalize(rot * glm::vec3{ 1.0f, 0.0f, 0.0f });
    glm::vec3 up = glm::vec3{ 0.0f, 1.0f, 0.0f };

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { pos += forward * dt; }
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { pos -= right * dt; }
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { pos -= forward * dt; }
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { pos += right * dt; }
    if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { pos -= up * dt; }
    if(glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { pos += up * dt; }

    view = glm::lookAt(pos, pos + forward, up);
}

void Camera::on_mouse_move(float px, float py) {
    const float dt = Engine::delta_time();

    pitch += (py - lpy) * dt;
    yaw += (lpx - px) * dt;
    pitch = glm::clamp(pitch, -89.0f, 89.0f);
    lpx = px;
    lpy = py;
}
