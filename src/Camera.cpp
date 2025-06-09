// Camera.cpp
#include "Camera.hpp" // Include the header file that DECLARES the Camera class

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <QDebug> // If you're using Qt's qDebug for logging

// NO "class Camera { ... }" RE-DECLARATION HERE

// Constructor Definition
Camera::Camera(glm::vec3 position)
    : m_Position(position),
    m_FocalPoint(0.0f, 0.0f, 0.0f),
    m_Up(0.0f, 1.0f, 0.0f),
    m_Distance(glm::length(position - m_FocalPoint)),
    m_Yaw(0.0f),
    m_Pitch(0.0f),
    m_IsPerspective(true)
{
    if (m_Distance < 0.01f && m_Distance > -0.01f) {
        if (glm::length(m_Position - m_FocalPoint) < 0.01f) {
            m_Position = m_FocalPoint - glm::vec3(0.0f, 0.0f, 5.0f);
        }
        m_Distance = glm::length(m_Position - m_FocalPoint);
    }
    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);
    updateCameraVectors();
    logState("Camera Initialized");
}

// Method Definitions
glm::mat4 Camera::getViewMatrix() const { // Definition with const
    return glm::lookAt(m_Position, m_FocalPoint, m_Up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const { // Definition with const
    if (aspectRatio <= 0.0f) {
        qWarning() << "Camera::getProjectionMatrix: Invalid aspect ratio" << aspectRatio << ".Using 1.0.";
        aspectRatio = 1.0f;
    }
    if (m_IsPerspective) {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.001f, 1000.0f);
    }
    else {
        float ortho_size = m_Distance * 0.5f;
        return glm::ortho(-ortho_size * aspectRatio, ortho_size * aspectRatio,
            -ortho_size, ortho_size,
            -100.0f, 100.0f);
    }
}

void Camera::processMouseMovement(double xoffset, double yoffset, bool isPanning) {
    if (isPanning) {
        glm::vec3 front = glm::normalize(m_FocalPoint - m_Position);
        glm::vec3 right = glm::normalize(glm::cross(front, m_Up));
        glm::vec3 localUp = glm::normalize(glm::cross(right, front));

        float panSpeedFactor = m_Distance * 0.002f;

        m_FocalPoint -= right * static_cast<float>(xoffset) * panSpeedFactor;
        // OLD: m_FocalPoint += localUp * static_cast<float>(yoffset) * panSpeedFactor;
        // NEW: Invert yoffset for panning
        m_FocalPoint -= localUp * static_cast<float>(yoffset) * panSpeedFactor; // <--- Notice the minus sign
    }
    else { // Orbiting
        float sensitivity = 0.002f; // THIS IS THE SENSITIVITY FOR ORBITING
        m_Yaw += static_cast<float>(xoffset) * sensitivity;
        m_Pitch -= static_cast<float>(yoffset) * sensitivity; // Negative because screen Y is often inverted

        // Clamp pitch
        float maxPitch = glm::radians(89.0f);
        if (m_Pitch > maxPitch) m_Pitch = maxPitch;
        if (m_Pitch < -maxPitch) m_Pitch = -maxPitch;
    }
    updateCameraVectors();
}

void Camera::processMouseScroll(double yoffset) {
    // FIX: Use proportional (percentage-based) zooming.
    // This feels natural at any distance because it modifies the distance
    // by a percentage rather than a fixed amount. 0.95 is a good starting
    // point, make it closer to 1.0 for slower zoom, further for faster.
    m_Distance *= pow(0.95f, static_cast<float>(yoffset));

    // Clamp the distance to prevent zooming through the near plane or too far away.
    if (m_Distance < 0.01f) {
        m_Distance = 0.01f;
    }
    if (m_Distance > 1000.0f) {
        m_Distance = 1000.0f;
    }
    updateCameraVectors();
}

void Camera::toggleProjection() {
    // ... your implementation ...
    m_IsPerspective = !m_IsPerspective;
    qDebug() << "Camera projection switched to:" << (m_IsPerspective ? "Perspective" : "Orthographic");
    logState("After projection toggle");
}

void Camera::logState(const std::string& contextMessage) const {
    // ... your implementation ...
    qDebug().nospace() << "Camera State" << (contextMessage.empty() ? "" : " (" + QString::fromStdString(contextMessage) + ")") << ":"
        << "\n  Position: (" << m_Position.x << ", " << m_Position.y << ", " << m_Position.z << ")"
        << "\n  FocalPoint: (" << m_FocalPoint.x << ", " << m_FocalPoint.y << ", " << m_FocalPoint.z << ")"
        << "\n  Up: (" << m_Up.x << ", " << m_Up.y << ", " << m_Up.z << ")"
        << "\n  Distance: " << m_Distance
        << "\n  Yaw (deg): " << glm::degrees(m_Yaw) << ", Pitch (deg): " << glm::degrees(m_Pitch)
        << "\n  IsPerspective: " << m_IsPerspective;
}

void Camera::resetView(float aspectRatio, const glm::vec3& target, float objectVisibleSize) {
    // ... your implementation ...
    Q_UNUSED(aspectRatio);
    m_FocalPoint = target;
    m_IsPerspective = true;
    float verticalFoV_rad = glm::radians(45.0f);
    m_Distance = (objectVisibleSize * 0.5f) / tan(verticalFoV_rad * 0.5f);
    if (m_Distance < 0.1f * objectVisibleSize) m_Distance = 0.1f * objectVisibleSize;
    if (m_Distance < 1.0f) m_Distance = 1.0f;
    m_Position = m_FocalPoint - glm::vec3(0.0f, -0.5f * m_Distance, m_Distance);
    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);
    updateCameraVectors();
    logState("Camera View Reset/Reframed");
}

