#version 430 core

// A single attribute: 2?component position (we'll embed texcoords in the triangle)
layout(location = 0) in vec2 aPos;

out vec2 TexCoords;

void main() {
    // Positions for a fullscreen triangle (NDC)
    // we can reconstruct TexCoords from gl_Position.xy * 0.5 + 0.5
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoords   = aPos * 0.5 + 0.5;
}
