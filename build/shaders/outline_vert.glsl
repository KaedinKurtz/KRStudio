// ===================================
//      outline_vert.glsl
// ===================================
#version 330 core
layout (location = 0) in vec3 aPos; // Vertex position in world space

// Uniforms from the C++ application
uniform mat4 u_view;
uniform mat4 u_projection;

void main()
{
    // Transform the world-space vertex position to clip space.
    // The model matrix is not needed because the vertices are already in world space.
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}