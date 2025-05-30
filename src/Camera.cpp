#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Camera::Camera(glm::vec3 position)
    : m_Position(position), m_FocalPoint(0.0f, 0.0f, 0.0f), m_Up(0.0f, 1.0f, 0.0f),
    m_Distance(5.0f), m_Yaw(0.0f), m_Pitch(0.0f), m_IsPerspective(true)
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

void Camera::processMouseMovement(double xoffset, double yoffset, bool isPanning) {
    if (isPanning) {
        glm::vec3 right = glm::normalize(glm::cross(m_FocalPoint - m_Position, m_Up));
        glm::vec3 up = glm::normalize(glm::cross(right, m_FocalPoint - m_Position));
        float panSpeed = 0.002f * m_Distance;
        m_FocalPoint -= right * static_cast<float>(xoffset) * panSpeed;
        m_FocalPoint += up * static_cast<float>(yoffset) * panSpeed;
    }
    else { // Orbiting
        m_Yaw += xoffset * 0.005;
        m_Pitch -= yoffset * 0.005;
        if (m_Pitch > glm::radians(89.0f)) m_Pitch = glm::radians(89.0f);
        if (m_Pitch < glm::radians(-89.0f)) m_Pitch = glm::radians(-89.0f);
    }
    updateCameraVectors();
}

void Camera::processMouseScroll(double yoffset) {
    m_Distance -= static_cast<float>(yoffset) * 0.5f;
    if (m_Distance < 1.0f) m_Distance = 1.0f;
    updateCameraVectors();
}

void Camera::toggleProjection() {
    m_IsPerspective = !m_IsPerspective;
    std::cout << "Switched to " << (m_IsPerspective ? "Perspective" : "Orthographic") << " projection." << std::endl;
}

void Camera::updateCameraVectors() {
    m_Position.x = m_FocalPoint.x + m_Distance * cos(m_Pitch) * sin(m_Yaw);
    m_Position.y = m_FocalPoint.y + m_Distance * sin(m_Pitch);
    m_Position.z = m_FocalPoint.z + m_Distance * cos(m_Pitch) * cos(m_Yaw);
}