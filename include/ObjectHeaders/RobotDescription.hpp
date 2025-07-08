// In RobotDescription.hpp
#pragma once

#include <string>
#include <vector>
#include <variant> // For holding different sensor types
#include <cstdint> // For specific integer types
#include <glm/glm.hpp>

struct Vec3 {
    float x = 0.0f; // Initialize to 0
    float y = 0.0f; // Initialize to 0
    float z = 0.0f; // Initialize to 0
};

struct MaterialDescription {
    // The base color of the material (RGBA). This is also called the Albedo.
    glm::vec4 albedo_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f); // A sensible default grey.

    // A file path to the main color texture. If empty, the albedo_color is used.
    std::string albedo_texture_path;

    // How "metallic" the surface is. 0.0 for non-metals, 1.0 for raw metals.
    float metalness = 0.0f;

    // How rough or smooth the surface is. 0.0 is a perfect mirror, 1.0 is completely diffuse (like chalk).
    float roughness = 0.5f;

    // Optional: File path for a texture that controls metalness and roughness in different areas.
    std::string metal_roughness_texture_path;

    // Emissive properties for things that glow.
    glm::vec3 emissive_color = glm::vec3(0.0f); // Default to black (not glowing).
    float emissive_intensity = 1.0f;
};

// A helper struct for things like sensor mounts
struct NamedTransform {
    std::string name;
    glm::vec3 position;
    glm::vec3 rpy; // roll, pitch, yaw
};

struct LinkDescription {
    // --- Core Identification ---
    std::string name; // The human-readable name of the link.
    uint64_t persistent_id; // A unique, permanent ID for this link.

    // --- Visual Properties ---
    glm::vec3 visual_origin_xyz; // The offset of the visual mesh from the link's origin.
    glm::vec3 visual_origin_rpy; // The orientation offset of the visual mesh.
    std::string mesh_filepath; // Path to the primary visual mesh.
    MaterialDescription material; // The detailed PBR material for this link's visual mesh.
    bool casts_shadow = true; // Does this link cast a shadow in the scene?
    bool is_visible = true; // Is this link currently visible in the viewport?

    // --- Physics & Collision Properties ---
    float mass = 1.0f; // The mass of the link in kilograms.
    glm::mat3 inertia = glm::mat3(0.0f); // The moment of inertia. Consider making this a glm::mat3 for full 3D inertia tensor.
    glm::vec3 center_of_mass_offset = glm::vec3(0.0f); // Offset of the center of mass from the link's origin.
    std::string collision_mesh_filepath; // Path to a (usually simpler) mesh used for physics calculations.
    float friction = 0.5f; // The friction coefficient of the link's surface.
    float restitution = 0.1f; // The "bounciness" of the link.
    bool is_static = false; // Is this link immovable (like the robot's base)?

    // --- Robotics & Interaction Properties ---
    bool is_end_effector = false; // Is this link a tool/gripper?
    std::vector<NamedTransform> sensor_mounts; // A list of places to attach virtual sensors.

    // --- Editor & Metadata ---
    std::string editor_layer = "Default"; // Which layer or group does this belong to in the UI?
};



// JOINT CONTROLS AND SUPPORTING STRUCTURES

// --- Enumerations for Clarity ---
// Using enums makes the code self-documenting and prevents errors from typos in strings.

enum class JointType {
    FIXED,      // A non-moving connection between two links.
    REVOLUTE,   // Rotates around an axis with defined limits (e.g., an elbow).
    CONTINUOUS, // A revolute joint with no limits (e.g., a wheel).
    PRISMATIC,  // Slides along an axis (e.g., a piston).
    PLANAR,     // Moves in a 2D plane.
    FLOATING    // A 6-DOF connection, typically used to connect the robot's base to the world.
};

enum class ControlMode {
    POSITION,   // The primary goal is to reach a target position (angle/distance).
    VELOCITY,   // The primary goal is to maintain a target speed.
    TORQUE,     // The primary goal is to apply a specific torque.
    CURRENT,    // A low-level mode to command a specific current to the motor.
    DUTY_CYCLE, // The lowest-level command, setting the PWM duty cycle.
    INACTIVE    // The joint is not being actively controlled.
};

