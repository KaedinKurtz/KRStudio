#version 430 core
// -----------------------------------------------------------------------------
// Generates a full-screen triangle with proper UVs (no VBO required).
// -----------------------------------------------------------------------------
out vec2 vUV;                // passed to the fragment shader

void main()
{
    // Vertex pattern:  (0,0)  (2,0)  (0,2)
    vec2 pos = vec2( (gl_VertexID << 1) & 2,
                     (gl_VertexID      ) & 2 );

    vUV         = pos;              // 0 1 texture coords
    gl_Position = vec4(pos * 2.0 - 1.0,   // 0/2 1/ 3 in NDC
                       0.0,
                       1.0);
}