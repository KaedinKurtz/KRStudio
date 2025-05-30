#include <iostream>
#include <vector>
#include <memory>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Robot.hpp"

// Global camera object
Camera camera;

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

int main() {
    // --- Initialization ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Robotics Software", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // --- Callbacks ---
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    // --- Resources ---
    const std::vector<float> cube_vertices = { /* ... vertex data from previous steps ... */
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,-0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,-0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,-0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,-0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f,-0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f,
    };
    Mesh cube_mesh(cube_vertices);
    Shader ourShader("shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
    Robot simpleArm("simple_arm.urdf");

    glEnable(GL_DEPTH_TEST);

    // --- Timing ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    // --- Render Loop ---
    while (!glfwWindowShouldClose(window)) {
        // --- Frame Timing ---
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // --- Input & State Update ---
        glfwPollEvents();
        simpleArm.update(deltaTime);

        // --- Rendering ---
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ourShader.use();

        // Set camera uniforms
        ourShader.setMat4("view", camera.getViewMatrix());
        ourShader.setMat4("projection", camera.getProjectionMatrix(1280.0f / 720.0f));

        // Draw the robot
        simpleArm.draw(ourShader, cube_mesh);

        // --- Swap Buffers ---
        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}