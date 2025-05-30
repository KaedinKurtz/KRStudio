#include "Robot.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include <urdf_parser/urdf_parser.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
// No need to include <QTime> here

Robot::Robot(const std::string& urdf_path) {
    loadModel(urdf_path);
}

void Robot::loadModel(const std::string& urdf_path) {
    // ... (this function remains unchanged)
    urdf::ModelInterfaceSharedPtr urdf_robot = urdf::parseURDFFile(urdf_path);
    if (!urdf_robot) {
        std::cerr << "ERROR::ROBOT: Failed to parse URDF file: " << urdf_path << std::endl;
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

        std::shared_ptr<Link> parent_link = linkMap[parent_name];
        auto new_joint = std::make_shared<Joint>();
        new_joint->name = name;
        new_joint->origin_xyz = glm::vec3(joint->parent_to_joint_origin_transform.position.x, joint->parent_to_joint_origin_transform.position.y, joint->parent_to_joint_origin_transform.position.z);
        new_joint->axis = glm::vec3(joint->axis.x, joint->axis.y, joint->axis.z);

        parent_link->child_joints.push_back(new_joint);
        parent_link->child_links.push_back(linkMap[child_name]);
        jointMap[name] = new_joint;
    }
    rootLink = linkMap[urdf_robot->getRoot()->name];
}

// CORRECTED UPDATE FUNCTION
void Robot::update(double deltaTime) {
    // Accumulate the delta time from the application's timer
    m_totalTime += deltaTime * 50.0; // Multiplying to speed up the animation a bit

    if (jointMap.count("joint_1")) {
        // Use the framework-independent total time for a smooth sine-wave animation
        jointMap["joint_1"]->current_angle_rad = sin(glm::radians(m_totalTime));
    }
}

void Robot::draw(Shader& shader, Mesh& link_mesh) const {
    // ... (this function remains unchanged)
    if (!rootLink) return;

    glm::mat4 identity = glm::mat4(1.0f);
    shader.setMat4("model", identity);
    link_mesh.draw();

    drawRecursive(rootLink, shader, link_mesh, identity);
}

void Robot::drawRecursive(const std::shared_ptr<Link>& link, Shader& shader, Mesh& mesh, const glm::mat4& parentTransform) const {
    // ... (this function remains unchanged)
    if (!link) return;

    for (size_t i = 0; i < link->child_links.size(); ++i) {
        auto child_joint = link->child_joints[i];
        auto child_link = link->child_links[i];

        glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), child_joint->origin_xyz);
        glm::mat4 rotation_matrix = glm::rotate(glm::mat4(1.0f), (float)child_joint->current_angle_rad, child_joint->axis);

        glm::mat4 current_transform = parentTransform * translation_matrix * rotation_matrix;

        shader.setMat4("model", current_transform);
        mesh.draw();

        drawRecursive(child_link, shader, mesh, current_transform);
    }
}