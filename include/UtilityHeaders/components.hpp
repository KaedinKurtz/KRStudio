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
#include <Eigen/Dense> 

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
    glm::vec3 offColor{ 0.1f, 0.0f, 0.0f };
    float speed = 0.50f;
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

struct FieldVisualizerComponent {
    // --- FIX: Define enums inside the component for clear scope ---
    enum class DisplayMode { Arrows, Particles, Flow };
    enum class ColoringMode { Intensity, Lifetime, Directional };

    // --- General Settings (apply to all modes) ---
    bool isEnabled = true;
    DisplayMode displayMode = DisplayMode::Arrows;
    AABB bounds = { 
        glm::vec3(-5.0f),
        glm::vec3(5.0f) 
    };

    // --- FIX: Use nested structs for organization ---
    struct ArrowSettings {
        glm::ivec3 density = { 10, 10, 10 };
        float vectorScale = 1.0f;
        float headScale = 0.5f;
        float intensityMultiplier = 1.0f;
        float cullingThreshold = 0.01f;
        bool scaleByLength = false;
        float lengthScaleMultiplier = 1.0f;
        bool scaleByThickness = false;
        float thicknessScaleMultiplier = 1.0f;
        ColoringMode coloringMode = ColoringMode::Intensity;
        std::vector<ColorStop>           intensityGradient;
        glm::vec4 xPosColor, xNegColor, yPosColor, yNegColor, zPosColor, zNegColor;
    } arrowSettings;

    struct FlowSettings {
        int particleCount = 5000;
        float lifetime = 7.0f;
        float baseSpeed = 0.15f;
        float speedIntensityMultiplier = 0.3f;
        float baseSize = 0.1f;
        float headScale = 0.5f;
        float peakSizeMultiplier = 2.0f;
        float minSize = 0.01f;
        float growthPercentage = 0.2f;
        float shrinkPercentage = 0.2f;
        float randomWalkStrength = 0.1f;
        bool scaleByLength = false;
        float lengthScaleMultiplier = 1.0f;
        bool scaleByThickness = false;
        float thicknessScaleMultiplier = 1.0f;
        ColoringMode coloringMode = ColoringMode::Intensity;
        std::vector<ColorStop>           intensityGradient;     // NEW
        std::vector<ColorStop>           lifetimeGradient;
    } flowSettings;

    struct ParticleSettings {
        bool isSolid = true;
        int particleCount = 10000;
        float lifetime = 4.0f;
        float baseSpeed = 1.0f;
        float speedIntensityMultiplier = 1.0f;
        float baseSize = 0.05f;
        float peakSizeMultiplier = 2.0f;
        float minSize = 0.01f;
        float baseGlowSize = 0.1f;
        float peakGlowMultiplier = 2.0f;
        float minGlowSize = 0.02f;
        float randomWalkStrength = 0.1f;
        ColoringMode coloringMode = ColoringMode::Intensity;
        glm::vec4 xPosColor, xNegColor, yPosColor, yNegColor, zPosColor, zNegColor;
        std::vector<ColorStop>           intensityGradient;     // NEW
        std::vector<ColorStop>           lifetimeGradient;
    } particleSettings;

    // --- Internal GPU State ---
    bool isGpuDataDirty = true;
    FieldVisGpuData gpuData;
    GLuint particleBuffer[2] = { 0, 0 };
    GLuint particleVAO[2] = { 0, 0 };
    int currentReadBuffer = 0;
};

