#version 410 core

out vec4 FragColor;

// A uniform to control the glow color from C++
uniform vec3 glowColor;

void main()
{
    // Simply output the solid glow color.
    // The "glow" effect comes from how we blend it in the C++ code.
    FragColor = vec4(glowColor, 1.0);
}
