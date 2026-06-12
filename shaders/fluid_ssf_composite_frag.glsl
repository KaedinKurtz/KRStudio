#version 430 core

// Pass 4: composite the water surface over the scene.
// Reconstructs view-space normals from the filtered depth, then:
//   refraction   — two-interface Wyman 2005 when the back-face data is
//                  available (High quality): refract at entry, march the
//                  approximate in-water path length d~, project the exit
//                  point. Falls back to the thickness-scaled UV offset.
//   absorption   — per-channel Beer-Lambert from the transmission color
//   scattering   — turbidity fades transmission toward the body color
//   reflection   — Fresnel-weighted prefiltered environment (IBL)
//   specular     — Blinn-Phong key light
//   foam         — contact line where the surface meets geometry + the
//                  world-anchored lingering foam mask (High quality)
// Composites in LINEAR HDR; the TonemapPass applies the display transform.
// Writes gl_FragDepth so later overlay passes compose correctly.

in vec2 TexCoords;

uniform sampler2D u_fluidDepth;  // filtered view-space z (0 = no fluid)
uniform sampler2D u_thickness;   // metres of water along the ray
uniform sampler2D u_sceneColor;  // scene BEFORE the fluid composite
uniform sampler2D u_sceneDepth;
uniform sampler2D u_backData;    // xyz exit normal, w back view-z (High)
uniform sampler2D u_foamMask;    // world-anchored lingering foam (High)
uniform samplerCube u_prefilteredEnv;

uniform mat4 u_projection;
uniform mat4 u_invView;
uniform vec2 u_texel;        // fluid-depth texel size
uniform vec2 u_projScale;    // proj[0][0], proj[1][1]
uniform vec2 u_worldMin;     // fluid domain XZ min (foam mask space)
uniform vec2 u_worldSize;    // fluid domain XZ extent
uniform float u_time;

uniform vec3 u_waterColor;
uniform float u_turbidity;
uniform float u_emissivity;
uniform float u_ior;
uniform float u_absorptionScale;
uniform float u_refractScale;
uniform float u_foamDistance; // contact-line falloff (m)
uniform int u_quality;        // 1 = back-face refraction + foam masks
uniform int u_debugMode; // 0 off, 1 depth, 2 thickness, 3 normals

out vec4 FragColor;

vec3 viewPosAt(vec2 uv, float viewZ)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * viewZ / u_projScale.x, ndc.y * viewZ / u_projScale.y, -viewZ);
}

// Window-space depth -> positive view-space distance.
// ze = -P32/(ndcZ + P22), so the positive distance is P32/(ndcZ + P22).
float linearViewZ(float winZ)
{
    float ndcZ = winZ * 2.0 - 1.0;
    return u_projection[3][2] / (ndcZ + u_projection[2][2]);
}

// Cheap scrolling value noise for foam texture.
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

