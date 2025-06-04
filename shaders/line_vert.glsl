/**
 * @file line_vert.glsl
 * @brief Vertex shader for drawing simple colored lines or objects.
 *
 * This shader takes a 3D vertex position and transforms it into clip space
 * using the provided view and projection matrices.
 */

// Specifies the GLSL version. 330 corresponds to OpenGL 3.3.
#version 330 core

// Input vertex attribute for position. 'layout(location = 0)' binds this
// attribute to the first slot (index 0) of the VAO.
layout(location = 0) in vec3 aPos;

// Uniform variables are global and set from the C++ application.
uniform mat4 view;       // The camera's view matrix.
uniform mat4 projection; // The camera's projection matrix (perspective or ortho).

void main() {
    // Calculate the final clip-space position of the vertex.
    // The transformation is applied in reverse order: Model (if any) -> View -> Projection.
    // Here, the model matrix is assumed to be applied on the C++ side or is an identity matrix.
    gl_Position = projection * view * vec4(aPos, 1.0);
}