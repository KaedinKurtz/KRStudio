// Camera.hpp
#pragma once // Correct: For header files

#include <glm/glm.hpp>
#include <string>

class Camera {
public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f));

    // Declarations of methods to be defined in Camera.cpp
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
    void processMouseMovement(double xoffset, double yoffset, bool isPanning);
    void processMouseScroll(double yoffset);
    void toggleProjection();
    void logState(const std::string& contextMessage = "") const;
    void resetView(float aspectRatio, const glm::vec3& target = glm::vec3(0.0f), float objectSize = 1.0f);
    void defaultInitialView();
    void forceRecalculateView(glm::vec3 newPosition, glm::vec3 newTarget, float newDistance);
    void setToKnownGoodView();

    // Inline getter definitions (this is fine in a header)
    glm::vec3 getPosition() const { return m_Position; }
    glm::vec3 getFocalPoint() const { return m_FocalPoint; }
    float getDistance() const { return m_Distance; }
    bool isPerspective() const { return m_IsPerspective; }

private:
    void updateCameraVectors(); // Declaration

    glm::vec3 m_Position;
    glm::vec3 m_FocalPoint;
    glm::vec3 m_Up;
    float m_Distance;
    float m_Yaw;
    float m_Pitch;
    bool m_IsPerspective;
};