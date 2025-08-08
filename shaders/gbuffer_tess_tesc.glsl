#version 430 core
layout (vertices = 3) out;

// Inputs match the VS outputs by location index
layout (location = 0) in vec2 tcs_in_TexCoords[];
layout (location = 1) in vec3 tcs_in_Normal[];

// Outputs use the same locations to pass data to the TES
layout (location = 0) out vec2 tcs_out_TexCoords[];
layout (location = 1) out vec3 tcs_out_Normal[];

uniform vec3 viewPos;
uniform float minTess;
uniform float maxTess;
uniform float maxDist;

float GetTessLevel(float distance)
{
    float clampedDist = clamp(distance, 0.0, maxDist);
    return mix(maxTess, minTess, clampedDist / maxDist);
}

void main()
{
    // Pass through the built-in position
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    // Pass through our custom attributes
    tcs_out_TexCoords[gl_InvocationID] = tcs_in_TexCoords[gl_InvocationID];
    tcs_out_Normal[gl_InvocationID] = tcs_in_Normal[gl_InvocationID];

    // Calculate tessellation levels using the world position from the built-in gl_in
    if (gl_InvocationID == 0)
    {
        float d0 = distance(viewPos, gl_in[0].gl_Position.xyz);
        float d1 = distance(viewPos, gl_in[1].gl_Position.xyz);
        float d2 = distance(viewPos, gl_in[2].gl_Position.xyz);

        gl_TessLevelOuter[0] = GetTessLevel(d1);
        gl_TessLevelOuter[1] = GetTessLevel(d2);
        gl_TessLevelOuter[2] = GetTessLevel(d0);
        gl_TessLevelInner[0] = gl_TessLevelOuter[0];
    }
}