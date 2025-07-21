// Camera.cpp
#include "Camera.hpp" // Include the header file that DECLARES the Camera class

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <QDebug> // If you're using Qt's qDebug for logging
#include <algorithm> // For std::clamp

// NO "class Camera { ... }" RE-DECLARATION HERE

// Constructor Definition
Camera::Camera(glm::vec3 position)
    : m_Position(position),
    m_FocalPoint(0.0f, 0.0f, 0.0f),
    m_Up(0.0f, 1.0f, 0.0f),
    m_IsPerspective(true)
{
    m_Distance = glm::length(m_Position - m_FocalPoint);
    if (m_Distance < 0.01f) {
        m_Position = m_FocalPoint - glm::vec3(0.0f, 0.0f, 5.0f);
        m_Distance = 5.0f;
    }

    glm::vec3 direction = glm::normalize(m_FocalPoint - m_Position);
    m_Pitch = asin(direction.y);
    m_Yaw = atan2(direction.x, direction.z);

    updateCameraVectors();
    logState("Camera Initialized");
}

// Method Definitions
glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_Position, m_FocalPoint, m_Up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    if (aspectRatio <= 0.0f) {
        aspectRatio = 1.0f;
    }
    if (m_IsPerspective) {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.001f, 1000.0f);
    }
    else {
        float ortho_size = m_Distance * 0.5f;
        return glm::ortho(-ortho_size * aspectRatio, ortho_size * aspectRatio,
            -ortho_size, ortho_size,
            -1000.0f, 1000.0f);
    }
}

void Camera::orbit(float xoffset, float yoffset) {
    float k = 0.01f;                                    // tune to taste
    float sensitivity = k * std::min(1.0f, std::sqrt(m_Distance));
	// Adjust yaw and pitch based on mouse movement
    m_Yaw += xoffset * sensitivity;
    m_Pitch += yoffset * sensitivity;

    // Clamp pitch to avoid flipping
    m_Pitch = std::clamp(m_Pitch, -glm::pi<float>() / 2.0f + 0.01f, glm::pi<float>() / 2.0f - 0.01f);
    updateCameraVectors();
}

void Camera::pan(float dx, float dy) { pan(dx, dy, 1280, 720); }

void Camera::dolly(float wheelDelta)
{
    float step = std::pow(0.95f, wheelDelta / 120.0f);   // 120 = one wheel click
    m_Distance *= step;
    m_Distance = std::clamp(m_Distance, 0.05f, 5000.0f);
    updateCameraVectors();
}

void Camera::move(Camera_Movement direction, float deltaTime) {
    float velocity = 2.5f * deltaTime;
    if (direction == FORWARD) m_Position += (m_FocalPoint - m_Position) * velocity;
    if (direction == BACKWARD) m_Position -= (m_FocalPoint - m_Position) * velocity;
    if (direction == LEFT) m_Position -= m_Right * velocity;
    if (direction == RIGHT) m_Position += m_Right * velocity;
    if (direction == UP) m_Position += m_Up * velocity;
    if (direction == DOWN) m_Position -= m_Up * velocity;

    // If we move the camera, the focal point should move with it
    m_FocalPoint = m_Position + glm::normalize(m_FocalPoint - m_Position) * m_Distance;
}

void Camera::focusOn(const glm::vec3& target, float distance) {
    m_FocalPoint = target;
    m_Distance = distance;
    updateCameraVectors();
}


void Camera::toggleProjection() {
    // ... your implementation ...
    m_IsPerspective = !m_IsPerspective;
    ////qDebug() << "Camera projection switched to:" << (m_IsPerspective ? "Perspective" : "Orthographic");
    logState("After projection toggle");
}

void Camera::logState(const std::string& contextMessage) const {
    // ... your implementation ...
    //qDebug().nospace() << "Camera State" << (contextMessage.empty() ? "" : " (" + QString::fromStdString(contextMessage) + ")") << ":"
     //   << "\n  Position: (" << m_Position.x << ", " << m_Position.y << ", " << m_Position.z << ")"
    //    << "\n  FocalPoint: (" << m_FocalPoint.x << ", " << m_FocalPoint.y << ", " << m_FocalPoint.z << ")"
      //  << "\n  Up: (" << m_Up.x << ", " << m_Up.y << ", " << m_Up.z << ")"
     //   << "\n  Distance: " << m_Distance
      //  << "\n  Yaw (deg): " << glm::degrees(m_Yaw) << ", Pitch (deg): " << glm::degrees(m_Pitch)
     //   << "\n  IsPerspective: " << m_IsPerspective;
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
    ////qDebug() << "Camera::setToKnownGoodView - Setting to a predefined stable view.";
    m_FocalPoint = glm::vec3(0.0f, 0.5f, 0.0f);
    m_IsPerspective = true;
    m_Distance = 7.0f;
    m_Yaw = glm::radians(25.0f);
    m_Pitch = glm::radians(15.0f);
    updateCameraVectors();
    logState("Camera after setToKnownGoodView");
}

void Camera::updateCameraVectors() {
    // Calculate the new front vector
    glm::vec3 front;
    front.x = cos(m_Yaw) * cos(m_Pitch);
    front.y = sin(m_Pitch);
    front.z = sin(m_Yaw) * cos(m_Pitch);

    // Update camera position based on focal point, distance, and orientation
    m_Position = m_FocalPoint - (glm::normalize(front) * m_Distance);

    // Re-calculate the Right and Up vector
    m_Right = glm::normalize(glm::cross(front, m_WorldUp));
    m_Up = glm::normalize(glm::cross(m_Right, front));
}

void Camera::pan(float dxPix, float dyPix, float vpW, float vpH)
{
    if (vpW <= 0.0f || vpH <= 0.0f) return;

    float unitsPerPixel;
    if (m_IsPerspective) {
        float vFov = glm::radians(45.0f);           // same you use in getProjectionMatrix
        unitsPerPixel = 2.0f * m_Distance * tan(vFov * 0.5f) / vpH;
    }
    else {
        float orthoHalfHeight = 0.5f * m_Distance;
        unitsPerPixel = 2.0f * orthoHalfHeight / vpH;
    }

    m_FocalPoint += (-m_Right * dxPix + m_Up * dyPix) * unitsPerPixel;
    updateCameraVectors();
}

void Camera::freeLook(float dxPix, float dyPix)
{
    // exponential moving average for silky motion
    glm::vec2 raw(dxPix, dyPix);
    m_lastDelta = m_smoothAlpha * raw + (1.0f - m_smoothAlpha) * m_lastDelta;

    float sensitivity = 0.0025f;
    m_Yaw += m_lastDelta.x * sensitivity;
    m_Pitch += m_lastDelta.y * sensitivity;
    m_Pitch = std::clamp(m_Pitch,
        -glm::half_pi<float>() + 0.01f,
        +glm::half_pi<float>() - 0.01f);
    updateCameraVectors();
}

void Camera::flyMove(Camera_Movement dir, float dt)
{
    float v = 1.0f * dt;             // constant speed (tweak or expose)
    glm::vec3 fwd = glm::normalize(m_FocalPoint - m_Position);

    if (dir == FORWARD)  m_Position += fwd * v;
    if (dir == BACKWARD) m_Position -= fwd * v;
    if (dir == LEFT)     m_Position -= m_Right * v;
    if (dir == RIGHT)    m_Position += m_Right * v;
    if (dir == UP)       m_Position += m_Up * v;
    if (dir == DOWN)     m_Position -= m_Up * v;

    m_FocalPoint = m_Position + fwd * m_Distance;
}