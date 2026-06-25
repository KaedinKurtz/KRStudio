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
    glm::mat4 getReversedZProjection(float aspectRatio) const;

    // --- NEW: Camera Control Functions ---
    void orbit(float xoffset, float yoffset);
    void pan(float xoffset, float yoffset);
    void dolly(float wheelDelta);
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
    glm::vec3 getPosition()    const { return m_Position; }
    glm::vec3 getFocalPoint()  const { return m_FocalPoint; }
    float     getDistance()    const { return m_Distance; }
    bool      isPerspective()  const { return m_IsPerspective; }

    // --- Global camera preferences (Settings; shared by all viewports) ---
    static void  setFovDeg(float v)           { s_fovDeg = v; }
    static float fovDeg()                      { return s_fovDeg; }
    static void  setNearClip(float v)          { s_zNear = v; }
    static void  setFarClip(float v)           { s_zFar = v; }
    static void  setOrbitSensitivity(float v)  { s_orbitSens = v; }
    static void  setZoomFactor(float v)        { s_zoomFactor = v; }
    static void  setLookSmoothing(float v)     { s_smoothAlpha = v; }
    static void  setInvertLookY(bool v)        { s_invertY = v; }
    static void  setDefaultNavMode(NavMode m)  { s_defaultNavMode = m; }

private:
    void updateCameraVectors();

    // Navigation state
    NavMode m_Mode = s_defaultNavMode;   // seeded from the global default (Settings)

    // EMA for smoothing freeLook
    glm::vec2 m_lastDelta{ 0.0f, 0.0f };

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
    float m_Distance = 5.0f;
    bool  m_IsPerspective = true;   // also set by every ctor path; default is belt-and-suspenders

    // Global camera preferences (defined in Camera.cpp; set via Settings).
    static float   s_fovDeg;      // vertical FOV (deg)
    static float   s_zNear;       // near clip
    static float   s_zFar;        // far clip
    static float   s_orbitSens;   // orbit sensitivity
    static float   s_zoomFactor;  // dolly per-click factor
    static float   s_smoothAlpha; // freeLook EMA smoothing
    static bool    s_invertY;     // invert look/orbit Y
    static NavMode s_defaultNavMode;
};
