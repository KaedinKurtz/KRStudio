#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Camera.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include <urdf_parser/urdf_parser.h>

// Global camera object
Camera camera;

// Basic data structures to hold simplified URDF data
struct MyJoint {
    std::string name;
    glm::vec3 origin_xyz;
    glm::vec3 origin_rpy;
    glm::vec3 axis;
    double current_angle = 0.0;
};

struct MyLink {
    std::string name;
    std::vector<std::shared_ptr<MyLink>> child_links;
    std::vector<std::shared_ptr<MyJoint>> child_joints;
};

// GLFW callback functions
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    static double lastX = xpos, lastY = ypos;
    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos; // reversed since y-coordinates go from top to bottom
    lastX = xpos;
    lastY = ypos;
    camera.processMouseMovement(xoffset, yoffset);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    camera.processMouseButton(button, action, mods, window);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.processMouseScroll(yoffset);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        camera.toggleProjection();
    }
}

// URDF parsing function
std::shared_ptr<MyLink> loadUrdf(const std::string& filename, std::map<std::string, std::shared_ptr<MyLink>>& link_map) {
    urdf::ModelInterfaceSharedPtr urdf_robot = urdf::parseURDFFile(filename);
    if (!urdf_robot) {
        std::cerr << "ERROR: Failed to parse URDF file." << std::endl;
        return nullptr;
    }

    // Create a MyLink for each link in the URDF
    for (auto const& [name, link] : urdf_robot->links_) {
        link_map[name] = std::make_shared<MyLink>();
        link_map[name]->name = name;
    }

    // Populate the parent-child relationships from the URDF joints
    for (auto const& [name, joint] : urdf_robot->joints_) {
        std::string parent_name = joint->parent_link_name;
        std::string child_name = joint->child_link_name;
        std::shared_ptr<MyLink> parent_link = link_map[parent_name];

        auto my_joint = std::make_shared<MyJoint>();
        my_joint->name = name;
        my_joint->origin_xyz = glm::vec3(joint->parent_to_joint_origin_transform.position.x, joint->parent_to_joint_origin_transform.position.y, joint->parent_to_joint_origin_transform.position.z);

        double r, p, y;
        joint->parent_to_joint_origin_transform.rotation.getRPY(r, p, y);
        my_joint->origin_rpy = glm::vec3(r, p, y);
        my_joint->axis = glm::vec3(joint->axis.x, joint->axis.y, joint->axis.z);

        parent_link->child_joints.push_back(my_joint);
        parent_link->child_links.push_back(link_map[child_name]);
    }

    // Return the root link
    return link_map[urdf_robot->getRoot()->name];
}

// Recursive function to draw the robot links
void drawLinkRecursive(const std::shared_ptr<MyLink>& link, Shader& shader, const Mesh& mesh, const glm::mat4& parentTransform) {
    for (size_t i = 0; i < link->child_links.size(); ++i) {
        auto child_joint = link->child_joints[i];
        auto child_link = link->child_links[i];

        // Calculate the transform for this joint
        glm::mat4 joint_translation = glm::translate(glm::mat4(1.0f), child_joint->origin_xyz);
        glm::mat4 joint_rotation = glm::rotate(glm::mat4(1.0f), (float)child_joint->current_angle, child_joint->axis);

        // The transform of this link is the parent's transform followed by this joint's transform
        glm::mat4 current_link_transform = parentTransform * joint_translation * joint_rotation;

        // Draw the link
        shader.setMat4("model", current_link_transform);
        mesh.draw();

        // Recurse for child links
        drawLinkRecursive(child_link, shader, mesh, current_link_transform);
    }
}

int main() {
    // --- GLFW and GLAD Initialization ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Robotics Software", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // --- Set GLFW Callbacks ---
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    // --- Define Cube Vertex Data ---
    // This is the only low-level graphics data remaining in main,
    // and it's neatly contained.
    const std::vector<float> cube_vertices = {
        // positions
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f
    };

    // --- Create Mesh and Shader ---
    Mesh cube_mesh(cube_vertices);
    Shader ourShader("shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");

    // --- Load Robot Model ---
    std::map<std::string, std::shared_ptr<MyLink>> link_map;
    std::shared_ptr<MyLink> root_link = loadUrdf("simple_arm.urdf", link_map);
    if (!root_link) { return -1; }

    glEnable(GL_DEPTH_TEST);

    // --- Render Loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ourShader.use();

        // Animate the first joint for demonstration
        if (!root_link->child_joints.empty()) {
            root_link->child_joints[0]->current_angle = glfwGetTime();
        }

        // Set camera matrices
        ourShader.setMat4("view", camera.getViewMatrix());
        ourShader.setMat4("projection", camera.getProjectionMatrix(1280.0f / 720.0f));

        if (root_link) {
            // Draw the base link at the origin
            glm::mat4 base_transform = glm::mat4(1.0f);
            ourShader.setMat4("model", base_transform);
            cube_mesh.draw();

            // Recursively draw all child links
            drawLinkRecursive(root_link, ourShader, cube_mesh, base_transform);
        }

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}