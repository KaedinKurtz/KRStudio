#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <QDebug> // For qDebug()
#include <string> // For std::string

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
    if (aspectRatio <= 0) {
        qWarning() << "Warning: Invalid aspect ratio in getProjectionMatrix:" << aspectRatio << ". Using 1.0 as fallback.";
        aspectRatio = 1.0f;
    }
    if (m_IsPerspective) {
        float fovRadians = glm::radians(45.0f);
        return glm::perspective(fovRadians, aspectRatio, 0.1f, 100.0f);
    }
    else {
        float ortho_size = m_Distance * 0.5f;
        return glm::ortho(-ortho_size * aspectRatio, ortho_size * aspectRatio, -ortho_size, ortho_size, -100.0f, 100.0f);
    }

    // --- THIS IS THE FIX ---
    // Add this fallback return statement to ensure the function always returns
    // something and silence the compiler warning. It returns an identity matrix.
    return glm::mat4(1.0f);
}

void Camera::logState(const std::string& contextMessage) const {
    qDebug().nospace() << "Camera State" << (contextMessage.empty() ? "" : " (" + QString::fromStdString(contextMessage) + ")") << ":"
        << "\n  Position: (" << m_Position.x << ", " << m_Position.y << ", " << m_Position.z << ")"
        << "\n  FocalPoint: (" << m_FocalPoint.x << ", " << m_FocalPoint.y << ", " << m_FocalPoint.z << ")"
        << "\n  Up: (" << m_Up.x << ", " << m_Up.y << ", " << m_Up.z << ")"
        << "\n  Distance: " << m_Distance
        << "\n  Yaw: " << m_Yaw << " (rad), Pitch: " << m_Pitch << " (rad)"
        << "\n  IsPerspective: " << m_IsPerspective;
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

void Camera::defaultInitialView() {
    m_Position = glm::vec3(0.0f, 0.0f, 5.0f); // Or your preferred default
    m_FocalPoint = glm::vec3(0.0f, 0.0f, 0.0f);
    m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
    m_Distance = 5.0f;
    m_Yaw = 0.0f;
    m_Pitch = 0.0f;
    m_IsPerspective = true;
    updateCameraVectors(); // This is important
    logState("Camera Reset to Default Initial View");
}

void Camera::resetView(float aspectRatio, const glm::vec3& target, float objectVisibleSize) {
    m_FocalPoint = target;
    m_IsPerspective = true; // Assuming perspective for this reset

    // Basic heuristic: adjust distance based on a fixed vertical FoV (e.g., 45 degrees)
    // to make an object of 'objectVisibleSize' roughly fill the height.
    // tan(verticalFoV / 2) = (objectVisibleSize / 2) / distance
    // distance = (objectVisibleSize / 2) / tan(verticalFoV_radians / 2)
    float verticalFoV_rad = glm::radians(45.0f);
    m_Distance = (objectVisibleSize * 0.5f) / tan(verticalFoV_rad * 0.5f);
    if (m_Distance < 1.0f) m_Distance = 1.0f; // Min distance

    // Reset orientation to look directly at target
    m_Position = m_FocalPoint + glm::vec3(0.0f, 0.0f, m_Distance); // Simple back-view
    m_Yaw = 0.0f;
    m_Pitch = 0.0f; // Look straight

    updateCameraVectors();
    logState("Camera View Reset/Reframed");
}

void Camera::setToKnownGoodView() {
    // Remove or comment out any lines that used aspectRatio
    // Q_UNUSED(aspectRatio);
    qDebug() << "Camera::setToKnownGoodView - FORCING SUPER SIMPLE VIEW";

    m_FocalPoint = glm::vec3(0.0f, 0.0f, 0.0f);
    m_IsPerspective = true;

    m_Distance = 7.0f;
    m_Yaw = 0.0f;
    m_Pitch = 0.0f;

    updateCameraVectors();
    logState("Camera after FORCED simple setToKnownGoodView");
}

void Camera::forceRecalculateView(glm::vec3 newPosition, glm::vec3 newTarget, float newDistance) {
    m_Position = newPosition;
    m_FocalPoint = newTarget;
    m_Distance = newDistance; // Or calculate from position and target
    // Recalculate Yaw/Pitch based on new Position and FocalPoint
    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);
    updateCameraVectors(); // This will recalculate m_Up and potentially refine m_Position based on yaw/pitch
    logState("Camera after forceRecalculateView");
}

