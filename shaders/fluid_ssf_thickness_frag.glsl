#version 430 core

// Pass 3: additive thickness — the chord length of each particle sphere
// along the view ray, accumulated with additive blending (half resolution).

in vec3 vViewCenter;
in float vRadius;
in float vLife;

uniform mat4 u_projection;
uniform sampler2D u_sceneDepth;
uniform vec2 u_invTargetSize; // of the THICKNESS target

layout(location = 0) out float outThickness;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    // Occlusion test against the sphere centre.
    vec4 clip = u_projection * vec4(vViewCenter, 1.0);
    float winZ = (clip.z / clip.w) * 0.5 + 0.5;
    float sceneZ = texture(u_sceneDepth, gl_FragCoord.xy * u_invTargetSize).r;
    if (winZ >= sceneZ) discard;

    outThickness = 2.0 * vRadius * sqrt(1.0 - r2) * 0.7; // packing correction
}
