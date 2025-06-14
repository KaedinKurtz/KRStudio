#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include "Grid.hpp"
#include "Camera.hpp"
#include "IntersectionSystem.hpp"
#include "RobotDescription.hpp" 

struct SceneProperties
{
    bool fogEnabled = true;
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
};

struct TransformComponent
{
    glm::vec3 translation = { 0.0f, 0.0f, 0.0f };
    glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent(const glm::vec3& t = glm::vec3(0.0f), const glm::quat& r = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
        : translation(t), rotation(r) {
    }

    glm::mat4 getTransform() const {
        glm::mat4 rot = glm::mat4_cast(rotation);
        return glm::translate(glm::mat4(1.0f), translation) * rot * glm::scale(glm::mat4(1.0f), scale);
    }
};

struct GridComponent
{
    bool masterVisible = true;              // Master switch for the whole grid
    bool levelVisible[5] = { true, true, true, true, true }; // Per-level toggles

    std::vector<GridLevel> levels;
    float baseLineWidthPixels = 1.5f;

    bool showAxes = true;
    bool isMetric = true;

    bool showIntersections = true;

    bool isDotted = false;       // For switching between lines and dots
    bool snappingEnabled = false; // For the grid snap toggle

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

struct RenderableMeshComponent
{
    glm::vec4 color = { 0.8f, 0.8f, 0.8f, 1.0f };
    bool placeholder = true;

    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;

    const std::vector<glm::vec3>& getVertices() const { return vertices; }
    const std::vector<unsigned int>& getIndices() const { return indices; }
};

struct TagComponent
{
    std::string tag;
    TagComponent() = default;
    TagComponent(const TagComponent&) = default;
    TagComponent(const std::string& t) : tag(t) {}
};

struct CameraComponent
{
    Camera camera;
    bool isPrimary = true;
};

struct IntersectionComponent {
    IntersectionSystem::IntersectionResult result;
};

struct MaterialComponent
{
    MaterialDescription description;

    MaterialComponent() = default;
    MaterialComponent(const MaterialComponent&) = default;
    MaterialComponent(const MaterialDescription& desc) : description(desc) {}
};

struct JointComponent
{
    // It contains the rich description data...
    JointDescription description;

    // ...and also holds the entity IDs for the links it connects.
    entt::entity parentLink = entt::null;
    entt::entity childLink = entt::null;

    // Current state of the joint
    double currentPosition = 0.0;
    double currentVelocity = 0.0;

    JointComponent() = default;
    JointComponent(const JointComponent&) = default;
    JointComponent(const JointDescription& desc) : description(desc) {}
};

struct LinkComponent {
    LinkDescription description;

    LinkComponent(const LinkDescription& desc = LinkDescription()) : description(desc) {}
};

// NEW COMPONENT
struct ParentComponent {
    entt::entity parent = entt::null;
    ParentComponent(entt::entity p = entt::null) : parent(p) {}
};

struct RobotRootComponent {
    std::string name;
};
