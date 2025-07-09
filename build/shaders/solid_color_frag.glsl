#version 410 core

// The final color that will be written to the framebuffer.
out vec4 FragColor;

// A uniform to define the outline color. We can set this from the C++ code.
uniform vec3 outlineColor;

void main()
{
    // This shader's only job is to output the specified solid color.
    FragColor = vec4(outlineColor, 1.0);
}
