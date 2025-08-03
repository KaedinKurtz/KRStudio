#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Transform vertex position to world space and pass to fragment shader
    FragPos = vec3(model * vec4(aPos, 1.0));
    
    // Transform normal to world space and pass to fragment shader
    // We use the transpose of the inverse of the model matrix to correctly handle non-uniform scaling.
    Normal = mat3(transpose(inverse(model))) * aNormal;
    
    // Final clip space position
    gl_Position = projection * view * vec4(FragPos, 1.0);
}