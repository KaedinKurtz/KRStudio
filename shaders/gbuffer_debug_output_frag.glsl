#version 450 core

// We only care about writing to one G-Buffer output for this test.
// This MUST match the layout location of your albedo+ao buffer.
layout (location = 2) out vec4 gAlbedoAO;

// The 'in' block must still exist to match the vertex shader interface,
// but we will not use any of its variables.
in VS_OUT {
    vec3 WorldPos;
    vec3 WorldNormal;
} fs_in;

void main()
{
    // This shader does nothing but output a constant, bright magenta color.
    // There are no textures, no uniforms, no calculations.
    // If this does not produce a magenta dragon, the problem is
    // definitively in the OpenGL pipeline state (FBO, blending, etc.),
    // not the shader code.
    gAlbedoAO = vec4(1.0, 0.0, 1.0, 1.0); // Bright Magenta
}