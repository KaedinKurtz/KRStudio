#version 450 core

// --- UNIFORMS ---
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoAO;
uniform sampler2D gMetalRough;
uniform sampler2D gEmissive;

uniform samplerCube irradianceMap;
uniform samplerCube prefilteredEnvMap;
uniform sampler2D   brdfLUT;

uniform vec3 viewPos;
const int MAX_LIGHTS = 8;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int activeLightCount;
uniform int u_hdrEnabled; // 1: output linear HDR (TonemapPass finishes), 0: legacy in-shader Reinhard
uniform float u_iblIntensity; // scales IBL diffuse+specular fill; HDR irradiance is ~PI*sky-radiance, so ~0.3 keeps texture contrast instead of blowing albedo to white
uniform vec3  u_sunDir;       // STATIC directional sun: world-space direction the light TRAVELS (down/forward)
uniform vec3  u_sunColor;     // sun radiance (linear), NO 1/d^2 falloff -> constant in time (no orbit pulse)
uniform mat4  u_invViewProj;  // reconstructs the world-space view ray for background/silhouette fragments
uniform float u_specClamp;    // specular IBL firefly clamp (Settings: render/specFireflyClamp)
uniform int   u_debugView;    // TEMP desaturation probe: 0=normal, 1=albedo, 2=diffuseIBL, 3=specularIBL, 4=ambient, 5=Lo(direct), 6=irradiance, 7=prefiltered
uniform float u_iblSpecScale; // scales ONLY the additive specular IBL. Diffuse IBL is multiplicative (hue-preserving) so it stays as the fill; the specular env term is near-neutral and ADDITIVE, so a high IBL fill floods saturated albedo with grey. Keep this low (~0.15) for matte surfaces.
uniform float u_iblTint;      // 0..1 how much the DIELECTRIC spec-IBL reflection is tinted by albedo (1 = hue-preserving fill, 0 = neutral/physical). High IBL with neutral reflection washes saturated albedo grey.

// --- LTC rectangle AREA LIGHTS (Linearly Transformed Cosines, Heitz 2016) ---
uniform sampler2D ltc_1;      // inverse-transform coefficients (m00,m02,m20,m11 packed in rgba)
uniform sampler2D ltc_2;      // BRDF magnitude (.x) + Fresnel (.y)
const int MAX_AREA_LIGHTS = 8;
struct AreaLight {
    vec3  p0; vec3 p1; vec3 p2; vec3 p3;  // world-space rect corners, CCW seen from the front (+localZ)
    vec3  color;
    float intensity;
    int   twoSided;
};
uniform AreaLight areaLights[MAX_AREA_LIGHTS];
uniform int activeAreaLightCount;

// --- ECS DIRECTIONAL lights (addable; separate from the Settings-driven sun above) ---
const int MAX_DIR_LIGHTS = 4;
uniform vec3 dirLightDirections[MAX_DIR_LIGHTS]; // world-space TRAVEL direction (like u_sunDir)
uniform vec3 dirLightColors[MAX_DIR_LIGHTS];     // radiance = color * intensity (no falloff)
uniform int  activeDirLightCount;

// --- ECS SPOT lights (point light + cone falloff) ---
const int MAX_SPOT_LIGHTS = 8;
struct SpotLight {
    vec3  position;
    vec3  direction;   // normalized cone axis = TRAVEL direction (local +Z of the entity)
    vec3  color;       // radiance = color * intensity (1/d^2 applied in-shader)
    float range;       // soft cutoff radius (0 = pure 1/d^2)
    float cosInner;    // cos(inner half-angle): full intensity inside this cone
    float cosOuter;    // cos(outer half-angle): zero past this cone
};
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];
uniform int activeSpotLightCount;

in vec2 TexCoords;
out vec4 fragColor;

const float PI = 3.14159265359;

