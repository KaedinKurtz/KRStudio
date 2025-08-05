#version 450 core

struct Material {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D aoMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D emissiveMap;
};
uniform Material material;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Tangent;
in vec3 Bitangent;

// New uniform to control albedo brightness
uniform float u_albedoBrightness;

void main()
{
    // Position
    gPosition = vec4(FragPos, 1.0);

    // Normal (always assume normal map for this test)
    vec3 nMap = texture(material.normalMap, TexCoords).rgb * 2.0 - 1.0;
    vec3 T = normalize(Tangent);
    vec3 B = normalize(Bitangent);
    vec3 N = normalize(Normal);
    gNormal = vec4(normalize(mat3(T, B, N) * nMap), 1.0);

    // Albedo + AO
    vec3 albedo = texture(material.albedoMap, TexCoords).rgb;
    // Apply the brightness factor to the albedo color.
    albedo *= u_albedoBrightness;
    float ao    = texture(material.aoMap, TexCoords).r;
    gAlbedoAO   = vec4(albedo, ao);

    // Metallic + Roughness
    float metallic  = texture(material.metallicMap, TexCoords).r;
    float roughness = texture(material.roughnessMap, TexCoords).r;
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);

    // Emissive
    vec3 emissive = texture(material.emissiveMap, TexCoords).rgb;
    gEmissive = vec4(emissive, 1.0);
}