#pragma once // Prevents the file from being included multiple times

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

class Camera {
public:
    // Constructor
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f));

    // Getters for the matrices needed for rendering
    glm::mat4 getViewMatrix();
    glm::mat4 getProjectionMatrix(float aspectRatio);

    // Methods to process input
    void processKeyboard(GLFWwindow* window, float deltaTime);
    void processMouseMovement(double xoffset, double yoffset);
    void processMouseScroll(double yoffset);
    void processMouseButton(int button, int action, int mods, GLFWwindow* window);

    // Method to toggle projection
    void toggleProjection();

private:
    // Camera state
    glm::vec3 m_Position;
    glm::vec3 m_FocalPoint;
    glm::vec3 m_Up;

    float m_Distance;
    float m_Yaw;
    float m_Pitch;

    bool m_IsPerspective;

    // Mouse state
    bool m_IsOrbiting;
    bool m_IsPanning;
    double m_LastMouseX;
    double m_LastMouseY;

    // Recalculates position based on spherical coordinates
    void updateCameraVectors();
};