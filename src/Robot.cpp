/**
 * @file Robot.cpp
 * @brief Implementation of the Robot class for loading and rendering a URDF model.
 */

#include "Robot.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include <urdf_parser/urdf_parser.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>

 /**
  * @brief Constructs a Robot object from a URDF file path.
  * @param urdf_path The file path to the .urdf file.
  */
Robot::Robot(const std::string& urdf_path) {
    loadModel(urdf_path);
}

/**
 * @brief Loads the robot's structure from a URDF file.
 * @param urdf_path The path to the URDF file.
 */
void Robot::loadModel(const std::string& urdf_path) {
    // Use the urdf_parser library to parse the URDF file into a model interface.
    urdf::ModelInterfaceSharedPtr urdf_robot = urdf::parseURDFFile(urdf_path);
    if (!urdf_robot) {
        std::cerr << "ERROR::ROBOT: Failed to parse URDF file: " << urdf_path << std::endl;
        return;
    }
    modelName = urdf_robot->getName();

    // First pass: create all Link objects and store them in a map for easy access.
    for (auto const& [name, link] : urdf_robot->links_) {
        linkMap[name] = std::make_shared<Link>();
        linkMap[name]->name = name;
    }

    // Second pass: create all Joint objects and build the kinematic tree structure.
    for (auto const& [name, joint] : urdf_robot->joints_) {
        std::string parent_name = joint->parent_link_name;
        std::string child_name = joint->child_link_name;

        // Retrieve the parent link from our map.
        std::shared_ptr<Link> parent_link = linkMap[parent_name];
        // Create a new Joint object and populate it with data from the URDF.
        auto new_joint = std::make_shared<Joint>();
        new_joint->name = name;
        new_joint->origin_xyz = glm::vec3(joint->parent_to_joint_origin_transform.position.x, joint->parent_to_joint_origin_transform.position.y, joint->parent_to_joint_origin_transform.position.z);
        new_joint->axis = glm::vec3(joint->axis.x, joint->axis.y, joint->axis.z);

        // Connect the parent link to its child joint and child link.
        parent_link->child_joints.push_back(new_joint);
        parent_link->child_links.push_back(linkMap[child_name]);
        // Store the new joint in our joint map.
        jointMap[name] = new_joint;
    }
    // Set the root of our kinematic tree.
    rootLink = linkMap[urdf_robot->getRoot()->name];
}

/**
 * @brief Updates the robot's joint angles for animation.
 * @param deltaTime The time elapsed since the last frame.
 */
void Robot::update(double deltaTime) {
    // A simple animation based on total elapsed time.
    m_totalTime += deltaTime * 50.0; // Speed up the animation

    // Animate joint_1 and joint_2 using sin and cos for a simple circular motion.
    if (jointMap.count("joint_1")) {
        jointMap["joint_1"]->current_angle_rad = sin(glm::radians(m_totalTime));
    }
    if (jointMap.count("joint_2")) {
        jointMap["joint_2"]->current_angle_rad = cos(glm::radians(m_totalTime));
    }
}

/**
 * @brief Draws the entire robot.
 * @param shader The shader program to use for rendering.
 * @param link_mesh The mesh to use for drawing each link.
 */
void Robot::draw(Shader& shader, Mesh& link_mesh) const {
    (void)shader;
    (void)link_mesh;
}

/**
 * @brief Recursively traverses the kinematic tree and draws each link.
 * @param link The current link in the traversal.
 * @param shader The shader to use.
 * @param mesh The mesh to draw for each link.
 * @param parentTransform The transformation matrix of the parent link.
 */
void Robot::drawRecursive(const std::shared_ptr<Link>& link, Shader& shader, Mesh& mesh, const glm::mat4& parentTransform) const {
    (void)shader;
    (void)mesh;
    (void)parentTransform;
    if (!link) return;

    // Iterate through all children of the current link.
    for (size_t i = 0; i < link->child_links.size(); ++i) {
        auto child_joint = link->child_joints[i];
        auto child_link = link->child_links[i];

        // Calculate the local transformation for this joint.
        // 1. Translate from parent link's origin to this joint's origin.
        glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), child_joint->origin_xyz);
        // 2. Rotate around the joint's axis by its current angle.
        glm::mat4 rotation_matrix = glm::rotate(glm::mat4(1.0f), (float)child_joint->current_angle_rad, child_joint->axis);

        // The final model matrix for the child link is ParentTransform * LocalTransform.
        glm::mat4 current_transform = parentTransform * translation_matrix * rotation_matrix;

        // Set a color for the link for visual distinction.
        glm::vec4 color;
        if (child_joint->name == "joint_2") {
            color = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); // Green
        }
        else {
            color = glm::vec4(1.0f, 0.5f, 0.2f, 1.0f); // Orange
        }
        (void)color;

        // Recurse to draw the children of this link, passing down the new transform.
        drawRecursive(child_link, shader, mesh, current_transform);
    }
}

/**
 * @brief Retrieves the current state (angles) of all joints.
 * @return A map from joint name to its current angle in radians.
 */
std::map<std::string, double> Robot::getJointStates() const {
    std::map<std::string, double> states;
    for (const auto& [name, joint_ptr] : jointMap) {
        states[name] = joint_ptr->current_angle_rad;
    }
    return states;
}