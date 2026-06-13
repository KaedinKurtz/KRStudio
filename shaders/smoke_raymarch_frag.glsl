#version 430 core

// Volumetric ray-march of the gas grid, composited in linear HDR over the
// lit scene (before the tonemap). Front-to-back accumulation with
// Beer-Lambert extinction; a short light-march gives soft self-shadowing;
// temperature drives blackbody emission for fire. Output is PREMULTIPLIED
// colour + coverage alpha, blended with (GL_ONE, GL_ONE_MINUS_SRC_ALPHA).

in vec2 TexCoords;

uniform sampler3D u_scalars;   // r density, g temperature, b fuel
uniform sampler2D u_sceneDepth;

uniform mat4 u_invViewProj;
uniform vec3 u_camPos;
uniform vec3 u_origin;         // smoke AABB min (world)
uniform vec3 u_size;           // smoke AABB extent (world)
uniform vec3 u_smokeColor;
uniform vec3 u_lightDir;       // FROM light toward scene (normalized)
uniform float u_densityScale;
uniform int u_steps;
uniform int u_fireEnabled;
uniform int u_debug; // 1 = render a faint constant fog filling the box

out vec4 FragColor;

vec3 unproject(vec2 ndc, float z)
{
    vec4 w = u_invViewProj * vec4(ndc, z, 1.0);
    return w.xyz / w.w;
}

// AABB slab test; returns (tNear, tFar), tFar<tNear => miss.
vec2 intersectBox(vec3 ro, vec3 rd, vec3 lo, vec3 hi)
{
    vec3 inv = 1.0 / rd;
    vec3 t0 = (lo - ro) * inv;
    vec3 t1 = (hi - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

float densityAt(vec3 wp)
{
    vec3 uvw = (wp - u_origin) / u_size;
    return texture(u_scalars, uvw).r;
}

// Warm blackbody-ish ramp for fire (temperature 0..~6).
vec3 blackbody(float t)
{
    t = clamp(t * 0.32, 0.0, 1.6);
    vec3 c = mix(vec3(0.7, 0.06, 0.0),    // dull red
                 vec3(1.3, 0.5, 0.08), clamp(t, 0.0, 1.0)); // orange
    c = mix(c, vec3(1.6, 1.35, 0.7), clamp(t - 1.0, 0.0, 1.0)); // yellow-white
    return c * t * 3.0;
}

void main()
{
    vec2 ndc = TexCoords * 2.0 - 1.0;
    vec3 nearW = unproject(ndc, -1.0);
    vec3 farW = unproject(ndc, 1.0);
    vec3 rd = normalize(farW - nearW);

    vec2 t = intersectBox(u_camPos, rd, u_origin, u_origin + u_size);
    if (t.y <= max(t.x, 0.0)) discard;
    float tNear = max(t.x, 0.0);
    float tFar = t.y;

    // Stop the march at opaque geometry.
    float sd = texture(u_sceneDepth, TexCoords).r;
    if (sd < 1.0) {
        vec3 sceneW = unproject(ndc, sd * 2.0 - 1.0);
        float tScene = dot(sceneW - u_camPos, rd);
        tFar = min(tFar, tScene);
    }
    if (tFar <= tNear) discard;

    int steps = max(u_steps, 8);
    float stepLen = (tFar - tNear) / float(steps);
    // Jitter the start to break up banding.
    float jitter = fract(sin(dot(TexCoords, vec2(12.9898, 78.233))) * 43758.5453);
    float tcur = tNear + jitter * stepLen;

    vec3 accum = vec3(0.0);
    float trans = 1.0;

    for (int i = 0; i < steps; ++i) {
        vec3 wp = u_camPos + rd * tcur;
        vec4 s = texture(u_scalars, (wp - u_origin) / u_size);
        float dens = s.r;
        if (u_debug == 2) dens = max(dens, 0.08); // fill box to prove the march
        if (dens > 0.002) {
            float ext = dens * u_densityScale * stepLen;
            float a = 1.0 - exp(-ext);

            // Soft self-shadow: a few taps toward the light.
            float shadow = 1.0;
            float ls = stepLen * 2.0;
            float occ = 0.0;
            for (int l = 1; l <= 4; ++l) {
                vec3 lp = wp - u_lightDir * (ls * float(l));
                occ += densityAt(lp);
            }
            shadow = exp(-occ * u_densityScale * ls * 0.6);

            vec3 lit = u_smokeColor * (0.25 + 0.95 * shadow);
            if (u_fireEnabled == 1) lit += blackbody(s.g);

            accum += trans * a * lit;
            trans *= (1.0 - a);
            if (trans < 0.01) break;
        }
        tcur += stepLen;
    }

    float alpha = 1.0 - trans;
    if (alpha < 0.003) discard;
    FragColor = vec4(accum, alpha); // premultiplied
}