// --- PBR HELPER FUNCTIONS (Unchanged) ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness; float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0); float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0); float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0); float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- LTC (Linearly Transformed Cosines) rectangle area-light evaluation ---
// Heitz et al. 2016; the clipless form from three.js (LTC_ClippedSphereFormFactor),
// no horizon switch / no ltc_2.w lookup. The rational-poly in the edge form factor
// already folds in 1/(2*PI), so callers add NO extra /PI.
const float LTC_LUT_SIZE  = 64.0;
const float LTC_LUT_SCALE = (LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE;
const float LTC_LUT_BIAS  = 0.5 / LTC_LUT_SIZE;

vec3 LTC_EdgeVectorFormFactor(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float theta_sintheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * theta_sintheta;
}
float LTC_ClippedSphereFormFactor(vec3 f) {
    float l = length(f);
    return max((l * l + f.z) / (l + 1.0), 0.0);
}
// Integrate the (LTC-transformed) clamped cosine over the rectangle pts[4] from shading
// point P. Returns a scalar form factor (already normalized). One-sided unless twoSided.
float LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 pts[4], bool twoSided) {
    vec3 v1 = pts[1] - pts[0];
    vec3 v2 = pts[3] - pts[0];
    vec3 lightNormal = cross(v1, v2);
    if (dot(lightNormal, P - pts[0]) < 0.0 && !twoSided) return 0.0;  // behind a one-sided panel

    // Tangent aligned with the view. GUARD the degenerate case V ~ N (camera-facing
    // surfaces -> V - N*dot(V,N) collapses to 0, which would zero the whole basis and
    // kill the form factor on every face you look at head-on). Fall back to an arbitrary
    // perpendicular of N there; the LTC lobe is symmetric head-on so orientation is moot.
    vec3 vt = V - N * dot(V, N);
    vec3 T1 = (dot(vt, vt) > 1e-6)
                ? normalize(vt)
                : normalize(cross((abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)), N));
    vec3 T2 = -cross(N, T1);  // three.js-canonical basis handedness; with our CCW corner winding
                              // (lightNormal=+localZ) this makes a one-sided panel light the side it
                              // FACES (an overhead down-facing panel lights tops, not undersides).
    mat3 M = Minv * transpose(mat3(T1, T2, N));

    vec3 L0 = normalize(M * (pts[0] - P));
    vec3 L1 = normalize(M * (pts[1] - P));
    vec3 L2 = normalize(M * (pts[2] - P));
    vec3 L3 = normalize(M * (pts[3] - P));

    vec3 ff = vec3(0.0);
    ff += LTC_EdgeVectorFormFactor(L0, L1);
    ff += LTC_EdgeVectorFormFactor(L1, L2);
    ff += LTC_EdgeVectorFormFactor(L2, L3);
    ff += LTC_EdgeVectorFormFactor(L3, L0);
    return LTC_ClippedSphereFormFactor(ff);
}

// World-space view ray for a fullscreen-pass pixel. Used to sample the env for
// degenerate/background fragments so the silhouette dissolves into the sky
// instead of leaving a near-black 1px coverage band.
vec3 viewRay(vec2 uv) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 wp = u_invViewProj * clip;
    wp /= wp.w;
    return normalize(wp.xyz - viewPos);
}

