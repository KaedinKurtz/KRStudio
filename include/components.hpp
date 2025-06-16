#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <entt/entt.hpp>

#include "GridLevel.hpp"
#include "Camera.hpp"
#include "RobotDescription.hpp"

// --- CORE COMPONENTS ---

struct SelectedComponent {};

struct BoundingBoxComponent {
    glm::vec3 min = { -0.5f, -0.5f, -0.5f };
    glm::vec3 max = { 0.5f,  0.5f,  0.5f };
};

struct TransformComponent
{
    glm::vec3 translation = { 0.0f, 0.0f, 0.0f };
    glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    glm::mat4 getTransform() const {
        glm::mat4 rot = glm::mat4_cast(rotation);
        return glm::translate(glm::mat4(1.0f), translation) * rot * glm::scale(glm::mat4(1.0f), scale);
    }
};

struct TagComponent
{
    std::string tag;
    TagComponent(const std::string& t = "") : tag(t) {}
};

struct CameraComponent
{
    Camera camera;
    bool isPrimary = true;
};

// --- RENDER-RELATED COMPONENTS ---

struct RenderableMeshComponent
{
    glm::vec4 color = { 0.8f, 0.8f, 0.8f, 1.0f };
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
};

struct RenderResourceComponent
{
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
};

// --- ROBOTICS-SPECIFIC COMPONENTS ---

struct LinkComponent {
    LinkDescription description;
};

struct JointComponent
{
    JointDescription description;
    entt::entity parentLink = entt::null;
    entt::entity childLink = entt::null;
    double currentPosition = 0.0;
};

struct ParentComponent {
    entt::entity parent = entt::null;
};

struct RobotRootComponent {
    std::string name;
};

struct MaterialComponent
{
    MaterialDescription description;
};

// --- SCENE-WIDE & MISC COMPONENTS ---

struct SceneProperties
{
    bool fogEnabled = true;
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
};

// REFACTOR: This struct now contains all of its original members.
struct GridComponent
{
    bool masterVisible = true;
    bool levelVisible[5] = { true, true, true, true, true };
    std::vector<GridLevel> levels;
    float baseLineWidthPixels = 1.5f;
    bool showAxes = true;
    bool isMetric = true;
    bool showIntersections = true;
    bool isDotted = false;
    bool snappingEnabled = false;
    glm::vec3 xAxisColor = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxisColor = { 0.2f, 0.2f, 1.0f };
    glm::vec3 xAxis2Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis2Color = { 0.2f, 0.2f, 1.0f };
    glm::vec3 xAxis3Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis3Color = { 0.2f, 0.2f, 1.0f };
    glm::vec3 xAxis4Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis4Color = { 0.2f, 0.2f, 1.0f };
    glm::vec3 xAxis5Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis5Color = { 0.2f, 0.2f, 1.0f };
    glm::vec3 origin = { 1.0f, 0.2f, 0.2f };
    glm::vec3 eulerOrient = { 1.0f, 0.2f, 0.2f };
    glm::vec4 quaternionOrient = { 1.0f, 0.2f, 0.2f, 0.3f };
    float axisLineWidthPixels = 1.4f;
};