#pragma once

#include <glm/glm.hpp>
#include <string>

class Camera {
public:
    // Enum for camera movement directions
    enum Camera_Movement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };

    enum class NavMode { ORBIT, FLY };
    void  setNavMode(NavMode m) { m_Mode = m; }
    NavMode navMode()       const { return m_Mode; }

    // fly-camera helpers
    void  freeLook(float dxPix, float dyPix);
    void  flyMove(Camera_Movement dir, float dt);

    // refined pan (screen-scaled)
    void  pan(float dxPix, float dyPix, float vpW, float vpH);

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f));

    // --- Core Functions ---
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // --- NEW: Camera Control Functions ---
    void orbit(float xoffset, float yoffset);
    void pan(float xoffset, float yoffset);
    void dolly(float yoffset);
    void move(Camera_Movement direction, float deltaTime);
    void focusOn(const glm::vec3& target, float distance = 5.0f);

    // --- Utility and State Functions ---
    void toggleProjection();
    void logState(const std::string& contextMessage = "") const;
    void resetView(float aspectRatio, const glm::vec3& target = glm::vec3(0.0f), float objectSize = 1.0f);
    void defaultInitialView();
    void forceRecalculateView(glm::vec3 newPosition, glm::vec3 newTarget, float newDistance);
    void setToKnownGoodView();

    // --- Getters ---
    glm::vec3 getPosition() const { return m_Position; }
    glm::vec3 getFocalPoint() const { return m_FocalPoint; }
    float getDistance() const { return m_Distance; }
    bool isPerspective() const { return m_IsPerspective; }

private:

    NavMode m_Mode = NavMode::ORBIT;
    // EMA for smoothing
    glm::vec2 m_lastDelta{ 0 };
    float      m_smoothAlpha = 0.25f; // 0 = instant, 1 = very smooth

    void updateCameraVectors();

    // Camera Attributes
    glm::vec3 m_Position;
    glm::vec3 m_FocalPoint;
    glm::vec3 m_Up;
    glm::vec3 m_WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 m_Right;

    // Euler Angles
    float m_Yaw;
    float m_Pitch;

    // Camera options
    float m_Distance;
    bool m_IsPerspective;
};
