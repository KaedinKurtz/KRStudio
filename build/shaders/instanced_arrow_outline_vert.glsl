#version 410 core

// Per-vertex attributes for the base arrow mesh
layout (location = 0) in vec3 aPos;    // Vertex position
layout (location = 1) in vec3 aNormal; // Vertex normal

// Per-instance attributes (unique for each arrow)
layout (location = 2) in mat4 aInstanceMatrix; // Model matrix
layout (location = 6) in vec3 aInstanceColor;  // Color

// Pass-through to the Geometry Shader
out VS_OUT {
    vec3 color;
    vec3 normal;
} vs_out;

void main()
{
    // We don't apply projection or view matrices here.
    // That will be done in the Geometry Shader after it generates the new vertices.
    // We just pass the world-space position.
    gl_Position = aInstanceMatrix * vec4(aPos, 1.0);
    
    // Pass the color and transformed normal to the next stage.
    vs_out.color = aInstanceColor;
    vs_out.normal = mat3(transpose(inverse(aInstanceMatrix))) * aNormal;
}
