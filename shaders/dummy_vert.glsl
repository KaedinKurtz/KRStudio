// File: debug_vert.glsl
#version 450 core

// We declare this to match the fragment shader, but we don't use it.
out vec3 localPos;

// An array of hardcoded vertex positions in Normalized Device Coordinates.
const vec3 positions[3] = vec3[](
    vec3(-1.0, -1.0, 1.0), // Bottom-left
    vec3( 3.0, -1.0, 1.0), // Far bottom-right
    vec3(-1.0,  3.0, 1.0)  // Far top-left
);

void main()
{
    // Directly output one of the hardcoded positions. This bypasses
    // all matrix uniforms and vertex attributes from the VAO.
    gl_Position = vec4(positions[gl_VertexID], 1.0);
    
    // Pass a dummy value just to make the shader interface valid.
    localPos = positions[gl_VertexID];
}