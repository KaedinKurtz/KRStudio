#version 430 core
layout (vertices = 1) out;

void main()
{
    /* one patch = the whole curve let TE control t-subdivision */
    gl_out[0].gl_Position = vec4(0.0);           // dummy
    gl_TessLevelOuter[0]  = 32.0;                // 32 segments
}