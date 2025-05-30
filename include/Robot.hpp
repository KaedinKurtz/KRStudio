#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations to avoid including heavy headers here
class Shader;
class Mesh;

class Robot {
public:
    // --- Enums for State Management ---
    enum class OperatingMode { MANUAL, AUTONOMOUS, TEACH, ESTOP };
    enum class State { IDLE, MOVING, WORKING, ERROR, BOOTING, SHUTDOWN };

    // --- Constructor ---
    Robot(const std::string& urdf_path);

    // --- Public Interface ---
    void update(double deltaTime);
    void draw(Shader& shader, Mesh& link_mesh) const;

    // --- Control Interface (Stubs for now) ---
    void setJointTargetAngles(const std::vector<double>& angles);
    void setOperatingMode(OperatingMode mode);
    void emergencyStop();
    void clearErrors();

    // --- Public Getters for Monitoring ---
    State getState() const { return currentState; }
    OperatingMode getOperatingMode() const { return currentMode; }
    double getUptime() const { return uptime; }
    const std::vector<double>& getJointPositions() const { return jointPositions_rad; }
    // ... other getters can be added as needed

private:
    // --- Nested Structures for Kinematic Tree ---
    struct Joint; // Forward declare for Link
    struct Link {
        std::string name;
        glm::vec3 visual_origin_xyz{ 0.0f, 0.0f, 0.0f }; // Placeholder for visual geometry offset
        std::vector<std::shared_ptr<Link>> child_links;
        std::vector<std::shared_ptr<Joint>> child_joints;
    };

    struct Joint {
        std::string name;
        glm::vec3 origin_xyz;
        glm::vec3 origin_rpy;
        glm::vec3 axis;
        double current_angle_rad = 0.0;
    };

    // --- Private Member Variables ---

    // Kinematic Structure
    std::shared_ptr<Link> rootLink;
    std::map<std::string, std::shared_ptr<Link>> linkMap;
    std::map<std::string, std::shared_ptr<Joint>> jointMap;

    // Identification
    std::string modelName;
    std::string robotId = "RB-001";
    std::string manufacturer = "Gemini Robotics";

    // Operational Status
    State currentState = State::BOOTING;
    OperatingMode currentMode = OperatingMode::MANUAL;
    double uptime = 0.0;
    std::string currentTask = "None";
    double taskProgress = 0.0;

    // Kinematic & Dynamic State (Actuals)
    std::vector<double> jointPositions_rad;
    std::vector<double> jointVelocities_rad_s;
    std::vector<double> jointEfforts_Nm;
    glm::vec3 endEffectorPosition{ 0.0f, 0.0f, 0.0f };
    glm::quat endEffectorOrientation{ 1.0f, 0.0f, 0.0f, 0.0f };

    // Diagnostics & Power
    double mainVoltage_V = 24.1;
    std::vector<double> motorTemperatures_C;
    double cpuTemperature_C = 45.5;
    double cpuUsage_percent = 15.2;
    double memoryUsage_percent = 22.5;

    // Safety & Errors
    bool isEmergencyStopPressed = false;
    bool isProtectiveStop = false;
    std::vector<std::string> activeErrors;

    // --- Private Helper Functions ---
    void loadModel(const std::string& urdf_path);
    void drawRecursive(const std::shared_ptr<Link>& link, Shader& shader, Mesh& mesh, const glm::mat4& parentTransform) const;
};