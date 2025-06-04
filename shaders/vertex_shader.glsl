// Specifies the GLSL version. 330 corresponds to OpenGL 3.3.
// 'core' means we're using the modern, core profile of OpenGL (no deprecated features).
#version 330 core

// Input vertex attribute for position.
// 'layout(location = 0)' means this attribute 'aPos' will receive data from
// the vertex buffer bound to attribute slot 0 in your VAO setup on the C++ side.
// 'in vec3 aPos;' declares 'aPos' as an input variable of type 3-component vector (x, y, z).
layout (location = 0) in vec3 aPos;

// Uniform variables. Uniforms are global variables in the shader program
// that are set from your C++ application. They remain constant for all vertices
// processed in a single draw call.
uniform mat4 model;      // Model matrix: Transforms vertex positions from model space (local to the object) to world space.
uniform mat4 view;       // View matrix: Transforms vertex positions from world space to view space (camera space).
uniform mat4 projection; // Projection matrix: Transforms vertex positions from view space to clip space. This handles perspective or orthographic projection.

void main()
{
    // This is the core of the vertex shader.
    // It calculates the final position of the vertex on the screen.
    // Transformations are applied in reverse order of how they're written here:
    // 1. vec4(aPos, 1.0): Converts the 3D input position 'aPos' into a 4D homogeneous coordinate.
    //    The '1.0' for the w-component is crucial for matrix transformations, especially perspective.
    // 2. model * vec4(aPos, 1.0): Multiplies by the model matrix. Vertex is now in world space.
    // 3. view * (model * ...): Multiplies by the view matrix. Vertex is now in view/camera space.
    // 4. projection * (view * ...): Multiplies by the projection matrix. Vertex is now in clip space.
    //
    // gl_Position is a special built-in output variable in vertex shaders.
    // The value assigned to it is what the GPU uses for subsequent stages (like primitive assembly and clipping).
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