void Camera::defaultInitialView() {
    // ... your implementation ...
    m_Position = glm::vec3(0.0f, 1.5f, 7.0f);
    m_FocalPoint = glm::vec3(0.0f, 1.0f, 0.0f);
    m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
    m_IsPerspective = true;
    m_Distance = glm::length(m_Position - m_FocalPoint);
    if (m_Distance < 0.01f) m_Distance = 5.0f;
    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);
    updateCameraVectors();
    logState("Camera Reset to Default Initial View");
}

void Camera::forceRecalculateView(glm::vec3 newPosition, glm::vec3 newTarget, float newDistance) {
    // ... your implementation ...
    m_Position = newPosition;
    m_FocalPoint = newTarget;
    if (newDistance > 0.0f) {
        m_Distance = newDistance;
    }
    else {
        m_Distance = glm::length(m_Position - m_FocalPoint);
        if (m_Distance < 0.01f) m_Distance = 5.0f;
    }
    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);
    updateCameraVectors();
    logState("Camera after forceRecalculateView");
}

void Camera::setToKnownGoodView() {
    // ... your implementation ...
    qDebug() << "Camera::setToKnownGoodView - Setting to a predefined stable view.";
    m_FocalPoint = glm::vec3(0.0f, 0.5f, 0.0f);
    m_IsPerspective = true;
    m_Distance = 7.0f;
    m_Yaw = glm::radians(25.0f);
    m_Pitch = glm::radians(15.0f);
    updateCameraVectors();
    logState("Camera after setToKnownGoodView");
}

void Camera::updateCameraVectors() {
    // ... your implementation ...
    m_Position.x = m_FocalPoint.x + m_Distance * cos(m_Pitch) * sin(m_Yaw);
    m_Position.y = m_FocalPoint.y + m_Distance * sin(m_Pitch);
    m_Position.z = m_FocalPoint.z + m_Distance * cos(m_Pitch) * cos(m_Yaw);
}

// Define other non-inline methods here...