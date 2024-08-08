#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glfw/glfw3.h>

class Camera {
  public:
    glm::mat4 update(GLFWwindow* window, float dt) {
        this->dt = dt;
        if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && glfwGetTime() - last_press_time > 0.3f) {
            last_press_time = glfwGetTime();
            enabled = !enabled;
        }
        if(!enabled) { return last_v; }

        glm::quat rot = glm::angleAxis(yaw, glm::vec3{ 0.0f, 1.0f, 0.0f });
        rot = rot * glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });

        glm::vec3 forward = glm::normalize(rot * glm::vec3{ 0.0f, 0.0f, -1.0f });
        glm::vec3 right = glm::normalize(rot * glm::vec3{ 1.0f, 0.0f, 0.0f });
        glm::vec3 up = glm::vec3{ 0.0f, 1.0f, 0.0f };

        if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { pos += forward * dt; }
        if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { pos += right * dt; }
        if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { pos -= forward * dt; }
        if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { pos -= right * dt; }
        if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { pos -= up * dt; }
        if(glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { pos += up * dt; }

        last_v = glm::lookAt(pos, pos + forward, up);
        return last_v;
    }

    void on_mouse_move(float px, float py) {
        pitch += (py - lpy) * dt;
        yaw += (lpx - px) * dt;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
        lpx = px;
        lpy = py;
    }

  private:
    glm::mat4 last_v;
    glm::vec3 pos{ 0.0f };
    float pitch{ 0.0f }, yaw{ 0.0f };
    float lpx{ -1.0f }, lpy{ -1.0f };
    float dt;
    float last_press_time{ 0.0f };
    bool enabled{ true };
};