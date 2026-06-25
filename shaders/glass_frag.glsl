#version 430 core

// Screen-space refractive glass: refract the view ray at the front surface,
// walk a virtual thickness, project the exit point back to screen and
// sample the pre-glass scene. Fresnel-weighted prefiltered env reflection
// on top, Beer-Lambert tint through the body, optional chromatic
// dispersion (three slightly different IORs). Runs in linear HDR before
// the tonemap.

in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform sampler2D u_sceneColor; // scene BEFORE the glass
uniform sampler2D u_sceneDepth;
uniform samplerCube u_prefilteredEnv;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_camPos;
uniform float u_ior;
uniform vec3 u_tint;
uniform float u_thickness;
uniform float u_dispersion;
uniform float u_roughness;

out vec4 FragColor;

vec2 projectToUV(vec3 worldPos)
{
    vec4 clip = projection * view * vec4(worldPos, 1.0);
    return clamp(clip.xy / clip.w * 0.5 + 0.5, vec2(0.001), vec2(0.999));
}

vec3 sampleRefracted(vec3 V, vec3 N, float ior)
{
    vec3 refr = refract(-V, N, 1.0 / ior);
    if (dot(refr, refr) < 1e-6) refr = reflect(-V, N); // TIR fallback
    vec2 uv = projectToUV(vWorldPos + refr * u_thickness);
    // Don't pull FOREGROUND pixels through the glass.
    vec2 here = projectToUV(vWorldPos);
    float sceneZ = texture(u_sceneDepth, uv).r;
    vec4 clipHere = projection * view * vec4(vWorldPos, 1.0);
    float hereZ = clipHere.z / clipHere.w * 0.5 + 0.5;
    if (sceneZ < hereZ) uv = here;
    return texture(u_sceneColor, uv).rgb;
}

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(u_camPos - vWorldPos);
    if (dot(N, V) < 0.0) N = -N; // double-sided

    vec3 transmitted;
    if (u_dispersion > 1e-5) {
        transmitted = vec3(sampleRefracted(V, N, u_ior - u_dispersion).r,
                           sampleRefracted(V, N, u_ior).g,
                           sampleRefracted(V, N, u_ior + u_dispersion).b);
    } else {
        transmitted = sampleRefracted(V, N, u_ior);
    }
    transmitted *= u_tint;

    vec3 R = reflect(-V, N);
    // Clamp reflected radiance — belt-and-suspenders against firefly patches from
    // an over-bright env reflection (this pass runs in linear HDR before tonemap).
    vec3 reflected = min(textureLod(u_prefilteredEnv, R, u_roughness * 4.0).rgb, vec3(8.0));

    float f0 = (u_ior - 1.0) / (u_ior + 1.0);
    f0 *= f0;
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);

    FragColor = vec4(mix(transmitted, reflected, clamp(fresnel, 0.0, 1.0)), 1.0);
}