void main()
{
    // --- 1. RETRIEVE MATERIAL PROPERTIES ---
    vec3 fragPos   = texture(gPosition, TexCoords).rgb;
    vec3 nRaw      = texture(gNormal, TexCoords).rgb;
    // DBG=14: RAW G-buffer normal, sampled BEFORE the silhouette early-out below.
    // Flat (0.5,0.5,0.5) grey == zero normal written by the geometry pass (mesh has
    // no usable normals); a varied colour == real per-pixel normals are present.
    if (u_debugView == 14) { fragColor = vec4(nRaw * 0.5 + 0.5, 1.0); return; }
    // DBG=15: LENGTH of the raw G-buffer normal. black==0 (no normal generated),
    // white==1 (unit normal present, only its DIRECTION is wrong), red(>1)==unnormalized.
    if (u_debugView == 15) { float L=length(nRaw); fragColor = vec4(L, L>1.001?1.0:L, L>1.001?0.0:L, 1.0); return; }
    // Background / silhouette-edge pixels carry a zeroed G-buffer normal. The
    // rasterizer marks a thin (sub-pixel/1px) edge band 'covered' (depth<1.0),
    // so the skybox's LEQUAL test can't overwrite it, and shading a zero normal
    // gives a near-black band. Output the environment along the view ray so the
    // silhouette dissolves into the sky instead of ringing dark.
    if (dot(nRaw, nRaw) < 1e-8) {
        // Scale by u_iblIntensity to match the skybox pass (u_skyNits = IBL nits), else this
        // silhouette fill is ~IBL-scale darker than the real sky -> a dark ring around geometry.
        vec3 sky = textureLod(prefilteredEnvMap, viewRay(TexCoords), 0.0).rgb * u_iblIntensity;
        if (u_hdrEnabled == 0) { sky = sky / (sky + vec3(1.0)); sky = pow(sky, vec3(1.0/2.2)); }
        fragColor = vec4(sky, 1.0);
        return;
    }
    vec3 N         = normalize(nRaw);
    vec3 albedo    = texture(gAlbedoAO, TexCoords).rgb;
    float ao       = texture(gAlbedoAO, TexCoords).a;
    float metallic = texture(gMetalRough, TexCoords).r;
    float roughness= texture(gMetalRough, TexCoords).g;
    vec3 emissive  = texture(gEmissive, TexCoords).rgb;
    
    vec3 V = normalize(viewPos - fragPos);
    // TWO-SIDED NORMAL: the BRep/CAD import (CadImporter) produces inward-facing normals
    // on the robot (verified: NdotV<=0 on visible faces). Diffuse/IBL hide it (they sample
    // *some* direction and return a colour), but the LTC area-light form factor needs a
    // correct outward normal or it integrates to ~0 -> the robot wouldn't receive area
    // light. Flip the shading normal to face the viewer; this is a no-op for correctly-
    // wound meshes (the floor, NdotV>0 already) and corrects inward normals for ALL lights.
    if (dot(N, V) < 0.0) N = -N;
    vec3 R = reflect(-V, N);
    
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // --- 2. DIRECT LIGHTING (Point Lights) ---
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < activeLightCount; ++i) {
        vec3 L = normalize(lightPositions[i] - fragPos);
        vec3 H = normalize(V + L);
        float distance = length(lightPositions[i] - fragPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColors[i] * attenuation;
        
        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);     
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3 specular     = numerator / denominator;
            
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= (1.0 - metallic);
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // --- 2b. DIRECT LIGHTING (Static Directional Sun) ---
    // A fixed key light (no position, no 1/d^2) so brightness is constant in
    // time. This replaces the orbiting point light that caused the periodic
    // whole-arm dimming. activeLightCount=0 disables the point loop above.
    {
        vec3 L = normalize(-u_sunDir);   // toward the sun
        vec3 H = normalize(V + L);
        vec3 radiance = u_sunColor;      // constant: no attenuation

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3 specular     = numerator / denominator;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // --- 2c. DIRECT LIGHTING (ECS Directional lights) ---
    // Same form as the sun: no position, no attenuation; radiance constant over the scene.
    for (int i = 0; i < activeDirLightCount; ++i) {
        vec3 L = normalize(-dirLightDirections[i]);   // toward the light
        vec3 H = normalize(V + L);
        vec3 radiance = dirLightColors[i];

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  numerator   = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3  specular    = numerator / denominator;
        vec3  kD = (vec3(1.0) - F) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // --- 2e. DIRECT LIGHTING (ECS Spot lights) ---
    // Point light + a smooth cone mask between the inner (full) and outer (zero) half-angles.
    for (int i = 0; i < activeSpotLightCount; ++i) {
        vec3  toLight = spotLights[i].position - fragPos;
        float dist    = length(toLight);
        vec3  L       = toLight / max(dist, 1e-4);     // toward the light
        // Cone: angle between the spot's travel axis and the direction TO the fragment (-L).
        float cosTheta = dot(spotLights[i].direction, -L);
        float denomCone = max(spotLights[i].cosInner - spotLights[i].cosOuter, 1e-4);
        float cone = clamp((cosTheta - spotLights[i].cosOuter) / denomCone, 0.0, 1.0);
        cone *= cone;                                   // soften the edge
        if (cone <= 0.0) continue;

        float attenuation = 1.0 / (dist * dist);
        if (spotLights[i].range > 0.0) {                // optional soft range cutoff
            float t = clamp(1.0 - pow(dist / spotLights[i].range, 4.0), 0.0, 1.0);
            attenuation *= t * t;
        }
        vec3 radiance = spotLights[i].color * attenuation * cone;

        vec3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  numerator   = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3  specular    = numerator / denominator;
        vec3  kD = (vec3(1.0) - F) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // TEMP area-light debug capture (shown via u_debugView 8/9/10 below)
    float dbgAreaDiff = 0.0;
    vec3  dbgCorner   = (activeAreaLightCount > 0) ? areaLights[0].p0 : vec3(0.0);
    vec3  dbgFacing   = vec3(0.0);
    if (activeAreaLightCount > 0) {
        vec3 ctr = (areaLights[0].p0 + areaLights[0].p2) * 0.5;   // rect centre
        float fc = dot(N, normalize(ctr - fragPos));
        dbgFacing = vec3(max(-fc, 0.0), max(fc, 0.0), 0.0);       // green=toward panel, red=away
    }

    // --- 2d. RECTANGLE AREA LIGHTS (LTC) ---
    // Physically-based soft area lights. DIFFUSE uses the identity transform (no LUT);
    // SPECULAR warps the cosine lobe by the LTC inverse-matrix from ltc_1 and scales by
    // the split-sum magnitude/Fresnel from ltc_2. Both form factors are already
    // normalized (no extra /PI). This is the area-light behaviour point lights cannot do.
    if (activeAreaLightCount > 0) {
        float ltcNdotV = clamp(dot(N, V), 0.0, 1.0);
        vec2 ltcUV = vec2(roughness, sqrt(1.0 - ltcNdotV));
        ltcUV = ltcUV * LTC_LUT_SCALE + LTC_LUT_BIAS;
        vec4 t1 = texture(ltc_1, ltcUV);
        vec4 t2 = texture(ltc_2, ltcUV);
        mat3 Minv = mat3(
            vec3(t1.x, 0.0, t1.y),
            vec3(0.0,  1.0, 0.0 ),
            vec3(t1.z, 0.0, t1.w)
        );
        vec3 specMag      = F0 * t2.x + (1.0 - F0) * t2.y;   // split-sum scale + Fresnel
        vec3 diffuseColor = albedo * (1.0 - metallic);
        for (int i = 0; i < activeAreaLightCount && i < MAX_AREA_LIGHTS; ++i) {
            vec3 pts[4];
            pts[0] = areaLights[i].p0; pts[1] = areaLights[i].p1;
            pts[2] = areaLights[i].p2; pts[3] = areaLights[i].p3;
            bool ts = areaLights[i].twoSided != 0;
            float diff = LTC_Evaluate(N, V, fragPos, mat3(1.0), pts, ts);
            float spec = LTC_Evaluate(N, V, fragPos, Minv,      pts, ts);
            if (i == 0) dbgAreaDiff = diff;
            Lo += areaLights[i].color * areaLights[i].intensity * (diffuseColor * diff + specMag * spec);
        }
    }

    // --- 3. INDIRECT LIGHTING (Image-Based Lighting) ---
    vec3 F_ambient      = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ambient     = F_ambient;
    vec3 kD_ambient     = (vec3(1.0) - kS_ambient) * (1.0 - metallic);
    
    vec3 irradiance     = texture(irradianceMap, N).rgb;
    // Lambertian diffuse IBL: L_o = (albedo/PI) * E, where the irradiance map now
    // stores the TRUE cosine-weighted irradiance E (= PI*L for a uniform env). The
    // /PI is the Lambertian BRDF normalization; with the corrected bake + this /PI,
    // u_iblIntensity = 1.0 is the physically principled scale (no more compensating crank).
    vec3 diffuseIBL     = kD_ambient * (irradiance / PI) * albedo * u_iblIntensity;

    // --- 4a. GEOMETRIC CORRECTION & COMPOSITING (stabilized) ---
    const float MAX_REFLECTION_LOD = 4.0;

    // 1. Compact, stable curvature estimate
    float curvature = clamp(length(fwidth(N)), 0.0, 4.0);  // clamp: fwidth across a silhouette (valid|NaN normal) otherwise explodes/NaNs

    // 2. Scale & clamp geometric roughness
    const float geoScale = 0.5;      // how strongly edges blur
    const float maxGeo    = 0.3;     // clamp so it never over-blurs
    float geoRough = clamp(curvature * geoScale, 0.0, maxGeo);

    // 3. Clamp view?angle roughness
    float angleTerm = (1.0 - max(dot(N, V), 0.0));
    const float angleScale = 0.1;    // gentle grazing blur
    const float maxAngle  = 0.1;     // clamp to avoid spider-web spikes
    float angleRough = clamp(angleTerm * angleScale, 0.0, maxAngle);

    // 4. Combine with base roughness
    float finalRoughness = clamp(roughness + geoRough + angleRough, 0.0, 1.0);

    // 5. Sample environment with LOD based on new roughness
    float mipLevel = finalRoughness * MAX_REFLECTION_LOD;
    vec3 prefilteredColor = textureLod(prefilteredEnvMap, R, mipLevel).rgb;

    // 6. Reflection tinting & BRDF LUT.
    // For DIELECTRICS, fully tint the prefiltered env reflection by albedo. Physically
    // a dielectric's spec reflection is neutral-white, but with env2's dark horizon the
    // robot's vertical faces are lit almost entirely by this rough sky reflection. A
    // NEUTRAL reflection there dumps grey onto saturated albedo (the washed-out look the
    // user reported). Tinting it by albedo keeps the same illumination/brightness but
    // makes it HUE-PRESERVING -> a red surface reflects red, not grey. (Metals keep their
    // F0=albedo tint via the BRDF term below, so this only changes the dielectric fill.)
    prefilteredColor = mix(prefilteredColor, prefilteredColor * albedo, (1.0 - metallic) * u_iblTint);
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    // Karis environment-BRDF split-sum: scale term uses the FLAT F0, not the full
    // angle-dependent Fresnel (F_ambient, which -> 1 at grazing). Using F_ambient here
    // double-counted Fresnel and inflated dielectric spec-IBL up to ~25x, adding a
    // near-neutral env reflection that floored the dark albedo (the washed-out look).
    vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y) * u_iblIntensity * u_iblSpecScale;

    // Firefly clamp: isolated ultra-bright HDR env texels (e.g. the sun) otherwise saturate
    // single pixels. specularIBL is already scaled by u_iblIntensity (nits), so scale the
    // threshold to match -- otherwise the clamp crushes legitimate metal/glossy reflections.
    float specClampNits = u_specClamp * u_iblIntensity;
    float specLum = dot(specularIBL, vec3(0.2126, 0.7152, 0.0722));
    if (specLum > specClampNits) specularIBL *= specClampNits / specLum;

    // --- DEBUG VIEW (empirical desaturation probe) ---
    // Run with KRS_HDR=0 so TonemapPass skips and this gamma-encoded term hits the
    // screen directly. Lets us see WHICH term (diffuseIBL / specularIBL / Lo) is
    // washing the albedo. u_debugView=0 in normal operation.
    if (u_debugView != 0) {
        vec3 dbg = vec3(0.0);
        if      (u_debugView == 1) dbg = albedo;
        else if (u_debugView == 2) dbg = diffuseIBL;
        else if (u_debugView == 3) dbg = specularIBL;
        else if (u_debugView == 4) dbg = diffuseIBL + specularIBL;
        else if (u_debugView == 5) dbg = Lo;
        else if (u_debugView == 6) dbg = irradiance;
        else if (u_debugView == 7) dbg = prefilteredColor;
        else if (u_debugView == 8) dbg = vec3(dbgAreaDiff);          // area-light diffuse form factor (black = 0)
        else if (u_debugView == 9) dbg = abs(dbgCorner) * 0.15;      // first rect corner (black = not uploaded)
        else if (u_debugView == 10) dbg = dbgFacing;                 // green=faces panel, red=faces away
        else if (u_debugView == 11) dbg = N * 0.5 + 0.5;             // world normal as color
        else if (u_debugView == 12) dbg = fragPos * 0.15 + 0.5;      // world position (readable around origin)
        else if (u_debugView == 13) dbg = vec3(max(dot(N, V), 0.0)); // NdotV: white=faces camera (outward), black=grazing/back
        fragColor = vec4(pow(max(dbg, 0.0), vec3(1.0/2.2)), 1.0);
        return;
    }

    // --- 5. FINAL ASSEMBLY ---
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    // Emissive is stored as (map + color*strength); scale it into nits so it sits in the
    // same physically-based space as the lights and is brought to display by the EV exposure.
    const float EMISSIVE_NITS = 2000.0;
    vec3 color = ambient + Lo + emissive * EMISSIVE_NITS;

    // HDR pipeline: output linear radiance; the TonemapPass at the end of
    // the frame applies ACES + gamma AFTER water/foam composite in linear
    // light. u_hdrEnabled=0 is the KRS_HDR=0 bring-up fallback (legacy
    // in-shader Reinhard).
    if (u_hdrEnabled == 0) {
        color = color / (color + vec3(1.0)); // Reinhard
        color = pow(color, vec3(1.0/2.2));   // gamma
    }

    // Belt-and-suspenders: sanitize any residual NaN/Inf to the environment
    // (not black) so nothing degenerate reaches the screen as a dark speckle.
    if (any(isnan(color)) || any(isinf(color))) {
        vec3 sky = textureLod(prefilteredEnvMap, viewRay(TexCoords), 0.0).rgb * u_iblIntensity;
        if (u_hdrEnabled == 0) { sky = sky / (sky + vec3(1.0)); sky = pow(sky, vec3(1.0/2.2)); }
        color = sky;
    }
    fragColor = vec4(color, 1.0);
}
