#version 450 core

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

// --- Scalable Lighting Uniforms ---
// Use arrays to support multiple lights in the future.
// Your C++ code should set these as e.g. "lightPositions[0]", "lightColors[0]"
const int MAX_LIGHTS = 4; // Set a maximum number of lights
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int activeLightCount; // Tell the shader how many lights are currently active

in vec2 TexCoords;
out vec4 fragColor;

const float PI = 3.14159265359;

// PBR Helper Functions (Unchanged)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
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
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 1. Retrieve material properties from G-Buffer
    vec3  fragPos   = texture(gPosition, TexCoords).rgb;
    vec3  albedo    = texture(gAlbedoAO, TexCoords).rgb;

    // THE FIX for "nuking the scene":
    // If the albedo is black, we assume this is a background pixel and discard it,
    // preventing it from being overwritten with a black lit color.
    // A small threshold avoids issues with floating point precision.
    if(length(albedo) < 0.01) {
        discard;
    }

    // Continue retrieving other G-Buffer properties
    vec3  N         = normalize(texture(gNormal, TexCoords).rgb); // Normal is already in world space
    float ao        = texture(gAlbedoAO, TexCoords).a;
    vec2  mr        = texture(gMetalRough, TexCoords).rg;
    float metallic  = mr.r;
    float roughness = mr.g;
    vec3  emissive  = texture(gEmissive, TexCoords).rgb;

    // 2. Setup vectors for lighting
    vec3 V = normalize(viewPos - fragPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 3. Direct Lighting: Loop over all active lights
    vec3 Lo = vec3(0.0); // Outgoing radiance
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
        
        vec3  numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3  specular = numerator / denominator;
            
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        
        float NdotL = max(dot(N, L), 0.0);        
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // 4. Image-Based Lighting (IBL) for ambient term
    vec3 F_ambient = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kD = (vec3(1.0) - F_ambient) * (1.0 - metallic);
    
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = kD * albedo * irradiance;
    
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(prefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb;    
    vec2 brdf  = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_ambient * brdf.x + brdf.y);

    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    
    // 5. Final color assembly
    vec3 color = Lo + ambient + emissive;
    color = color / (color + vec3(1.0)); // Reinhard tonemapping
    color = pow(color, vec3(1.0/2.2)); // Gamma correction

    fragColor = vec4(color, 1.0);
}