enum class MotorType {
    NONE,       // For unactuated joints.
    DC,         // A standard brushed DC motor.
    BLDC,       // A Brushless DC motor.
    PMSM,       // A Permanent Magnet Synchronous Motor (often used interchangeably with BLDC).
    STEPPER     // A stepper motor.
};

enum class CommutationType {
    NONE,
    TRAPEZOIDAL, // Standard for BLDC motors, requires Hall sensors.
    SINUSOIDAL,  // Requires a high-resolution encoder for smooth, quiet operation (FOC).
    MICROSTEP    // For stepper motors.
};

enum class CommunicationProtocol {
    NONE,
    SERIAL_CUSTOM, // A custom text- or binary-based protocol over UART.
    CANOPEN,       // A standard industrial protocol over CAN bus.
    ETHERCAT       // A high-speed industrial protocol over Ethernet.
};

// --- Nested Data Structures for Organization ---

// Describes the physical limits of the joint's movement.
struct JointLimits {
    double lower = 0.0;             // The lower position limit (radians for revolute, meters for prismatic).
    double upper = 0.0;             // The upper position limit (radians for revolute, meters for prismatic).
    double velocity_limit = 1.0;    // The maximum speed (rad/s or m/s).
    double effort_limit = 1.0;      // The maximum torque or force (Nm or N).
};

// Describes the physical properties of the motor itself.
struct MotorProperties {
    std::string model_name = "DefaultMotor"; // The model name, e.g., "Copley APV-09050".
    MotorType type = MotorType::NONE;       // The underlying technology of the motor (BLDC, Stepper, etc.).
    CommutationType commutation = CommutationType::NONE; // The control strategy required by the motor.
    uint32_t pole_pairs = 7;                // For BLDC/PMSM, the number of magnetic pole pairs. Crucial for electrical calculations.
    uint32_t phases = 3;                    // The number of electrical phases.
    double steps_per_revolution = 200.0;    // For stepper motors, the number of full steps per revolution.

    // Electrical Parameters
    double torque_constant_Kt = 0.0;        // In Nm/A (Newton-meters per Amp). Relates current to torque.
    double back_emf_constant_Ke = 0.0;      // In V/(rad/s) (Volts per radian per second). Relates speed to voltage.
    double terminal_resistance = 0.0;       // In Ohms. The electrical resistance of the motor windings.
    double terminal_inductance = 0.0;       // In Henrys. The electrical inductance of the motor windings.
    double max_continuous_current = 1.0;    // The maximum current the motor can handle continuously (Amps).
    double peak_current = 1.0;              // The maximum current the motor can handle for short bursts (Amps).
    double max_voltage = 48.0;              // The nominal maximum operating voltage (Volts).
};

// Contains all parameters for a standard PID (Proportional-Integral-Derivative) controller.
struct PIDParameters {
    double p = 0.0; // Proportional gain. The primary driver towards the target.
    double i = 0.0; // Integral gain. Corrects for steady-state error over time.
    double d = 0.0; // Derivative gain. Dampens oscillations and smooths movement.
    double i_max = 0.0; // The maximum value for the integral term to prevent "integral windup".
    double i_min = 0.0; // The minimum value for the integral term.
    double feed_forward = 0.0; // A feed-forward gain for velocity or acceleration.
};

// --- Sensor Structures ---

enum class EncoderType {
    INCREMENTAL,        // Reports relative changes in position. Needs a homing sequence.
    ABSOLUTE_SINGLE_TURN, // Knows its position within a single revolution.
    ABSOLUTE_MULTI_TURN   // Knows its position across multiple revolutions.
};

struct EncoderSensor {
    std::string model_name = "DefaultEncoder";
    EncoderType type = EncoderType::INCREMENTAL; // The type of encoder.
    double counts_per_revolution = 4096.0;      // The number of distinct "ticks" or counts per full rotation of the encoder.
    double gear_ratio_to_joint = 1.0;           // The gear ratio between this sensor and the final joint output. (e.g., > 1.0 if the sensor is on the high-speed motor shaft before a gearbox).
    double zero_offset = 0.0;                   // A calibration offset to align the encoder's zero with the joint's zero.
};

