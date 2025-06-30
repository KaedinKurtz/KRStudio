#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <functional> // Required for std::function in ParametricData
#include <entt/entt.hpp>
#include <QOpenGLContext>
#include <qopengl.h>
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
struct PulsingSplineTag {};

// --- Material & Texture Components ---

// Represents a single texture map (e.g., diffuse, normal, metallic).
struct Texture {
    GLuint id = 0;      // The OpenGL texture ID.
    std::string path;   // The file path it was loaded from.
};

// A component that defines the surface properties of a mesh.
// This replaces simple color properties for more advanced rendering.
struct MaterialComponent {
    glm::vec3 albedo = { 0.8f, 0.8f, 0.8f }; // The base color of the material.
    std::shared_ptr<Texture> albedoMap;      // A texture for the base color (optional).

    float metallic = 0.1f;                   // How metallic the surface is.
    float roughness = 0.8f;                  // How rough the surface is.
    // You can add more maps here later (normalMap, metallicMap, etc.)
};


// --- FIELD-SPECIFIC COMPONENTS ---

struct FieldSourceTag {};

enum class FieldColoringMode {
    Magnitude, // Color is based on the vector's length (good for wind, flow).
    Polarity   // Color is based on scalar potential (good for attraction/repulsion).
};

struct PotentialSourceComponent {
    float strength = 10.0f;
};

struct VectorSourceComponent {
    glm::vec3 direction = { 1.0f, 0.5f, 0.0f };
    float strength = 1.0f;
};

struct ColorStop {
    float position = 0.0f; // Position in the gradient (0.0 to 1.0)
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

enum class FieldVisMode { Vector, Potential, Flow };

struct ParticleState {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;
    std::vector<glm::vec4> colors;
    std::vector<float> ages;
    std::vector<float> lifetimes;
    int particleCount = 2000; // The number of particles to simulate
};

struct FieldVisualizerComponent {
    FieldVisMode mode = FieldVisMode::Vector;
    bool isEnabled = true;

    glm::vec3 bounds = { 20.0f, 10.0f, 20.0f };
    glm::ivec3 density = { 20, 10, 20 };

    // --- Data Mapping & Appearance ---
    FieldColoringMode coloringMode = FieldColoringMode::Magnitude; // The new mode selector.
    std::vector<ColorStop> colorGradient; // Can now be used for magnitude or polarity.

    // Used for Magnitude mode
    float minMagnitude = 0.0f;
    float maxMagnitude = 2.0f;

    // Used for Polarity mode
    float minPotential = -1.0f;
    float maxPotential = 1.0f;

    float vectorScale = 1.0f;
    float arrowHeadScale = 0.50f;
    float cullingThreshold = 0.01f;

    std::vector<entt::entity> sourceEntities;
    ParticleState particles;
};

struct PointEffectorComponent {
    float strength = 1.0f; // Positive for repulsion, negative for attraction
    float radius = 10.0f;  // How far the influence extends
    enum class FalloffType { None, Linear, InverseSquare };
    FalloffType falloff = FalloffType::Linear;
};

struct SplineEffectorComponent {
    float strength = -1.0f; // Negative for attraction
    float radius = 5.0f;   // How far from the spline the influence is felt
    enum class ForceDirection { Perpendicular, Tangent };
    ForceDirection direction = ForceDirection::Perpendicular; // Pulls towards the nearest point
};

struct MeshEffectorComponent {
    float strength = 10.0f; // Positive for repulsion
    float distance = 2.0f; // How far out from the surface the repulsion is felt
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
    // NOTE: The `glm::vec4 colour` member has been removed.
    // Color is now handled by the MaterialComponent's 'albedo' property.
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

// FIX: Removed the duplicate definition of MaterialComponent that was here.
// The primary definition is now at the top of the file.

// --- SCENE-WIDE & MISC COMPONENTS ---

struct SceneProperties
{
    bool fogEnabled = true;
    // New: Added a background color property for the scene.
    glm::vec4 backgroundColor = { 0.1f, 0.1f, 0.15f, 1.0f };
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
};

/* ── spline data --------------------------------------------------------- */
enum class SplineType { Linear, CatmullRom, Bezier, Parametric };

struct ParametricData { std::function<glm::vec3(float)> func; };

struct SplineComponent
{
    SplineType type = SplineType::Linear;
    std::vector<glm::vec3> controlPoints;
    ParametricData parametric;
    glm::vec4 glowColour{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 coreColour{ 1.0f, 1.0f, 1.0f, 1.0f };
    float thickness = 8.0f;
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