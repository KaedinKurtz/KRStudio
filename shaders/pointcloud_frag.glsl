#version 430 core

out vec4 FragColor;

in vec2 TexCoord; // ADDED: Input from the vertex shader

uniform sampler2D texture_color; // ADDED: The color image texture

void main()
{
    // Sample the color from the texture at the given coordinate
    FragColor = texture(texture_color, TexCoord);
}
