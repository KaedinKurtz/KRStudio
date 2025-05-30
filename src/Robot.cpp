#include "Robot.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"

#include <urdf_parser/urdf_parser.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>

Robot::Robot(const std::string& urdf_path) {
    loadModel(urdf_path);
    currentState = State::IDLE; // Mark booting as complete
}

void Robot::loadModel(const std::string& urdf_path) {
    urdf::ModelInterfaceSharedPtr urdf_robot = urdf::parseURDFFile(urdf_path);
    if (!urdf_robot) {
        std::cerr << "ERROR::ROBOT: Failed to parse URDF file: " << urdf_path << std::endl;
        activeErrors.push_back("URDF_PARSE_FAILED");
        currentState = State::ERROR;
        return;
    }

    modelName = urdf_robot->getName();

    for (auto const& [name, link] : urdf_robot->links_) {
        linkMap[name] = std::make_shared<Link>();
        linkMap[name]->name = name;
    }

    for (auto const& [name, joint] : urdf_robot->joints_) {
        std::string parent_name = joint->parent_link_name;
        std::string child_name = joint->child_link_name;

        if (linkMap.find(parent_name) == linkMap.end() || linkMap.find(child_name) == linkMap.end()) {
            std::cerr << "ERROR::ROBOT: Invalid link name in joint '" << name << "'" << std::endl;
            continue;
        }

        std::shared_ptr<Link> parent_link = linkMap[parent_name];
        auto new_joint = std::make_shared<Joint>();
        new_joint->name = name;
        new_joint->origin_xyz = glm::vec3(joint->parent_to_joint_origin_transform.position.x, joint->parent_to_joint_origin_transform.position.y, joint->parent_to_joint_origin_transform.position.z);
        double r, p, y;
        joint->parent_to_joint_origin_transform.rotation.getRPY(r, p, y);
        new_joint->origin_rpy = glm::vec3(r, p, y);
        new_joint->axis = glm::vec3(joint->axis.x, joint->axis.y, joint->axis.z);

        parent_link->child_joints.push_back(new_joint);
        parent_link->child_links.push_back(linkMap[child_name]);
        jointMap[name] = new_joint;
    }

    rootLink = linkMap[urdf_robot->getRoot()->name];

    // Initialize state vectors based on number of joints
    size_t num_joints = jointMap.size();
    jointPositions_rad.assign(num_joints, 0.0);
    jointVelocities_rad_s.assign(num_joints, 0.0);
    jointEfforts_Nm.assign(num_joints, 0.0);
    motorTemperatures_C.assign(num_joints, 30.0); // Default temp
}

void Robot::update(double deltaTime) {
    if (currentState == State::ERROR || isEmergencyStopPressed) {
        return;
    }

    uptime += deltaTime;

    // --- Simulate state changes for demonstration ---

    // Animate the first joint using a sine wave for smooth motion
    if (!jointMap.empty()) {
        auto first_joint_it = jointMap.begin();
        first_joint_it->second->current_angle_rad = sin(uptime);

        // Update the public-facing state vector
        jointPositions_rad[0] = first_joint_it->second->current_angle_rad;
    }
    // In a real application, you would update all joint angles here from sensor data
}

void Robot::draw(Shader& shader, Mesh& link_mesh) const {
    if (!rootLink) return;

    // Draw the base link at the origin
    glm::mat4 base_transform = glm::mat4(1.0f);
    shader.setMat4("model", base_transform);
    link_mesh.draw();

    // Recursively draw all child links
    drawRecursive(rootLink, shader, link_mesh, base_transform);
}

void Robot::drawRecursive(const std::shared_ptr<Link>& link, Shader& shader, Mesh& mesh, const glm::mat4& parentTransform) const {
    for (size_t i = 0; i < link->child_links.size(); ++i) {
        auto child_joint = link->child_joints[i];

        glm::mat4 joint_translation = glm::translate(glm::mat4(1.0f), child_joint->origin_xyz);
        glm::mat4 joint_rotation = glm::rotate(glm::mat4(1.0f), (float)child_joint->current_angle_rad, child_joint->axis);

        glm::mat4 current_link_transform = parentTransform * joint_translation * joint_rotation;

        shader.setMat4("model", current_link_transform);
        mesh.draw();

        drawRecursive(link->child_links[i], shader, mesh, current_link_transform);
    }
}

// --- Control Interface Implementations (Stubs) ---
void Robot::setJointTargetAngles(const std::vector<double>& angles) {
    std::cout << "Command received: Set joint angles." << std::endl;
    // In a real system, this would send commands to motor controllers.
}

void Robot::setOperatingMode(OperatingMode mode) {
    currentMode = mode;
    std::cout << "Operating mode set to: " << static_cast<int>(mode) << std::endl;
}

void Robot::emergencyStop() {
    isEmergencyStopPressed = true;
    currentState = State::ERROR;
    activeErrors.push_back("EMERGENCY_STOP_ACTIVATED");
    std::cerr << "EMERGENCY STOP ACTIVATED" << std::endl;
}

void Robot::clearErrors() {
    activeErrors.clear();
    if (currentState == State::ERROR) {
        currentState = State::IDLE;
    }
    std::cout << "Errors cleared." << std::endl;
}