#version 430 core

in vec4 fragColor;
out vec4 outColor;

void main()
{
    // Create a soft, round particle instead of a harsh square
    if (length(gl_PointCoord - vec2(0.5)) > 0.5) {
        discard;
    }
    outColor = fragColor;
}
