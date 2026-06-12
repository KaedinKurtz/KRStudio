#version 430 core

// Back-face pass for two-interface (Wyman 2005) refraction: with the depth
// test set to GL_GREATER (buffer cleared to 0) this keeps the FARTHEST
// fluid surface along each ray. Outputs the exit normal + view-z.

in vec3 vViewCenter;
in float vRadius;
in float vLife;
flat in mat3 vInvM;

uniform mat4 u_projection;
uniform sampler2D u_sceneDepth;
uniform vec2 u_invTargetSize;
uniform int u_aniso;

layout(location = 0) out vec4 outBack; // xyz exit normal (view), w view-z

void main()
{
    vec3 viewPos;
    vec3 nExit;

    if (u_aniso == 1) {
        vec2 ndc = gl_FragCoord.xy * u_invTargetSize * 2.0 - 1.0;
        vec3 dir = normalize(vec3(ndc.x / u_projection[0][0], ndc.y / u_projection[1][1], -1.0));
        vec3 dd = vInvM * dir;
        vec3 u0 = -(vInvM * vViewCenter);
        float a = dot(dd, dd);
        float b = 2.0 * dot(u0, dd);
        float c = dot(u0, u0) - 1.0;
        float disc = b * b - 4.0 * a * c;
        if (disc <= 0.0) discard;
        float t = (-b + sqrt(disc)) / (2.0 * a); // FAR root
        if (t <= 0.0) discard;
        viewPos = t * dir;
        // Unit-sphere normal back-transformed: n ∝ M^-T * u(t).
        vec3 uHit = t * dd + u0;
        nExit = normalize(transpose(vInvM) * uHit);
    } else {
        vec2 uv = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(uv, uv);
        if (r2 > 1.0) discard;
        vec3 n = vec3(uv.x, -uv.y, -sqrt(1.0 - r2)); // far hemisphere
        viewPos = vViewCenter + n * vRadius;
        nExit = n;
    }

    vec4 clip = u_projection * vec4(viewPos, 1.0);
    float winZ = (clip.z / clip.w) * 0.5 + 0.5;

    // Exits hidden behind opaque geometry stop at that geometry instead.
    float sceneZ = texture(u_sceneDepth, gl_FragCoord.xy * u_invTargetSize).r;
    if (winZ >= sceneZ) discard;

    gl_FragDepth = winZ;
    outBack = vec4(nExit, -viewPos.z);
}