struct DrawElementsIndirectCommand {
    GLuint count;          // The number of indices in the mesh to draw.
    GLuint instanceCount;  // The number of instances to draw. THIS is what the compute shader will set.
    GLuint firstIndex;     // The starting offset in the index buffer. Usually 0.
    GLuint baseVertex;     // A constant added to each index. Usually 0.
    GLuint baseInstance;   // The starting instance for instanced drawing. Usually 0.
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

inline bool isDescendantOf(const entt::registry& r,
    entt::entity child,
    entt::entity ancestor)
{
    while (r.any_of<ParentComponent>(child)) {
        child = r.get<ParentComponent>(child).parent;
        if (child == ancestor) return true;
    }
    return false;
}

// --- SCENE-WIDE & MISC COMPONENTS ---

struct SceneProperties
{
    bool fogEnabled = true;
    glm::vec4 backgroundColor = { 0.1f, 0.1f, 0.15f, 1.0f };
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
    float deltaTime = 0.016f;
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


// --- SLAM & SENSOR FUSION COMPONENTS ---

/**
 * @brief A tag to identify an entity as a SLAM Keyframe.
 * A Keyframe has a TransformComponent representing its optimized pose.
 */
struct KeyframeTag {};

/**
 * @brief Intrinsic parameters of the camera used for SLAM.
 * This is distinct from the rendering camera component.
 */
struct SlamCameraIntrinsics {
    double fx = 0.0, fy = 0.0;             // Focal length in pixels
    double cx = 0.0, cy = 0.0;             // Principal point in pixels
    std::vector<float> distortion_coeffs;  // Brown-Conrady, etc.
};

/**
 * @brief Calibration and noise model for the Inertial Measurement Unit.
 * Biases can be updated online by the backend optimizer.
 */
struct ImuCalibration {
    // Noise spectral density (continuous-time)
    float noise_gyro_density = 0.0;
    float noise_accel_density = 0.0;

    // Bias random walk (continuous-time)
    float walk_gyro = 0.0;
    float walk_accel = 0.0;

    // Current bias estimates (to be updated by the optimizer)
    glm::vec3 bias_gyro = { 0.0f, 0.0f, 0.0f };
    glm::vec3 bias_accel = { 0.0f, 0.0f, 0.0f };
};

/**
 * @brief Static extrinsic transformations between sensors on the robot rig.
 * Defines how sensors are mounted relative to the robot's base link.
 */
struct SensorExtrinsics {
    glm::mat4 T_base_imu = glm::mat4(1.0f);    // Transform from base to IMU
    glm::mat4 T_base_camera = glm::mat4(1.0f); // Transform from base to Camera
    glm::mat4 T_base_lidar = glm::mat4(1.0f);  // Transform from base to LiDAR
};

/**
 * @brief A single data point from the IMU sensor.
 */
struct ImuDataPoint {
    double timestamp = 0.0;
    glm::vec3 angular_velocity = { 0.0f, 0.0f, 0.0f };
    glm::vec3 linear_acceleration = { 0.0f, 0.0f, 0.0f };
};

/**
 * @brief A buffer of IMU measurements captured between two consecutive Keyframes.
 */
struct ImuBuffer {
    std::vector<ImuDataPoint> measurements;
};

/**
 * @brief The raw 3D point cloud from a LiDAR scan for a given Keyframe.
 * Points are stored in the LiDAR's local sensor frame.
 */
struct LidarPointCloud {
    std::vector<glm::vec3> points;
};

/**
 * @brief Holds extracted visual keypoints and descriptors from a Keyframe's image.
 */
struct VisualFeatureData {
    std::vector<glm::vec2> keypoints; // Keypoint locations in pixels

    // Eigen is ideal for descriptor matrix operations, even if poses use GLM.
    Eigen::MatrixXf descriptors;
};

/**
 * @brief A compact representation of a Keyframe's point cloud for fast loop closure matching.
 */
struct LidarDescriptor {
    Eigen::MatrixXf descriptor;
};

/**
 * @brief Associates a Keyframe entity with its corresponding vertex ID in a backend graph solver.
 */
struct GraphNodeID {
    uint64_t id = 0;
};

/**
 * @brief Defines the physical material properties of a collider.
 */
struct PhysicsMaterial {
    // --- Core Rigid Body Dynamics ---

    /** @brief Friction coefficient for objects at rest. */
    float staticFriction = 0.6f;

    /** @brief Friction coefficient for objects in motion. */
    float dynamicFriction = 0.6f;

    /** @brief Bounciness of the material upon collision (0=inelastic, 1=perfectly elastic). */
    float restitution = 0.1f;

    /** @brief Density of the material in kg/m^3. Used with a collider's volume to calculate mass. */
    float density = 1000.0f;

