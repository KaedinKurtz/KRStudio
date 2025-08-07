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

void main()
{
    // --- 1. RETRIEVE MATERIAL PROPERTIES ---
    vec3 fragPos   = texture(gPosition, TexCoords).rgb;
    vec3 N         = normalize(texture(gNormal, TexCoords).rgb);
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
    
    // --- 3. INDIRECT LIGHTING (Image-Based Lighting) ---
    vec3 F_ambient      = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ambient     = F_ambient;
    vec3 kD_ambient     = (vec3(1.0) - kS_ambient) * (1.0 - metallic);
    
    vec3 irradiance     = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL     = kD_ambient * irradiance * albedo;

    // --- 4a. GEOMETRIC CORRECTION & COMPOSITING (stabilized) ---
    const float MAX_REFLECTION_LOD = 4.0;

    // 1. Compact, stable curvature estimate
    float curvature = length(fwidth(N));    

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

    // 6. Reflection tinting & BRDF LUT as before
    prefilteredColor = mix(prefilteredColor, prefilteredColor * albedo, (1.0 - metallic) * 0.5);
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_ambient * brdf.x + brdf.y);
    
    // --- 5. FINAL ASSEMBLY ---
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    vec3 color = ambient * 1.5 + Lo + emissive;

    // Post-processing
    color = color / (color + vec3(1.0)); // Tonemapping
    color = pow(color, vec3(1.0/2.2));   // Gamma correction 
    
    fragColor = vec4(color, 1.0);
}
