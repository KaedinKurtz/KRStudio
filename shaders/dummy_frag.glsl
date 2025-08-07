#version 450 core

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;
layout(location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;
layout(location = 5) out vec2 gMotion;

void main()
{
    // Write a constant, unique color to each buffer.
    gPosition   = vec4(1.0, 0.0, 0.0, 1.0); // Red
    gNormal     = vec4(0.0, 1.0, 0.0, 1.0); // Green
    gAlbedoAO   = vec4(0.0, 0.0, 1.0, 1.0); // Blue
    gMetalRough = vec4(1.0, 1.0, 0.0, 1.0); // Yellow
    gEmissive   = vec4(1.0, 0.0, 1.0, 1.0); // Magenta
    gMotion     = vec2(0.5, 0.5);           // Grey
}