    // --- Structural Properties for Strain & Uncertainty ---

    /**
     * @brief The material's stiffness (Modulus of Elasticity) in Pascals (Pa).
     * This directly relates stress (force per area) to strain (deformation).
     * Higher values mean the material is stiffer and deforms less under load.
     */
    float youngsModulus = 70e9; // Default to a value similar to aluminum

    /**
     * @brief The maximum stress in Pascals (Pa) a material can withstand before
     * deforming permanently. Defines the elastic limit.
     */
    float yieldStrength = 275e6; // Default to a value similar to aluminum
};

/**
 * @brief The core component for physics simulation. An entity with this component is
 * considered a dynamic or kinematic object. Its state is updated by the physics system.
 */
struct RigidBodyComponent {
    enum class BodyType { Static, Kinematic, Dynamic };

    BodyType bodyType = BodyType::Dynamic;

    // --- Core Properties ---
    float mass = 1.0f;
    // The inverse mass is pre-calculated for performance. Static bodies have an inverse mass of 0.
    float inverseMass = 1.0f;

    // The inertia tensor defines resistance to rotational change.
    glm::mat3 inertiaTensor = glm::mat3(1.0f);
    glm::mat3 inverseInertiaTensor = glm::mat3(1.0f);

    // --- State Variables (updated by physics engine) ---
    glm::vec3 linearVelocity = { 0.0f, 0.0f, 0.0f };
    glm::vec3 angularVelocity = { 0.0f, 0.0f, 0.0f };

    // --- Force & Torque Accumulators (cleared each frame) ---
    glm::vec3 forceAccumulator = { 0.0f, 0.0f, 0.0f };
    glm::vec3 torqueAccumulator = { 0.0f, 0.0f, 0.0f };

    // --- Damping ---
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
};

/**
 * @brief A box-shaped collider.
 */
struct BoxCollider {
    glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f };
    glm::vec3 offset = { 0.0f, 0.0f, 0.0f }; // Local offset from the entity's transform.
    bool isTrigger = false;
    PhysicsMaterial material;
};

/**
 * @brief A sphere-shaped collider.
 */
struct SphereCollider {
    float radius = 0.5f;
    glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
    bool isTrigger = false;
    PhysicsMaterial material;
};

/**
 * @brief A capsule-shaped collider, useful for characters.
 */
struct CapsuleCollider {
    float radius = 0.5f;
    float height = 1.0f;
    glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
    bool isTrigger = false;
    PhysicsMaterial material;
};

/**
 * @brief A collider generated from a mesh, for complex static environments (e.g., level geometry).
 * Not typically used for dynamic objects due to performance.
 */
struct TriangleMeshCollider {
    // Typically you would store a handle/ID to the mesh data
    // to avoid duplicating geometry in memory.
    uint64_t meshDataId = 0;
    bool isTrigger = false;
    PhysicsMaterial material;
};

/**
 * @brief A request to generate a motion plan for a robot or mechanism.
 * This is a "command" component; a system will see it, generate a plan,
 * and replace it with a Trajectory component.
 */
struct MotionPlanRequest {
    // The target can be defined in joint space or cartesian space
    std::vector<double> goalJointState;
    Pose6D goalWorldPose; // Using the Pose6D alias from before

    std::string endEffectorName; // Name of the link to move to the goal pose
    std::string plannerAlgorithm = "RRTConnect"; // e.g., RRT, A*, etc.
};

/**
 * @brief Represents a time-parameterized trajectory for execution.
 * This is the *output* of a motion planner.
 */
struct Trajectory {
    struct Waypoint {
        double timestamp;
        std::vector<double> positions;
        std::vector<double> velocities;
        std::vector<double> accelerations;
    };
    std::vector<Waypoint> waypoints;
    double totalDuration;
};

/**
 * @brief Defines the operational limits for a single joint.
 * Essential for any realistic motion planning or control.
 */
struct JointLimits {
    double minPosition = 0.0;
    double maxPosition = 0.0;
    double maxVelocity = 0.0;
    double maxEffort = 0.0; // Max torque or force
};

/**
 * @brief Defines the parameters for a PID (Proportional-Integral-Derivative) controller.
 * Attach this to each joint you want to control.
 */
struct PIDController {
    double p_gain = 1.0;
    double i_gain = 0.0;
    double d_gain = 0.0;

