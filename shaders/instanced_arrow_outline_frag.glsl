#version 410 core

in vec3 fColor; // Receives color from the Geometry Shader
out vec4 FragColor;

void main()
{
    FragColor = vec4(fColor, 1.0);
}
