#include "Camera.hpp" // Include our own header
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Camera::Camera(glm::vec3 position)
    : m_Position(position), m_FocalPoint(0.0f, 0.0f, 0.0f), m_Up(0.0f, 1.0f, 0.0f),
    m_Distance(5.0f), m_Yaw(0.0f), m_Pitch(0.0f),
    m_IsPerspective(true), m_IsOrbiting(false), m_IsPanning(false),
    m_LastMouseX(0.0), m_LastMouseY(0.0)
{
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() {
    return glm::lookAt(m_Position, m_FocalPoint, m_Up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) {
    if (m_IsPerspective) {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    }
    else {
        float ortho_size = m_Distance * 0.5f;
        return glm::ortho(-ortho_size * aspectRatio, ortho_size * aspectRatio, -ortho_size, ortho_size, -100.0f, 100.0f);
    }
}

void Camera::processMouseMovement(double xoffset, double yoffset) {
    if (m_IsOrbiting) {
        m_Yaw += xoffset * 0.005;
        m_Pitch -= yoffset * 0.005;
        if (m_Pitch > glm::radians(89.0f)) m_Pitch = glm::radians(89.0f);
        if (m_Pitch < glm::radians(-89.0f)) m_Pitch = glm::radians(-89.0f);
    }
    else if (m_IsPanning) {
        glm::vec3 right = glm::normalize(glm::cross(m_FocalPoint - m_Position, m_Up));
        glm::vec3 up = glm::normalize(glm::cross(right, m_FocalPoint - m_Position));
        float panSpeed = 0.002f * m_Distance;
        m_FocalPoint -= right * (float)xoffset * panSpeed;
        m_FocalPoint += up * (float)yoffset * panSpeed;
    }
    updateCameraVectors();
}

void Camera::processMouseScroll(double yoffset) {
    m_Distance -= yoffset * 0.5f;
    if (m_Distance < 1.0f) m_Distance = 1.0f;
    updateCameraVectors();
}

void Camera::processMouseButton(int button, int action, int mods, GLFWwindow* window) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        glfwGetCursorPos(window, &m_LastMouseX, &m_LastMouseY);
        if (mods & GLFW_MOD_SHIFT) {
            m_IsPanning = true;
        }
        else {
            m_IsOrbiting = true;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE) {
        m_IsOrbiting = false;
        m_IsPanning = false;
    }
}

void Camera::toggleProjection() {
    m_IsPerspective = !m_IsPerspective;
    std::cout << "Switched to " << (m_IsPerspective ? "Perspective" : "Orthographic") << " projection." << std::endl;
}

void Camera::updateCameraVectors() {
    // Recalculate Cartesian position from spherical coordinates
    m_Position.x = m_FocalPoint.x + m_Distance * cos(m_Pitch) * sin(m_Yaw);
    m_Position.y = m_FocalPoint.y + m_Distance * sin(m_Pitch);
    m_Position.z = m_FocalPoint.z + m_Distance * cos(m_Pitch) * cos(m_Yaw);
}