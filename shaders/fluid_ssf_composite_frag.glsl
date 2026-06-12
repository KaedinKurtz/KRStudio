#version 430 core

// Pass 4: composite the water surface over the scene.
// Reconstructs view-space normals from the filtered depth, then:
//   refraction   — scene color sampled with a thickness-scaled offset
//   absorption   — per-channel Beer-Lambert from the transmission color
//   scattering   — turbidity fades transmission toward the body color
//   reflection   — Fresnel-weighted prefiltered environment (IBL)
//   specular     — Blinn-Phong key light
// Writes gl_FragDepth so later overlay passes compose correctly.

in vec2 TexCoords;

uniform sampler2D u_fluidDepth;  // filtered view-space z (0 = no fluid)
uniform sampler2D u_thickness;   // metres of water along the ray
uniform sampler2D u_sceneColor;  // scene BEFORE the fluid composite
uniform sampler2D u_sceneDepth;
uniform samplerCube u_prefilteredEnv;

uniform mat4 u_projection;
uniform mat4 u_invView;
uniform vec2 u_texel;        // fluid-depth texel size
uniform vec2 u_projScale;    // proj[0][0], proj[1][1]

uniform vec3 u_waterColor;
uniform float u_turbidity;
uniform float u_emissivity;
uniform float u_ior;
uniform float u_absorptionScale;
uniform float u_refractScale;
uniform int u_debugMode; // 0 off, 1 depth, 2 thickness, 3 normals

out vec4 FragColor;

vec3 viewPosAt(vec2 uv, float viewZ)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * viewZ / u_projScale.x, ndc.y * viewZ / u_projScale.y, -viewZ);
}

void main()
{
    float zc = texture(u_fluidDepth, TexCoords).r;
    if (zc <= 0.0) discard;

    vec3 P = viewPosAt(TexCoords, zc);

    // One-sided central differences: pick the neighbour closer in depth so
    // normals stay sane at silhouettes.
    float zxp = texture(u_fluidDepth, TexCoords + vec2(u_texel.x, 0)).r;
    float zxm = texture(u_fluidDepth, TexCoords - vec2(u_texel.x, 0)).r;
    float zyp = texture(u_fluidDepth, TexCoords + vec2(0, u_texel.y)).r;
    float zym = texture(u_fluidDepth, TexCoords - vec2(0, u_texel.y)).r;

    vec3 ddx = (zxp > 0.0 && (zxm <= 0.0 || abs(zxp - zc) <= abs(zxm - zc)))
        ? viewPosAt(TexCoords + vec2(u_texel.x, 0), zxp) - P
        : P - viewPosAt(TexCoords - vec2(u_texel.x, 0), max(zxm, 0.001));
    vec3 ddy = (zyp > 0.0 && (zym <= 0.0 || abs(zyp - zc) <= abs(zym - zc)))
        ? viewPosAt(TexCoords + vec2(0, u_texel.y), zyp) - P
        : P - viewPosAt(TexCoords - vec2(0, u_texel.y), max(zym, 0.001));

    vec3 N = normalize(cross(ddx, ddy));
    if (N.z < 0.0) N = -N; // face the camera

    float thick = max(texture(u_thickness, TexCoords).r, 0.0);

    if (u_debugMode == 1) { FragColor = vec4(vec3(zc * 0.15), 1.0); gl_FragDepth = 0.0; return; }
    if (u_debugMode == 2) { FragColor = vec4(vec3(thick * 1.5), 1.0); gl_FragDepth = 0.0; return; }
    if (u_debugMode == 3) { FragColor = vec4(N * 0.5 + 0.5, 1.0); gl_FragDepth = 0.0; return; }

    // ---- Refraction: offset shrinks to zero at silhouettes (thin water) ----
    vec2 refrUV = TexCoords + N.xy * thick * u_refractScale;
    // Reject refracted samples that would pull FOREGROUND objects into the
    // water (their depth is closer than the fluid surface).
    float refrSceneZ = texture(u_sceneDepth, refrUV).r;
    vec4 clipP = u_projection * vec4(P, 1.0);
    float fluidWinZ = (clipP.z / clipP.w) * 0.5 + 0.5;
    if (refrSceneZ < fluidWinZ) refrUV = TexCoords;
    vec3 refracted = texture(u_sceneColor, refrUV).rgb;

    // ---- Beer-Lambert absorption (per channel, 1/m) ----
    vec3 sigmaA = -log(clamp(u_waterColor, 0.02, 0.98)) * u_absorptionScale;
    vec3 transmitted = refracted * exp(-sigmaA * thick);

    // ---- Turbidity: single-scatter approximation toward the body color ----
    float scatter = 1.0 - exp(-u_turbidity * 6.0 * thick);
    transmitted = mix(transmitted, u_waterColor * 0.85, scatter);

    // ---- Fresnel-weighted environment reflection ----
    vec3 V = normalize(-P);
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

    color += u_waterColor * u_emissivity;

    gl_FragDepth = fluidWinZ;
    FragColor = vec4(color, 1.0);
}
