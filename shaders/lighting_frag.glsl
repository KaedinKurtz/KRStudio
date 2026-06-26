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
const int MAX_LIGHTS = 4;
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
    // Background / silhouette-edge pixels carry a zeroed G-buffer normal. The
    // rasterizer marks a thin (sub-pixel/1px) edge band 'covered' (depth<1.0),
    // so the skybox's LEQUAL test can't overwrite it, and shading a zero normal
    // gives a near-black band. Output the environment along the view ray so the
    // silhouette dissolves into the sky instead of ringing dark.
    if (dot(nRaw, nRaw) < 1e-8) {
        vec3 sky = textureLod(prefilteredEnvMap, viewRay(TexCoords), 0.0).rgb;
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

    // --- 3. INDIRECT LIGHTING (Image-Based Lighting) ---
    vec3 F_ambient      = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ambient     = F_ambient;
    vec3 kD_ambient     = (vec3(1.0) - kS_ambient) * (1.0 - metallic);
    
    vec3 irradiance     = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL     = kD_ambient * irradiance * albedo * u_iblIntensity;

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

    // Firefly clamp: isolated ultra-bright HDR env texels (e.g. the sun)
    // otherwise saturate single pixels to white through the tonemapper.
    float specLum = dot(specularIBL, vec3(0.2126, 0.7152, 0.0722));
    if (specLum > u_specClamp) specularIBL *= u_specClamp / specLum;

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
        fragColor = vec4(pow(max(dbg, 0.0), vec3(1.0/2.2)), 1.0);
        return;
    }

    // --- 5. FINAL ASSEMBLY ---
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    vec3 color = ambient + Lo + emissive*10;

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
        vec3 sky = textureLod(prefilteredEnvMap, viewRay(TexCoords), 0.0).rgb;
        if (u_hdrEnabled == 0) { sky = sky / (sky + vec3(1.0)); sky = pow(sky, vec3(1.0/2.2)); }
        color = sky;
    }
    fragColor = vec4(color, 1.0);
}
