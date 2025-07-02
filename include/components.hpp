#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <functional>
#include <entt/entt.hpp>
#include <QOpenGLContext>
#include <qopengl.h>
#include <unordered_map>

#include "GridLevel.hpp"
#include "Camera.hpp"
#include "RobotDescription.hpp"
#include "GpuResources.hpp" // <-- ADD THIS INCLUDE to get the GPU struct definitions

// --- CORE COMPONENTS ---

struct SelectedComponent {};
struct CameraGizmoTag {};
struct RecordLedTag {};
struct PulsingSplineTag {};

// --- Material & Texture Components ---

struct PulsingLightComponent {
    glm::vec3 onColor{ 1.0f, 0.0f, 0.0f };
    glm::vec3 offColor{ 0.2f, 0.0f, 0.0f };
    float speed = 5.0f;
};

struct Texture {
    GLuint id = 0;
    std::string path;
};

struct MaterialComponent {
    glm::vec3 albedo = { 0.8f, 0.8f, 0.8f };
    std::shared_ptr<Texture> albedoMap;
    float metallic = 0.1f;
    float roughness = 0.8f;
};

// --- FIELD-SPECIFIC COMPONENTS ---

struct FieldSourceTag {};

enum class FieldColoringMode { Magnitude, Polarity };
enum class FieldVisMode { Vector, Potential, Flow };

struct ColorStop {
    float position = 0.0f;
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct Particle {
    glm::vec4 position;
    glm::vec4 velocity;
    glm::vec4 color;
    float age;
    float lifetime;
    float size;
    float padding; // Ensures struct alignment matches std430 layout
};

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

struct InstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 color;
};


struct FieldVisualizerComponent {
    bool isEnabled = true;

    enum class DisplayMode { Arrows, Particles, Flow };
    DisplayMode displayMode = DisplayMode::Arrows;

    // --- NEW: Control the initial shape of the particle spawn ---
    enum class FlowSpawnDistribution { RandomBox, RandomSphere };
    FlowSpawnDistribution flowSpawnDistribution = FlowSpawnDistribution::RandomBox;

    // --- Arrow Mode Settings ---
    glm::ivec3 density = { 10, 10, 10 };
    float vectorScale = 1.0f;
    float arrowHeadScale = 0.50f;
    float cullingThreshold = 0.01f;

    // --- Particle Mode Settings ---
    int particleCount = 20000;
    float particleSize = 1.5f;
    glm::vec4 particleColor = { 1.0f, 0.8f, 0.4f, 0.7f };
    float particleLifetime = 4.0f;

    // --- Flow Mode Settings ---
    float flowLifetime = 5.0f;
    float flowBaseSpeed = 0.5f;
    float flowVelocityMultiplier = 0.25f;
    float flowScale = 0.15f;
    float flowFadeInTime = 0.2f;
    float flowFadeOutTime = 0.2f;
    glm::vec3 flowColorStart = { 1.0f, 1.0f, 0.0f };
    glm::vec3 flowColorMid = { 1.0f, 0.0f, 0.0f };
    glm::vec3 flowColorEnd = { 0.5f, 0.0f, 0.5f };
    float flowPeakScaleMultiplier = 2.0f;
    float flowRandomWalk = 0.15f;

    AABB bounds = { glm::vec3(-10.0f), glm::vec3(10.0f) };

    // ... Internal State ...
    bool isGpuDataDirty = true;
    FieldVisGpuData gpuData;
    GLuint particleBuffer[2] = { 0, 0 };
    GLuint particleVAO = 0;
    int currentReadBuffer = 0;
};
// --- Effector Components (Updated for GPU alignment) ---

struct PointEffectorComponent {
    enum class FalloffType { None, Linear, InverseSquare };

    float strength = 1.0f;
    float radius = 10.0f;
    FalloffType falloff = FalloffType::Linear;
};

struct SplineEffectorComponent {
    enum class ForceDirection { Perpendicular, Tangent };

    float strength = -1.0f;
    float radius = 5.0f;
    ForceDirection direction = ForceDirection::Perpendicular;
};

struct MeshEffectorComponent {
    float strength = 10.0f;
    float distance = 2.0f;
};

struct DirectionalEffectorComponent {
    glm::vec3 direction = { 0.0f, -1.0f, 0.0f };
    float strength = 1.0f;
};


// --- GEOMETRY & TRANSFORM COMPONENTS ---

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
    Vertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& t = glm::vec2(0.0f))
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
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
};

struct RenderResourceComponent
{
    struct Buffers {
        GLuint VAO = 0;
        GLuint VBO = 0;
        GLuint EBO = 0;
    };
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

// --- SCENE-WIDE & MISC COMPONENTS ---

struct SceneProperties
{
    bool fogEnabled = true;
    glm::vec4 backgroundColor = { 0.1f, 0.1f, 0.15f, 1.0f };
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
};

enum class SplineType { Linear, CatmullRom, Bezier, Parametric };

struct ParametricSpline { std::function<glm::vec3(float)> func; };

struct SplineComponent
{
    SplineType type = SplineType::Linear;
    std::vector<glm::vec3> controlPoints;
    ParametricSpline parametric;
    glm::vec4 glowColour{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 coreColour{ 1.0f, 1.0f, 1.0f, 1.0f };
    float thickness = 8.0f;

    bool isDirty = true;
    std::vector<glm::vec3> cachedVertices;
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
    float axisLineWidthPixels = 1.4f;
};
