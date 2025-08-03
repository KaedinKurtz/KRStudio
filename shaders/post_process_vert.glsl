#version 430 core

// This shader takes no vertex attributes as input.
// It generates all data based on the vertex ID.

out vec2 TexCoords; // Pass texture coordinates to the fragment shader.

void main()
{
    // A clever trick to generate 3 vertices forming a triangle that
    // covers the entire screen in normalized device coordinates (NDC).
    // The bitwise operations generate the corners (-1,-1), (3,-1), and (-1,3).
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;

    // Directly output the clip-space position of the vertex.
    gl_Position = vec4(x, y, 0.0, 1.0);

    // Calculate the corresponding texture coordinate for this vertex.
    // The math maps the large triangle's corners back to the [0,1] UV space.
    TexCoords = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
}