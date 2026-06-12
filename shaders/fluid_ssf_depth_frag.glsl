#version 430 core

// Pass 1 of screen-space fluids: write the view-space depth of the sphere
// surface (R32F) + hardware depth for particle-particle occlusion.

in vec3 vViewCenter;
in float vRadius;
in float vLife;

uniform mat4 u_projection;
uniform sampler2D u_sceneDepth; // target-resolution scene depth
uniform vec2 u_invTargetSize;

layout(location = 0) out float outViewZ;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    vec3 n = vec3(uv.x, -uv.y, sqrt(1.0 - r2));
    vec3 viewPos = vViewCenter + n * vRadius;

    vec4 clip = u_projection * vec4(viewPos, 1.0);
    float winZ = (clip.z / clip.w) * 0.5 + 0.5;

    // Hide fluid behind opaque scene geometry.
    float sceneZ = texture(u_sceneDepth, gl_FragCoord.xy * u_invTargetSize).r;
    if (winZ >= sceneZ) discard;

    gl_FragDepth = winZ;
    outViewZ = -viewPos.z; // positive metres in front of the camera
}
