#version 410 core

// Per-vertex attributes for the base arrow mesh
layout (location = 0) in vec3 aPos;    // e.g., (0, 0, 1)
layout (location = 1) in vec3 aNormal; // Normal for lighting (optional)

// Per-instance attributes (these change for each arrow drawn)
layout (location = 2) in mat4 aInstanceMatrix; // Unique Model matrix for this one arrow
layout (location = 6) in vec3 aInstanceColor;  // Unique color for this one arrow

// Outputs to the fragment shader
out VS_OUT {
    vec3 color;
} vs_out;

// Uniforms (constant for the entire draw call)
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Transform the vertex position by the instance's unique matrix, then view, then projection
    gl_Position = projection * view * aInstanceMatrix * vec4(aPos, 1.0);
    
    // Pass the instance's color to the fragment shader
    vs_out.color = aInstanceColor;
}
