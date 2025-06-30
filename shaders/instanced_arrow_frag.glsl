#version 410 core

// Input from the vertex shader
in VS_OUT {
    vec3 color;
} fs_in;

// Final output color
out vec4 FragColor;

void main()
{
    // The color is simply the per-instance color passed from the vertex shader
    FragColor = vec4(fs_in.color, 1.0);
}