    // Internal state for the controller
    double integral_term = 0.0;
    double previous_error = 0.0;
};





/**
 * @brief A handle to a loaded machine learning model for inference.
 * This allows you to abstract away the specific ML framework (ONNX, PyTorch, etc.).
 */
struct InferenceModel {
    uint64_t model_id = 0; // An ID mapping to a model loaded in a central manager
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
};

/**
 * @brief A general-purpose key-value store for state sharing.
 * Perfect for AI, behavior trees, and scripts to communicate without direct dependencies.
 */
struct Blackboard {
    std::unordered_map<std::string, entt::any> table;
};

/**
 * @brief A high-level goal for a task planner (like a Behavior Tree or FSM) to act upon.
 * e.g., "Go to the charging station" or "Pick up the red cube."
 */
struct Goal {
    std::string name;
    entt::entity targetEntity = entt::null;
    glm::vec3 targetPosition;
    float priority = 1.0f;
};
/**
 * @brief Defines an entity as a Reinforcement Learning agent.
 * This component holds the agent's state within a learning episode.
 */
struct RLAgent {
    uint64_t policy_model_id = 0; // ID for the InferenceModel this agent uses
    uint32_t step_count = 0;
    float cumulative_reward = 0.0f;
    bool is_ready_for_action = true;
};

/**
 * @brief Defines the rules and boundaries of the learning environment.
 * Attach this to a scene or arena entity.
 */
struct RLEnvironment {
    // A function pointer or script function that calculates the reward.
    // It takes the registry and agent entity to inspect the state.
    std::function<float(const entt::registry&, entt::entity)> reward_function;

    // A function that determines if the episode has ended (e.g., goal reached, time limit).
    std::function<bool(const entt::registry&, entt::entity)> termination_function;
};

/**
 * @brief Defines the shape and bounds of the agent's possible actions.
 * Modeled after common RL library conventions like OpenAI Gym/Gymnasium.
 */
struct ActionSpace {
    enum class SpaceType { Discrete, Continuous };
    SpaceType type;
    int64_t discrete_actions = 0; // Number of actions for a Discrete space
    std::vector<float> continuous_low_bounds;
    std::vector<float> continuous_high_bounds;
};

/**
 * @brief Defines the shape and bounds of the agent's sensory input.
 */
struct ObservationSpace {
    enum class SpaceType { Vector, Image };
    SpaceType type;
    std::vector<int64_t> shape; // e.g., {84, 84, 3} for an image
};

/**
 * @brief A singleton component that holds global configuration for a training session.
 */
struct TrainingManager {
    bool is_training_mode = true;
    uint64_t global_step_count = 0;

    // Key hyperparameters for the learning algorithm (e.g., PPO, SAC).
    float learning_rate = 3e-4;
    float discount_factor = 0.99f; // Gamma
    uint32_t batch_size = 256;
    uint32_t epochs_per_update = 10;
};

/**
 * @brief A single "experience" tuple, the fundamental unit of data in RL.
 */
struct Experience {
    std::vector<float> observation;
    std::vector<float> action;
    float reward;
    std::vector<float> next_observation;
    bool done; // True if the episode terminated after this experience
};

/**
 * @brief A buffer to store past experiences for off-policy learning.
 * This component would live on the TrainingManager entity.
 */
struct ReplayBuffer {
    // A deque is efficient for adding to the front and removing from the back.
    std::deque<Experience> memory;
    size_t capacity = 100000;
};
/**
 * @brief When attached to an agent, this component records its state-action pairs.
 * Used to create datasets for Imitation Learning (Behavioral Cloning).
 */
struct DemonstrationRecorder {
    std::string output_path;
    uint32_t samples_recorded = 0;
};

/**
 * @brief A component to enable Hierarchical Reinforcement Learning (HRL).
 * The high-level policy selects sub-goals, and a low-level policy executes them.
 */
struct HierarchicalPolicy {
    uint64_t high_level_policy_id; // Selects a sub-goal
    uint64_t low_level_policy_id;  // Executes the sub-goal

