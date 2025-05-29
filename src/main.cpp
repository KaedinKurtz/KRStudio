#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- Global State Variables ---
glm::vec3 camera_focal_point(0.0f, 0.0f, 0.0f);
glm::vec3 camera_up_vector(0.0f, 1.0f, 0.0f);
float camera_distance = 5.0f;
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

double last_mouse_x = 0.0;
double last_mouse_y = 0.0;
bool is_orbiting = false;
bool is_panning = false;

bool is_perspective = true;


// --- Helper Function ---
std::string loadShaderSource(const std::string& filePath) {
    std::ifstream shaderFile(filePath);
    if (!shaderFile.is_open()) {
        std::cerr << "ERROR: Could not open shader file: " << filePath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << shaderFile.rdbuf();
    return buffer.str();
}

// --- GLFW Callback Functions ---
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    double delta_x = xpos - last_mouse_x;
    double delta_y = ypos - last_mouse_y;

    if (is_orbiting) {
        camera_yaw += delta_x * 0.005;
        camera_pitch -= delta_y * 0.005;
        if (camera_pitch > glm::radians(89.0f)) camera_pitch = glm::radians(89.0f);
        if (camera_pitch < glm::radians(-89.0f)) camera_pitch = glm::radians(-89.0f);
    }
    else if (is_panning) {
        glm::vec3 camera_position;
        camera_position.x = camera_focal_point.x + camera_distance * cos(camera_pitch) * sin(camera_yaw);
        camera_position.y = camera_focal_point.y + camera_distance * sin(camera_pitch);
        camera_position.z = camera_focal_point.z + camera_distance * cos(camera_pitch) * cos(camera_yaw);

        glm::vec3 camera_direction = glm::normalize(camera_focal_point - camera_position);
        glm::vec3 right_vector = glm::normalize(glm::cross(camera_direction, camera_up_vector));
        glm::vec3 up_vector_actual = glm::normalize(glm::cross(right_vector, camera_direction));

        float pan_speed = 0.002f * camera_distance;
        camera_focal_point -= right_vector * (float)delta_x * pan_speed;
        camera_focal_point += up_vector_actual * (float)delta_y * pan_speed;
    }

    last_mouse_x = xpos;
    last_mouse_y = ypos;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
            is_panning = true;
        }
        else {
            is_orbiting = true;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE) {
        is_orbiting = false;
        is_panning = false;
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camera_distance -= yoffset * 0.5f;
    if (camera_distance < 1.0f) {
        camera_distance = 1.0f;
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_P)
    {
        is_perspective = !is_perspective;
        std::cout << "Switched to " << (is_perspective ? "Perspective" : "Orthographic") << " projection." << std::endl;
    }
}

// --- Main Application ---
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Projection Toggle: Press 'P'", NULL, NULL);
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

    // Register all callbacks
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    // --- 2. VERTEX DATA AND BUFFERS ---
    float vertices[] = {
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,

        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,
        -0.5f, -0.5f,  0.5f,

        -0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,

         0.5f,  0.5f,  0.5f,
         0.5f,  0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,

        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
        -0.5f, -0.5f,  0.5f,
        -0.5f, -0.5f, -0.5f,

        -0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
         0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f, -0.5f,
    };
    int verticesLength = sizeof(vertices);

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Shader compilation and linking
    std::string vertexShaderSource = loadShaderSource("shaders/vertex_shader.glsl");
    std::string fragmentShaderSource = loadShaderSource("shaders/fragment_shader.glsl");
    const char* vsSource = vertexShaderSource.c_str();
    const char* fsSource = fragmentShaderSource.c_str();

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");

    // Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::vec3 camera_position;
        camera_position.x = camera_focal_point.x + camera_distance * cos(camera_pitch) * sin(camera_yaw);
        camera_position.y = camera_focal_point.y + camera_distance * sin(camera_pitch);
        camera_position.z = camera_focal_point.z + camera_distance * cos(camera_pitch) * cos(camera_yaw);

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(camera_position, camera_focal_point, camera_up_vector);

        glm::mat4 projection;
        if (is_perspective)
        {
            //Perspective projection uses Field of View. Zoom is handled by camera movement.
            projection = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        }
        else
        {
            // In orthographic mode, "zoom" means changing the size of the viewing box.
            // Tie the size of the box to our camera_distance variable.
            float aspect_ratio = 1280.0f / 720.0f;
            float ortho_size = camera_distance * 0.5f; // You can adjust the 0.5f multiplier for sensitivity

            projection = glm::ortho(
                -ortho_size * aspect_ratio, // left
                ortho_size * aspect_ratio, // right
                -ortho_size,                // bottom
                ortho_size,                // top
                -100.0f,                    // near clipping plane
                100.0f                     // far clipping plane
            );
        }

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, verticesLength);

        glfwSwapBuffers(window);
    }

    // Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}