void main()
{
    float zc = texture(u_fluidDepth, TexCoords).r;
    if (zc <= 0.0) discard;

    vec3 P = viewPosAt(TexCoords, zc);

    // One-sided central differences: pick the neighbour closer in depth so
    // normals stay sane at silhouettes. Sampled 2 texels out — halves the
    // sprite-frequency noise that survives the depth filter.
    vec2 dx = vec2(u_texel.x * 2.0, 0.0);
    vec2 dy = vec2(0.0, u_texel.y * 2.0);
    float zxp = texture(u_fluidDepth, TexCoords + dx).r;
    float zxm = texture(u_fluidDepth, TexCoords - dx).r;
    float zyp = texture(u_fluidDepth, TexCoords + dy).r;
    float zym = texture(u_fluidDepth, TexCoords - dy).r;

    vec3 ddx = (zxp > 0.0 && (zxm <= 0.0 || abs(zxp - zc) <= abs(zxm - zc)))
        ? viewPosAt(TexCoords + dx, zxp) - P
        : P - viewPosAt(TexCoords - dx, max(zxm, 0.001));
    vec3 ddy = (zyp > 0.0 && (zym <= 0.0 || abs(zyp - zc) <= abs(zym - zc)))
        ? viewPosAt(TexCoords + dy, zyp) - P
        : P - viewPosAt(TexCoords - dy, max(zym, 0.001));

    vec3 N = normalize(cross(ddx, ddy));
    if (N.z < 0.0) N = -N; // face the camera

    float thick = max(texture(u_thickness, TexCoords).r, 0.0);

    if (u_debugMode == 1) { FragColor = vec4(vec3(zc * 0.15), 1.0); gl_FragDepth = 0.0; return; }
    if (u_debugMode == 2) { FragColor = vec4(vec3(thick * 1.5), 1.0); gl_FragDepth = 0.0; return; }
    if (u_debugMode == 3) { FragColor = vec4(N * 0.5 + 0.5, 1.0); gl_FragDepth = 0.0; return; }

    vec4 clipP = u_projection * vec4(P, 1.0);
    float fluidWinZ = (clipP.z / clipP.w) * 0.5 + 0.5;
    vec3 V = normalize(-P);

    // ---- Refraction ----------------------------------------------------
    vec2 refrUV;
    float pathLen = thick; // absorption path length through the water
    vec4 back = (u_quality == 1) ? texture(u_backData, TexCoords) : vec4(0.0);
    if (u_quality == 1 && back.w > zc + 1e-4) {
        // Wyman two-interface: refract at the entry surface, walk the
        // approximate underwater distance d~, project the exit point.
        vec3 T1 = refract(-V, N, 1.0 / u_ior);
        float dBack = back.w - zc;          // straight-through gap
        float facing = clamp(dot(N, V), 0.0, 1.0);
        float dTilde = mix(thick, dBack, facing);
        dTilde = clamp(dTilde, 0.0, dBack + 2.0 * thick);
        pathLen = max(dTilde, thick * 0.5);
        vec3 exitP = P + T1 * dTilde * clamp(u_refractScale * 12.5, 0.0, 2.0);
        vec4 clipE = u_projection * vec4(exitP, 1.0);
        refrUV = clamp(clipE.xy / clipE.w * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    } else {
        // Thickness-scaled offset; shrinks to zero at silhouettes. Cap so
        // residual normal noise can't drag in far-away texels.
        vec2 refrOff = N.xy * thick * u_refractScale;
        refrOff = clamp(refrOff, vec2(-0.04), vec2(0.04));
        refrUV = TexCoords + refrOff;
    }
    // Reject refracted samples that would pull FOREGROUND objects into the
    // water (their depth is closer than the fluid surface).
    float refrSceneZ = texture(u_sceneDepth, refrUV).r;
    if (refrSceneZ < fluidWinZ) refrUV = TexCoords;
    vec3 refracted = texture(u_sceneColor, refrUV).rgb;

    // ---- Beer-Lambert absorption (per channel, 1/m) ----
    vec3 sigmaA = -log(clamp(u_waterColor, 0.02, 0.98)) * u_absorptionScale;
    vec3 transmitted = refracted * exp(-sigmaA * pathLen);

    // ---- Turbidity: single-scatter approximation toward the body color ----
    float scatter = 1.0 - exp(-u_turbidity * 6.0 * pathLen);
    transmitted = mix(transmitted, u_waterColor * 0.85, scatter);

    // ---- Fresnel-weighted environment reflection ----
    vec3 Rw = mat3(u_invView) * reflect(-V, N);
    vec3 reflected = textureLod(u_prefilteredEnv, Rw, u_turbidity * 4.0).rgb;

    float f0 = (u_ior - 1.0) / (u_ior + 1.0);
    f0 *= f0;
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);

    vec3 color = mix(transmitted, reflected, clamp(fresnel, 0.0, 1.0));

    // ---- Key-light specular (matches the scene's hardcoded light) ----
    vec3 L = normalize(vec3(0.35, 0.65, 0.45));
    vec3 H = normalize(L + V);
    color += vec3(pow(max(dot(N, H), 0.0), 240.0)) * (0.4 + 2.0 * fresnel);

    // ---- Foam: contact line + lingering surface foam (High quality) ----
    if (u_quality == 1) {
        vec3 worldP = vec3(u_invView * vec4(P, 1.0));
        vec2 foamUV = (worldP.xz - u_worldMin) / u_worldSize;
        float lingering = texture(u_foamMask, foamUV).r;

        float sceneViewZ = linearViewZ(texture(u_sceneDepth, TexCoords).r);
        float gap = max(sceneViewZ - zc, 0.0);
        float contact = 1.0 - smoothstep(0.0, max(u_foamDistance, 1e-3), gap);

        float n = vnoise(worldP.xz * 28.0 + vec2(u_time * 0.35, -u_time * 0.22))
                  * 0.6 + vnoise(worldP.xz * 9.0 - vec2(u_time * 0.12)) * 0.4;
        float foamAmt = clamp(contact * 0.7 + lingering, 0.0, 0.9);
        foamAmt *= smoothstep(0.18, 0.75, n * (0.5 + 0.5 * foamAmt));
        // Foam albedo in linear HDR: bright but lit by the environment.
        vec3 foamColor = vec3(1.15);
        color = mix(color, foamColor, foamAmt);
    }

    color += u_waterColor * u_emissivity;

    gl_FragDepth = fluidWinZ;
    FragColor = vec4(color, 1.0);
}
