#version 450 core

// A simple 2-triangle quad in NDC with corresponding UVs
layout(location = 0) in vec2 aPos;      // e.g. (-1,-1),(+1,-1),(-1,+1),(+1,+1)
layout(location = 1) in vec2 aTexCoord; // (0,0),(1,0),(0,1),(1,1)

out vec2 TexCoords;

void main() {
    TexCoords   = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
