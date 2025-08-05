#version 450 core

// --- UNIFORMS ---
// G-Buffer textures
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoAO;
uniform sampler2D gMetalRough;
uniform sampler2D gEmissive;

// IBL resources
uniform samplerCube irradianceMap;
uniform samplerCube prefilteredEnvMap;
uniform sampler2D   brdfLUT;

// Lights & camera
uniform vec3 viewPos;
const int MAX_LIGHTS = 4;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int activeLightCount;

in vec2 TexCoords;
out vec4 fragColor;

const float PI = 3.14159265359;

// --- PBR HELPER FUNCTIONS ---
// These model the physical properties of how light interacts with a surface.
//-------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


void main()
{
    // --- 1. RETRIEVE MATERIAL PROPERTIES FROM G-BUFFER ---
    vec3 N          = normalize(texture(gNormal, TexCoords).rgb);
    if (length(N) < 0.1) { discard; }

    vec3 fragPos    = texture(gPosition, TexCoords).rgb;
    vec4 albedoAO   = texture(gAlbedoAO, TexCoords);
    vec3 albedo     = albedoAO.rgb;
    float ao        = albedoAO.a;
    vec2 mr         = texture(gMetalRough, TexCoords).rg;
    float metallic  = mr.r;
    float roughness = mr.g;
    vec3 emissive   = texture(gEmissive, TexCoords).rgb;

    // --- DEBUG OVERRIDES ---
    // Uncomment these lines to force the material to be something different.
    // This is great for testing the lighting response.
    //
    // Force a dielectric (non-metal) with high roughness to see diffuse IBL:
    // metallic = 0.0;
    // roughness = 0.8;
    //
    // Force a perfect mirror to test specular IBL:
    // metallic = 1.0;
    // roughness = 0.0;

    // --- 2. SETUP LIGHTING VECTORS ---
    vec3 V = normalize(viewPos - fragPos);
    vec3 F0 = vec3(0.04); // Base reflectance for non-metals
    F0 = mix(F0, albedo, metallic); // For metals, F0 is the albedo color

    // --- 3. DIRECT LIGHTING (Point Lights) ---
    vec3 Lo = vec3(0.0); // Outgoing Light
    for(int i = 0; i < activeLightCount; ++i)
    {
        vec3 L = normalize(lightPositions[i] - fragPos);
        vec3 H = normalize(V + L);
        
        float distance    = length(lightPositions[i] - fragPos);
        float attenuation = 1.0 / (distance * distance);
        vec3  radiance    = lightColors[i] * attenuation;
        
        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3  numerator   = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3  specular    = numerator / denominator;
            
        // For metals, diffuse light is black.
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= (1.0 - metallic);	  
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }   
    
    // --- 4. INDIRECT LIGHTING (Image-Based Lighting) ---
    // Diffuse IBL
    vec3 F_ambient   = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ambient  = F_ambient;
    vec3 kD_ambient  = (vec3(1.0) - kS_ambient) * (1.0 - metallic);
    vec3 irradiance  = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL  = kD_ambient * irradiance * albedo;

    // Specular IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(prefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf  = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_ambient * brdf.x + brdf.y);

    // Combine and apply ambient occlusion
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    
    // --- 5. FINAL ASSEMBLY ---
    vec3 color = ambient*1.5 + Lo + emissive;

    // HDR tonemapping maps the high dynamic range color to a displayable [0,1] range
    color = color / (color + vec3(1.0));
    // Gamma correction for final output
    color = pow(color, vec3(1.0/2.2)); 
    
    fragColor = vec4(color, 1.0);
}