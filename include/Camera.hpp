#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f));
    glm::mat4 getViewMatrix();
    glm::mat4 getProjectionMatrix(float aspectRatio);
    void processMouseMovement(double xoffset, double yoffset, bool isPanning);
    void processMouseScroll(double yoffset);
    void toggleProjection();
private:
    void updateCameraVectors();
    glm::vec3 m_Position;
    glm::vec3 m_FocalPoint;
    glm::vec3 m_Up;
    float m_Distance;
    float m_Yaw;
    float m_Pitch;
    bool m_IsPerspective;
};