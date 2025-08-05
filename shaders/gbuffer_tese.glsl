#version 450 core
layout(triangles, equal_spacing, cw) in;

// Inputs from TCS
in  vec3 TCS_FragPos_TS[];
in  vec3 TCS_ViewPos_TS[];
in  mat3 TCS_TBN[];
in  vec2 TCS_TexCoords[];

// Outputs to Fragment
out vec3 FragPos_TangentSpace;
out vec3 ViewPos_TangentSpace;
out mat3 TBN;
out vec2 TexCoords;

// uniforms
uniform sampler2D heightMap;
uniform float      heightScale;
uniform mat4       view;
uniform mat4       projection;

void main()
{
    // barycentric coordinates in the quad
    vec2 bc = gl_TessCoord.xy;

    // interpolate tangent-space pos & viewPos
    vec3 p0 = mix(TCS_FragPos_TS[0], TCS_FragPos_TS[1], bc.x);
    vec3 p1 = mix(TCS_FragPos_TS[3], TCS_FragPos_TS[2], bc.x);
    vec3 posTS = mix(p0, p1, bc.y);

    vec3 v0 = mix(TCS_ViewPos_TS[0], TCS_ViewPos_TS[1], bc.x);
    vec3 v1 = mix(TCS_ViewPos_TS[3], TCS_ViewPos_TS[2], bc.x);
    vec3 viewTS = mix(v0, v1, bc.y);

    // interpolate UV & a single TBN
    vec2 uv0 = mix(TCS_TexCoords[0], TCS_TexCoords[1], bc.x);
    vec2 uv1 = mix(TCS_TexCoords[3], TCS_TexCoords[2], bc.x);
    vec2 uv   = mix(uv0, uv1, bc.y);
    mat3 tbn  = TCS_TBN[0];

    // displacement along local normal
    float h   = texture(heightMap, uv).r * heightScale;
    posTS    += tbn * vec3(0.0, 0.0, h);

    // emit varyings
    FragPos_TangentSpace = posTS;
    ViewPos_TangentSpace = viewTS;
    TBN                   = tbn;
    TexCoords             = uv;

    // reconstruct world-pos and project
    vec3 worldPos = transpose(tbn) * posTS;
    gl_Position   = projection * view * vec4(worldPos, 1.0);
}
