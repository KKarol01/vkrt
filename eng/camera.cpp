#include <eng/camera.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glfw/glfw3.h>
#include <eng/engine.hpp>
#include "logger.hpp"

Camera::Camera(float fov_radians, float min_dist, float max_dist)
    : projection{ glm::perspectiveFov(glm::radians(80.0f), static_cast<float>(1280.0f), static_cast<float>(768.0f), min_dist, max_dist) } {
    GLFWwindow* window = Engine::get().window->window;

    double pos[2];
    glfwGetCursorPos(window, &pos[0], &pos[1]);
    lpx = pos[0];
    lpy = pos[1];
}

void Camera::update() {
    GLFWwindow* window = Engine::get().window->window;
    const float dt = Engine::get().delta_time() * 5.0f;

    if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && glfwGetTime() - last_press_time > 0.3f) {
        last_press_time = glfwGetTime();
        enabled = !enabled;
        if(!enabled) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }

    if(!enabled) { return; }

    glm::quat rot = glm::angleAxis(yaw, glm::vec3{ 0.0f, 1.0f, 0.0f });
    rot = rot * glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });

    glm::vec3 forward = glm::normalize(rot * glm::vec3{ 0.0f, 0.0f, -1.0f });
    glm::vec3 right = glm::normalize(rot * glm::vec3{ 1.0f, 0.0f, 0.0f });
    glm::vec3 up = glm::cross(right, forward);

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { pos += forward * dt; }
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { pos -= right * dt; }
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { pos -= forward * dt; }
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { pos += right * dt; }
    if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { pos -= up * dt; }
    if(glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { pos += up * dt; }

    view = glm::lookAt(pos, pos + forward, up);
    ENG_LOG("POS: {} {} {}", pos.x, pos.y, pos.z);
    ENG_LOG("DIR: {} {} {}", forward.x, forward.y, forward.z);
}

void Camera::on_mouse_move(float px, float py) {
    if(enabled) {
        pitch += glm::radians((lpy - py));
        yaw += glm::radians((lpx - px));
        pitch = glm::clamp(pitch, -glm::half_pi<float>(), glm::half_pi<float>());
    }
    lpx = px;
    lpy = py;
}