    // The current sub-goal selected by the high-level policy.
    Goal current_sub_goal;
    uint32_t sub_goal_step_count = 0;
    uint32_t sub_goal_frequency = 10; // High-level policy acts every 10 steps.
};

/*
 
-----------------Machine Learning and Reinforcement Learning Workflow-----------------

You attach an RLAgent and ActionSpace to your robot.

An RLEnvironment component on the scene defines the reward.

In a TrainingManager-controlled loop:

The agent's sensors generate an observation.

The InferenceModel (its policy) computes an action.

The action is performed.

The RLEnvironment calculates a reward.

The (observation, action, reward, next_observation) tuple is stored in the ReplayBuffer.

Periodically, the TrainingManager samples a batch from the ReplayBuffer and performs a gradient update on the policy's neural network.
*/


/**
 * @brief Attaches a script to an entity for custom logic.
 */
struct ScriptComponent {
    std::string script_path; // Path to a Lua, Python, etc. file
    bool is_active = true;
};

/**
 * @brief Holds the runtime state for an entity's script.
 * A ScriptingSystem would manage creating and running the VM.
 */
struct ScriptVM {
    // Using entt::any to hold a pointer to any VM state (e.g., lua_State*)
    entt::any vm_state;
};





/**
 * @brief Represents the strain and stress state of a physically simulated object.
 * Useful for structural analysis, damage modeling, or estimating sensor uncertainty.
 */
struct StructuralState {
    // The current stress tensor (force per area) acting on the object.
    glm::mat3 stressTensor = glm::mat3(0.0f);

    // The current strain (deformation) of the object.
    glm::mat3 strainTensor = glm::mat3(0.0f);
};

/**
 * @brief Defines the configuration state of a robot or articulated body.
 * A single source of truth for all joint positions and velocities.
 */
struct ArticulatedBodyState {
    std::vector<double> jointPositions;
    std::vector<double> jointVelocities;
};











/**
 * @brief Holds semantic information for a sensor view. Each pixel/point is
 * labeled with a class ID (e.g., 0=road, 1=building, 2=person).
 */
struct SemanticData {
    // A 1D array of class IDs corresponding to a 2D image or a 3D point cloud.
    std::vector<uint8_t> class_ids;
    uint32_t width, height; // For 2D semantic images
};

/**
 * @brief Represents a dynamically tracked object in the scene.
 * This component is persistent across frames for a given object.
 */
struct TrackedObject {
    uint64_t track_id;
    std::string semantic_label; // e.g., "person", "vehicle", "tool"
    float confidence = 1.0f;

    // State for a Kalman Filter or similar to predict future positions.
    Eigen::VectorXd filter_state;
    Eigen::MatrixXd filter_covariance;
};

/**
 * @brief Defines an entity's role in a hierarchical scene graph.
 * This creates relationships like "the cup is ON the table" and "the table is IN the kitchen."
 */
struct SceneGraphNode {
    enum class Relationship { Contains, IsOnTopOf, IsAttachedTo, InteractsWith };
    entt::entity parent_node = entt::null;
    std::unordered_map<Relationship, std::vector<entt::entity>> children_nodes;
};



//---------------------------State Space Components---------------------------

/**
 * @brief Represents a linear, time-invariant state-space model of the system.
 * This is a linearized model of the robot's dynamics around a specific operating point
 * (e.g., a particular joint configuration). Model is of the form:
 * xdot = Ax + Bu
 * y = Cx + Du
 */
struct StateSpaceModel {
    Eigen::MatrixXd A; // The state matrix (how the state evolves on its own).
    Eigen::MatrixXd B; // The input matrix (how control inputs affect the state).
    Eigen::MatrixXd C; // The output matrix (how the state translates to measurements).
    Eigen::MatrixXd D; // The feed-forward matrix (how inputs directly affect output).
};

/**
 * @brief Represents the state vector (x) of a system at a point in time.
 * For a robot arm, this would typically be a concatenation of all joint
 * positions and velocities [q_1, ..., q_n, q_1, ..., q_n].
 */
struct StateVector {
    Eigen::VectorXd x;
};

/**
 * @brief Defines the cost function for a Linear Quadratic Regulator (LQR).
 * The LQR finds a control law that minimizes a cost J = int(xQx + uRu)dt.
 */
struct CostFunctionLQR {
    /**
     * @brief The state cost matrix (Q). A positive semi-definite matrix that penalizes
     * deviations from the desired state. Larger diagonal values mean the controller
     * will try harder to keep that specific state variable near zero.
     */
    Eigen::MatrixXd Q;

