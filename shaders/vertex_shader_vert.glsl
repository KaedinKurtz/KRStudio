// =================================================================
//                      vertex_shader.glsl
// =================================================================
#version 330 core

// We explicitly define the inputs the shader expects from the C++ side.
// Location 0: The vertex's position in model space.
// Location 1: The vertex's normal vector in model space.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

// Uniforms set from the C++ application
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Data to be passed to the fragment shader
out vec3 FragPos;   // The vertex position transformed into world space
out vec3 Normal;    // The normal vector transformed into world space

void main()
{
    // Transform the vertex position and normal vector into world space.
    FragPos = vec3(model * vec4(aPos, 1.0));
    // Use the normal matrix to correctly transform normals (handles non-uniform scaling).
    Normal = mat3(transpose(inverse(model))) * aNormal;

    // Calculate the final clip-space position of the vertex.
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
