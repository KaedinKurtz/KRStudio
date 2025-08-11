#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>

class Shader;
class Mesh;

class Robot {
public:
    Robot(const std::string& urdf_path);
    void update(double deltaTime);
    void draw(Shader& shader, Mesh& link_mesh) const;
    std::map<std::string, double> getJointStates() const;

    enum class State {
        Idle,
        Moving,
        ExecutingTask,
        Error
    };

    enum class OperatingMode {
        Manual,
        Autonomous,
        Simulation
    };

private:
    struct Joint;
    struct Link {
        std::string name;
        std::vector<std::shared_ptr<Link>> child_links;
        std::vector<std::shared_ptr<Joint>> child_joints;
    };

    struct Joint {
        std::string name;
        glm::vec3 origin_xyz;
        glm::vec3 axis;
        double current_angle_rad = 0.0;
    };

    void loadModel(const std::string& urdf_path);
    void drawRecursive(const std::shared_ptr<Link>& link, Shader& shader, Mesh& mesh, const glm::mat4& parentTransform) const;

    std::shared_ptr<Link> rootLink;
    std::map<std::string, std::shared_ptr<Link>> linkMap;
    std::map<std::string, std::shared_ptr<Joint>> jointMap;
    std::string modelName;

    // Add this member to track time for animation
    double m_totalTime = 0.0;
};