    /**
     * @brief The control cost matrix (R). A positive definite matrix that penalizes
     * control effort. Larger values mean the controller will be more energy-efficient,
     * using smaller forces or torques.
     */
    Eigen::MatrixXd R;
};

/**
 * @brief Stores the result of an LQR calculation: the optimal state-feedback gain matrix (K).
 * The optimal control law is a simple matrix multiplication: u = -Kx.
 */
struct OptimalControlLaw {
    Eigen::MatrixXd gainMatrixK;
};

/**
 * @brief Stores a time-varying policy for non-linear trajectory optimization (e.g., from iLQR).
 * This policy contains both a feed-forward control sequence and a time-varying feedback gain.
 */
struct FeedbackPolicy {
    // A sequence of feed-forward control inputs along the nominal trajectory.
    std::vector<Eigen::VectorXd> feedForwardControls_u;

    // A sequence of time-varying feedback gain matrices (K) along the trajectory.
    std::vector<Eigen::MatrixXd> feedbackGains_K;
};

//---------------------------Drone Components---------------------------

/**
 * @brief Defines the physical configuration of a multirotor drone.
 * This is the primary component identifying an entity as a drone.
 */
struct MultirotorConfig {
    struct Motor {
        glm::vec3 position;       // Position relative to the drone's center of mass.
        glm::vec3 spin_axis;      // Usually {0, 0, 1} or {0, 0, -1}.
        bool is_clockwise;        // The propeller's spin direction.
    };
    std::vector<Motor> motors;
};

/**
 * @brief Models the aerodynamic properties of a single motor and propeller combination.
 * This is crucial for translating control inputs into physical forces.
 */
struct MotorModel {
    // Coefficients to convert motor speed (rad/s) to thrust (N) and torque (Nm).
    // Often modeled as: thrust = k_thrust * speed^2
    float thrust_coefficient = 1.5e-5;
    float torque_coefficient = 3.0e-7;

    // How quickly the motor can spool up or down (first-order time constant).
    float time_constant_s = 0.025;

    float max_speed_rad_s = 1000.0;
};

/**
 * @brief Holds the state and gains for the drone's attitude controller.
 * Typically a set of cascaded PID controllers for stabilizing roll, pitch, and yaw.
 */
struct AttitudeController {
    // Separate PID controllers for each axis of angular velocity.
    PIDController roll_rate_pid;
    PIDController pitch_rate_pid;
    PIDController yaw_rate_pid;

    // PID controllers for stabilizing angle (outer loop).
    PIDController roll_angle_pid;
    PIDController pitch_angle_pid;
};

/**
 * @brief Contains the target state for the low-level flight controller.
 * This component is written to by high-level planners and read by the AttitudeController.
 */
struct FlightCommand {
    glm::quat desired_attitude;
    float desired_thrust = 0.0f; // Normalized from 0 to 1
};

/**
 * @brief Holds the current state of each individual motor.
 * This is a dynamic component updated every frame by the flight controller.
 */
struct MotorState {
    // The target speed for each motor, normalized from 0 to 1.
    std::vector<float> motor_commands;
    // The actual current speed of each motor in rad/s.
    std::vector<float> motor_speeds_rad_s;
};

/**
 * @brief Holds data from a simulated GPS receiver.
 */
struct GpsReceiver {
    glm::dvec3 position; // Latitude, Longitude, Altitude
    float fix_quality = 0.0f; // 0 = None, 3 = High-quality fix
    int num_satellites = 0;
};

/**
 * @brief Holds data from a barometer for accurate altitude estimation.
 */
struct Barometer {
    float pressure_pascals = 101325.0f;
    float temperature_celsius = 15.0f;
};

/**
 * @brief Holds data from a downward-facing optical flow sensor for GPS-denied position hold.
 */
struct OpticalFlowSensor {
    glm::vec2 integrated_flow; // Total accumulated pixel movement
    float ground_distance = 0.0f;
    float quality = 0.0f;
};

/**
 * @brief Defines a mission as a sequence of waypoints for the drone to follow.
 */
struct WaypointMission {
    struct Waypoint {
        glm::vec3 position;
        float heading_rad;
        float loiter_time_s = 0.0f; // Time to wait at the waypoint
    };

    

