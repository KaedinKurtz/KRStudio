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
struct PulsingSplineTag {};

// --- FIELD-SPECIFIC COMPONENTS ---

struct FieldSourceTag {};

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
    // --- General Properties ---
    FieldVisMode mode = FieldVisMode::Vector;
    bool isEnabled = true;

    // --- Bounding Volume ---
    // The size, position, and orientation of the visualization volume
    // are controlled by this component AND the entity's TransformComponent.
    glm::vec3 bounds = { 10.0f, 10.0f, 10.0f };

    // --- Voxel Grid ---
    // The number of samples to take along each axis of the bounding volume.
    // This directly controls the density of the vectors/indicators.
    glm::ivec3 density = { 10, 10, 10 };

    // --- Data Mapping & Appearance ---
    // Used to map a field's magnitude to a color.
    std::vector<ColorStop> colorGradient;
    float minMagnitude = 0.0f;
    float maxMagnitude = 1.0f;

    // A global scale factor for the length of the visualized vectors.
    float vectorScale = 1.0f;

    // --- Data Sources ---
    // An optional list of specific entities that act as sources for this field.
    // If this vector is empty, the visualizer will consider ALL field sources in the scene.
    std::vector<entt::entity> sourceEntities;

	// The state of the particles used for visualization.
    ParticleState particles;
};

struct PointEffectorComponent {
    float strength = 1.0f; // Positive for repulsion, negative for attraction
    float radius = 10.0f;  // How far the influence extends
    // How the strength decreases with distance (e.g., linear, inverse-square)
    enum class FalloffType { None, Linear, InverseSquare };
    FalloffType falloff = FalloffType::Linear;
};

struct SplineEffectorComponent {
    float strength = -1.0f; // Negative for attraction
    float radius = 5.0f;   // How far from the spline the influence is felt
    // Define the direction of the force
    enum class ForceDirection { Perpendicular, Tangent };
    ForceDirection direction = ForceDirection::Perpendicular; // Pulls towards the nearest point
};

struct MeshEffectorComponent {
    float strength = 10.0f; // Positive for repulsion
    float distance = 2.0f; // How far out from the surface the repulsion is felt
    // Direction is almost always the surface normal (outward)
};

struct DirectionalEffectorComponent {
    glm::vec3 direction = { 0.0f, -1.0f, 0.0f };
    float strength = 1.0f;
};

// --- ...-SPECIFIC COMPONENTS ---

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

/* ── spline data --------------------------------------------------------- */
enum class SplineType { Linear, CatmullRom, Bezier, Parametric };

// This struct remains for the parametric function case
struct ParametricData { std::function<glm::vec3(float)> func; };

// A more unified component for all spline types
struct SplineComponent
{
    SplineType type = SplineType::Linear;

    // A single vector for all control-point based splines (Linear, Catmull-Rom, Bezier)
    std::vector<glm::vec3> controlPoints;

    // A separate data holder for parametric splines
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