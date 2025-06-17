#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <QtGui/QOpenGLContext>
#include <QtGui/qopengl.h> 
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <unordered_map>

#include "GridLevel.hpp"
#include "Camera.hpp"
#include "RobotDescription.hpp"

// --- CORE COMPONENTS ---

struct SelectedComponent {};
struct CameraGizmoTag {};
struct RecordLedTag {};

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

struct WorldTransformComponent {
    glm::mat4 matrix{ 1.0f };
};

struct Vertex
{
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{ 0.0f };

    Vertex() = default;

    Vertex(const glm::vec3& p,
        const glm::vec3& n,
        const glm::vec2& t = glm::vec2(0.0f))
        : position(p), normal(n), uv(t) {
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

struct RenderableMeshComponent {
    glm::vec4 colour{ 0.8f,0.8f,0.8f,1.0f };
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
};

struct RenderResourceComponent
{
    struct Buffers         // one trio of GL objects *per context*
    {
        GLuint VAO = 0;
        GLuint VBO = 0;
        GLuint EBO = 0;
    };

    // key = context pointer, value = the trio above
    std::unordered_map<QOpenGLContext*, Buffers> perContext;
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