    std::vector<Waypoint> waypoints;
    int current_waypoint_index = 0;
};

/**
 * @brief Manages the state and control of a camera gimbal.
 */
struct GimbalController {
    enum class Mode { Lock, Follow, PointAt };
    Mode mode = Mode::Follow;

    entt::entity target_entity = entt::null;
    glm::vec3 target_point;

    // The gimbal's current orientation relative to the drone body.
    glm::quat current_orientation;
};

//---------------------------Process and State Management Components---------------------------

/**
 * @brief A data asset that defines the template for a multi-step process.
 * It contains all possible steps and the transitions between them.
 */
struct ProcessBlueprint {
    std::string name;
    // A map where the key is a unique name for a step and the value is the
    // entity that holds the template for that step's details.
    std::unordered_map<std::string, entt::entity> step_templates;
};

/**
 * @brief The runtime state of an active process instance.
 * This is attached to an entity that represents a single execution of a process.
 */
struct ProcessState {
    enum class Status { Inactive, Running, Paused, Completed, Failed };

    entt::entity blueprint_entity = entt::null;
    Status status = Status::Inactive;
    std::string current_step_name;

    // A blackboard specific to this process instance for sharing data between steps.
    Blackboard process_data;
};

/**
 * @brief Defines the logic and conditions for a single step in a process.
 */
struct ProcessStep {
    std::string description;

    // A function that must return true for this step to begin.
    // Checks things like "Is the gripper empty?" or "Is the robot at the home position?"
    std::function<bool(const entt::registry&, entt::entity)> preconditions;

    // A function that returns true when this step is successfully completed.
    // Checks things like "Has the joint reached its target?" or "Is the sensor reading positive?"
    std::function<bool(const entt::registry&, entt::entity)> completion_criteria;

