#include <eng/camera.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glfw/glfw3.h>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>


static glm::mat4 infinitePerspectiveFovReverseZRH_ZO(float fov, float width, float height, float zNear) {
    const float h = 1.0f / glm::tan(0.5f * fov);
    const float w = h * height / width;
    glm::mat4 result = glm::zero<glm::mat4>();
    result[0][0] = w;
    result[1][1] = h;
    result[2][2] = 0.0f;
    result[2][3] = -1.0f;
    result[3][2] = zNear;
    return result;
};

Camera::Camera(float fov_radians, float min_dist, float max_dist)
    : projection{ glm::perspectiveFov(glm::radians(80.0f), static_cast<float>(1280.0f), static_cast<float>(768.0f), min_dist, max_dist) }
{
    GLFWwindow* window = Engine::get().window->window;
    projection= infinitePerspectiveFovReverseZRH_ZO(glm::radians(75.0f), 1280.0f, 768.0f, 0.1f);
    projection[1][1] *= -1.0f;

    double pos[2];
    glfwGetCursorPos(window, &pos[0], &pos[1]);
    lpx = (float)pos[0];
    lpy = (float)pos[1];
}

void Camera::update()
{
    GLFWwindow* window = Engine::get().window->window;
    const float dt = Engine::get().delta_time() * 5.0f;

    if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && glfwGetTime() - last_press_time > 0.3f)
    {
        last_press_time = (float)glfwGetTime();
        enabled = !enabled;
        if(!enabled) { glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
        else { glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
    }

    if(!enabled) { return; }

    glm::quat rot = glm::angleAxis(yaw, glm::vec3{ 0.0f, 1.0f, 0.0f });
    rot = rot * glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });

    glm::vec3 forward = glm::normalize(rot * glm::vec3{ 0.0f, 0.0f, -1.0f });
    glm::vec3 right = glm::normalize(rot * glm::vec3{ 1.0f, 0.0f, 0.0f });
    glm::vec3 up = glm::cross(right, forward);

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { pos += forward * dt * 2.0f; }
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { pos -= right * dt * 2.0f; }
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { pos -= forward * dt * 2.0f; }
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { pos += right * dt * 2.0f; }
    if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { pos -= up * dt * 2.0f; }
    if(glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { pos += up * dt * 2.0f; }

    prev_view = view;
    view = glm::lookAt(pos, pos + forward, up);
}

void Camera::on_mouse_move(float px, float py)
{
    if(enabled)
    {
        pitch += glm::radians((lpy - py)) * 0.5f;
        yaw += glm::radians((lpx - px)) * 0.5f;
        pitch = glm::clamp(pitch, -glm::half_pi<float>(), glm::half_pi<float>());
    }
    lpx = px;
    lpy = py;
}
