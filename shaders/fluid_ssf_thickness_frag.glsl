#version 430 core

// Pass 3: additive thickness — the chord length of each particle sphere
// (or fitted ellipsoid, u_aniso=1) along the view ray, accumulated with
// additive blending (half resolution).

in vec3 vViewCenter;
in float vRadius;
in float vLife;
flat in mat3 vInvM;

uniform mat4 u_projection;
uniform sampler2D u_sceneDepth;
uniform vec2 u_invTargetSize; // of the THICKNESS target
uniform int u_aniso;

layout(location = 0) out float outThickness;

void main()
{
    float chord;

    if (u_aniso == 1) {
        vec2 ndc = gl_FragCoord.xy * u_invTargetSize * 2.0 - 1.0;
        vec3 d = normalize(vec3(ndc.x / u_projection[0][0], ndc.y / u_projection[1][1], -1.0));
        vec3 dd = vInvM * d;
        vec3 u0 = -(vInvM * vViewCenter);
        float a = dot(dd, dd);
        float b = 2.0 * dot(u0, dd);
        float c = dot(u0, u0) - 1.0;
        float disc = b * b - 4.0 * a * c;
        if (disc <= 0.0) discard;
        chord = sqrt(disc) / a; // t2 - t1 along the normalised ray
    } else {
        vec2 uv = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(uv, uv);
        if (r2 > 1.0) discard;
        chord = 2.0 * vRadius * sqrt(1.0 - r2);
    }

    // Occlusion test against the particle centre.
    vec4 clip = u_projection * vec4(vViewCenter, 1.0);
    float winZ = (clip.z / clip.w) * 0.5 + 0.5;
    float sceneZ = texture(u_sceneDepth, gl_FragCoord.xy * u_invTargetSize).r;
    if (winZ >= sceneZ) discard;

    outThickness = chord * 0.7; // packing correction
}
