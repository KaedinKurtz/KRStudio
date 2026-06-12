#version 430 core

// Pass 1 of screen-space fluids: write the view-space depth of the sphere
// (or fitted ellipsoid, u_aniso=1) surface (R32F) + hardware depth for
// particle-particle occlusion.

in vec3 vViewCenter;
in float vRadius;
in float vLife;
flat in mat3 vInvM;

uniform mat4 u_projection;
uniform sampler2D u_sceneDepth; // target-resolution scene depth
uniform vec2 u_invTargetSize;
uniform int u_aniso;

layout(location = 0) out float outViewZ;

void main()
{
    vec3 viewPos;

    if (u_aniso == 1) {
        // Ray-trace the ellipsoid: camera ray through this fragment,
        // transformed into the unit-sphere space carried by vInvM.
        vec2 ndc = gl_FragCoord.xy * u_invTargetSize * 2.0 - 1.0;
        vec3 d = normalize(vec3(ndc.x / u_projection[0][0], ndc.y / u_projection[1][1], -1.0));
        vec3 dd = vInvM * d;
        vec3 u0 = -(vInvM * vViewCenter);
        float a = dot(dd, dd);
        float b = 2.0 * dot(u0, dd);
        float c = dot(u0, u0) - 1.0;
        float disc = b * b - 4.0 * a * c;
        if (disc <= 0.0) discard;
        float t = (-b - sqrt(disc)) / (2.0 * a);
        if (t <= 0.0) discard;
        viewPos = t * d;
    } else {
        vec2 uv = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(uv, uv);
        if (r2 > 1.0) discard;
        vec3 n = vec3(uv.x, -uv.y, sqrt(1.0 - r2));
        viewPos = vViewCenter + n * vRadius;
    }

    vec4 clip = u_projection * vec4(viewPos, 1.0);
    float winZ = (clip.z / clip.w) * 0.5 + 0.5;

    // Hide fluid behind opaque scene geometry.
    float sceneZ = texture(u_sceneDepth, gl_FragCoord.xy * u_invTargetSize).r;
    if (winZ >= sceneZ) discard;

    gl_FragDepth = winZ;
    outViewZ = -viewPos.z; // positive metres in front of the camera
}