    // A list of resources this step needs exclusive access to.
    std::vector<std::string> required_resource_tags;
};

/**
 * @brief Explicitly defines the graph of dependencies between process steps.
 * Allows for branching (if-else) and parallel execution paths.
 */
struct ProcessGraph {
    // An adjacency list. For each step (key), it lists the possible next steps (value).vvvvvvvvvvvvv
    std::unordered_map<std::string, std::vector<std::string>> transitions;
};

/**
 * @brief A simple tag to identify an entity as a physical resource (e.g., an arm, a tool).
 */
struct ResourceHandle {
    std::string tag; // e.g., "left_arm", "gripper", "welder"
};

/**
 * @brief A component placed on a resource entity to signify it's currently in use.
 * Prevents other processes from acquiring the same resource.
 */
struct ResourceLock {
    entt::entity owner_process; // The process instance that holds the lock.
};




//---------------------------Safety and Navigation Components---------------------------


/**
 * @brief Defines a spatial zone with specific rules for robot behavior.
 */
struct SafetyZone {
    enum class ZoneType { KeepOut, ReducedSpeed, SharedWorkspace, AudibleAlert };
    ZoneType type;
    // The zone's geometry can be defined by one of the collider components.
};

/**
 * @brief Allows a robot to project textures or information onto the environment.
 * Used to communicate intent, like its planned path or a warning.
 */
struct ProjectedDisplay {
    uint64_t texture_id; // ID of the texture to project
    glm::mat4 projection_matrix; // Defines the projector's pose and field of view
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

/**
 * @brief Describes a robot's "personal space" and how it should react to intrusion.
 */
struct Proxemics {
    float intimate_space_radius = 0.5f; // e.g., stop immediately
    float personal_space_radius = 1.5f; // e.g., slow down and yield
    float social_space_radius = 4.0f;   // e.g., acknowledge and track
};



/**
 * @brief A layer of data overlaid on a map representing traversal cost.
 * You can have multiple layers for terrain roughness, restricted zones, etc.
 */
struct CostmapLayer {
    std::string name;
    float resolution; // meters per cell
    uint32_t width, height;
    glm::vec3 origin; // The world position of the bottom-left corner of the map
    std::vector<float> costs; // 1D array of cost values
};

/**
 * @brief A handle to a navigation mesh (NavMesh) asset.
 * NavMeshes are efficient representations of walkable/drivable space for pathfinding.
 */
struct NavMesh {
    uint64_t nav_mesh_id;
};

/**
 * @brief Defines a node in a topological map (a graph of places).
 * Useful for high-level, human-like navigation like "go from the kitchen to the lab."
 */
struct TopologicalNode {
    std::string name; // "Kitchen", "Hallway_1", "Lab_Door"
    std::vector<entt::entity> connected_nodes;
};





/**
 * @brief Models the state of a battery.
 */
struct Battery {
    float current_charge_Wh = 100.0f; // Watt-hours
    float max_capacity_Wh = 100.0f;
    float health_percentage = 1.0f;   // Capacity degrades over time
    float current_draw_W = 0.0f;      // Instantaneous power draw in Watts
};

/**
 * @brief Defines the power consumption of a specific hardware component.
 * A PowerManagementSystem would sum these up to get the total draw.
 */
struct PowerConsumer {
    std::string name;
    float idle_draw_W = 1.0f;
    float active_draw_W = 10.0f;
    bool is_active = false;
};




//--------------------------Digital Twin Components--------------------------

/**
 * @brief The core component identifying an entity as a digital twin.
 * It provides a unique, persistent link to a specific physical asset.
 */
struct DigitalTwin {
    enum class SyncStatus { Disconnected, Live, Replay };

    // A universally unique identifier (UUID) for the physical asset.
    uint64_t asset_uuid;
    SyncStatus status = SyncStatus::Disconnected;
};

/**
 * @brief Subscribes an entity's state to a real-world data stream (e.g., ROS, MQTT).
 * A "SyncSystem" would use this info to pull data and update components.
 */
struct StateStreamSubscriber {
    std::string source_topic; // e.g., "/robot_A/joint_states"
    double last_sync_timestamp = 0.0;
};

/**
 * @brief Publishes an entity's state or commands to the real world.
 * Used to send motion commands or corrected states back to the physical robot.
 */
struct StateStreamPublisher {
    std::string destination_topic; // e.g., "/robot_A/trajectory_command"
};

/**
 * @brief Specifies what data on this entity should be logged to a time-series database.
 */
struct DataLogger {
    // List of component type IDs to log (e.g., entt::type_hash<TransformComponent>::value()).
    std::vector<entt::id_type> components_to_log;
    float logging_frequency_hz = 10.0f;
};

/**
 * @brief Defines operational thresholds for monitoring the physical asset's health.
 * A "HealthSystem" would compare incoming data against these limits.
 */
struct HealthMonitor {
    float max_motor_temperature_C = 85.0f;
    float max_motor_current_A = 20.0f;
    float max_vibration_g = 2.0f; // Max g-force from accelerometers
    bool needs_maintenance = false;
};

/**
 * @brief Gives an entity its own time stream, separate from the main engine clock.
 * Crucial for running predictive simulations faster than real-time.
 */
struct SimulationClock {
    double current_time = 0.0;
    double time_scale = 1.0; // 1.0 = real-time, 10.0 = 10x speed, 0.0 = paused
};

/**
 * @brief Actively compares the simulated state with the last received real-world state.
 * This is vital for measuring how accurately your simulation models reality.
 */
struct DeviationTracker {
    float position_error_m = 0.0f;
    float orientation_error_rad = 0.0f;
    std::vector<double> joint_error_rad;
};