#version 450 core

// Tell the TCS to emit 3 control?points per patch (triangles)
layout(vertices = 3) out;

// — Inputs from VS —
in vec3 VS_FragPos_TS[];
in vec3 VS_ViewPos_TS[];
in mat3 VS_TBN[];
in vec2 VS_TexCoords[];

// — Outputs to TES —
out vec3 TCS_FragPos_TS[];
out vec3 TCS_ViewPos_TS[];
out mat3 TCS_TBN[];
out vec2 TCS_TexCoords[];

// Tessellation levels (inner and outer)
uniform float tessLevelInner;
uniform float tessLevelOuter;

void main()
{
    // 1) Forward all per?vertex varyings
    TCS_FragPos_TS[gl_InvocationID] = VS_FragPos_TS[gl_InvocationID];
    TCS_ViewPos_TS[gl_InvocationID] = VS_ViewPos_TS[gl_InvocationID];
    TCS_TBN[      gl_InvocationID]  = VS_TBN[      gl_InvocationID];
    TCS_TexCoords[gl_InvocationID]  = VS_TexCoords[gl_InvocationID];

    // 2) Only the first invocation sets tess levels
    if (gl_InvocationID == 0) {
        // Inner level for triangles has one component
        gl_TessLevelInner[0]  = tessLevelInner;
        // Outer levels: one per edge of the triangle
        gl_TessLevelOuter[0]  = tessLevelOuter;
        gl_TessLevelOuter[1]  = tessLevelOuter;
        gl_TessLevelOuter[2]  = tessLevelOuter;
    }

    // 3) Pass through clip?space position
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