struct PotentiometerSensor {
    std::string model_name = "DefaultPot";
    double min_voltage = 0.0; // The voltage reading at the minimum joint limit.
    double max_voltage = 5.0; // The voltage reading at the maximum joint limit.
    double gear_ratio_to_joint = 1.0; // Almost always 1.0 for a pot, but included for consistency.
};

struct HallEffectSensor {
    std::string model_name = "DefaultHall";
    // Hall sensors often don't have a "counts" value, but they provide coarse absolute position within one electrical revolution of a BLDC motor.
    // The control system uses this for trapezoidal commutation.
    uint32_t phase_A_pin = 0;
    uint32_t phase_B_pin = 0;
    uint32_t phase_C_pin = 0;
};

// A union-like type that can hold any of our defined sensor types.
using SensorVariant = std::variant<EncoderSensor, PotentiometerSensor, HallEffectSensor>;

// Describes how the software communicates with the physical hardware controller.
struct HardwareInterface {
    uint32_t controller_id = 0;                     // The unique ID of the hardware controller on the bus (e.g., CAN Node ID).
    CommunicationProtocol protocol = CommunicationProtocol::NONE; // The communication protocol being used.
    std::string command_topic_name;                 // The name of the topic/channel to send commands on (e.g., "joint1/set_position").
    std::string feedback_topic_name;                // The name of the topic/channel to receive feedback on (e.g., "joint1/current_position").
};


//================================================================================
// The Master JointDescription Struct
// This brings all the other structures together into one comprehensive definition.
//================================================================================
struct JointDescription {
    // --- Core Kinematic Properties ---
    std::string name;                                     // The human-readable name of the joint, e.g., "elbow_joint".
    uint64_t persistent_id;                               // A unique, permanent ID for this joint.
    JointType type = JointType::REVOLUTE;                 // The type of motion this joint allows.
    std::string parent_link_name;                         // The name of the parent link this joint connects to.
    std::string child_link_name;                          // The name of the child link this joint moves.
    glm::vec3 origin_xyz = glm::vec3(0.0f);               // The position of the joint relative to the parent link's origin.
    glm::vec3 origin_rpy = glm::vec3(0.0f);               // The orientation of the joint relative to the parent link's origin (roll, pitch, yaw).
    glm::vec3 axis = glm::vec3(0.0f, 0.0f, 1.0f);         // The axis of rotation or translation, in the joint's own coordinate frame.

    // --- Mechanical & Transmission Properties ---
    JointLimits limits;                                   // A struct containing the physical motion limits of the joint.
    double gear_reduction = 1.0;                          // The gear ratio (e.g., 100.0 means a 100:1 gearbox). 1.0 for direct drive.
    double transmission_efficiency = 1.0;                 // The efficiency of the gearbox/transmission (0.0 to 1.0).
    double static_friction = 0.0;                         // The friction that must be overcome to start moving (Stribeck friction).
    double dynamic_friction = 0.0;                        // The friction that resists motion while moving (Coulomb and viscous friction).

    // --- Actuator & Electrical Properties ---
    MotorProperties motor;                                // A struct containing the detailed physical properties of the electric motor.

    // --- Control System Properties ---
    ControlMode default_control_mode = ControlMode::POSITION; // The control mode the joint boots into.
    PIDParameters position_pid;                           // PID parameters for when the joint is in position control mode.
    PIDParameters velocity_pid;                           // PID parameters for when the joint is in velocity control mode.
    PIDParameters torque_pid;                             // PID parameters for when the joint is in torque/current control mode.

    // --- Sensor & Feedback Properties ---
    // A joint can have multiple sensors (e.g., a low-res pot for absolute position and a high-res incremental encoder for velocity).
    std::vector<SensorVariant> sensors;                   // A list of all sensors attached to this joint.

    // --- Hardware Interface Properties ---
    HardwareInterface interface;                          // A struct describing how to communicate with the physical joint controller.
};

struct RobotDescription {
    std::string name;
    std::vector<LinkDescription> links;
    std::vector<JointDescription> joints;

    bool needsEnrichment = false;
};

