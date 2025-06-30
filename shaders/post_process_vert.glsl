#version 410 core
// -----------------------------------------------------------------------------
// Generates a full-screen triangle with proper UVs.
// No attributes or VBOs required – we build everything from gl_VertexID.
// -----------------------------------------------------------------------------
out vec2 vUV;                // pass to the fragment stage

void main()
{
    // (0,0)-(2,0)-(0,2) pattern → covers the whole NDC after the *2-1 trick*
    vec2 pos = vec2( (gl_VertexID << 1) & 2,
                      (gl_VertexID      ) & 2 );

    vUV         = pos;                   // 0-to-1 texture coordinates
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
