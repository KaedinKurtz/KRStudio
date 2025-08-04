// File: gbuffer_textured_frag.glsl
#version 450 core

struct Material {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D aoMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D emissiveMap;
    float     normalScale; // Added for adjustable normal map strength
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

void main()
{
    // --- Position ---
    gPosition = vec4(FragPos, 1.0);

    // --- Normal Mapping (Robust Version) ---
    // 1. Normalize the interpolated input vectors.
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent);
    vec3 B = normalize(Bitangent);

    // 2. Re-orthogonalize T with respect to N (Gram-Schmidt process).
    // This corrects for interpolation errors and ensures the TBN basis is perpendicular.
    T = normalize(T - dot(T, N) * N);

    // 3. Re-calculate the Bitangent to be perfectly perpendicular to the new T and N.
    // We also check the handedness to prevent flipped normals.
    B = cross(N, T);
    if (dot(cross(N, T), B) < 0.0){
        T = T * -1.0;
    }

    // 4. Construct the robust TBN matrix.
    mat3 TBN = mat3(T, B, N);

    // 5. Sample the normal map and unpack the normal from [0,1] to [-1,1].
    vec3 tangentSpaceNormal = texture(material.normalMap, TexCoords).rgb * 2.0 - 1.0;
    
    // Optional: If your normal map still looks wrong (e.g., light is inverted),
    // it might be a DirectX-style normal map. Uncomment the line below to fix it.
    // tangentSpaceNormal.y = -tangentSpaceNormal.y;

    // 6. Transform the tangent-space normal to world space using the TBN matrix.
    vec3 worldSpaceNormal = normalize(TBN * tangentSpaceNormal);
    gNormal = vec4(worldSpaceNormal, 1.0);


    // --- Albedo + AO ---
    vec3 albedo = texture(material.albedoMap, TexCoords).rgb;
    float ao    = texture(material.aoMap, TexCoords).r;
    gAlbedoAO   = vec4(albedo, ao);

    // --- DEBUG: Visualize the Normal Map ---
    // Uncomment the line below to write the raw normal map color to the albedo buffer.
    // If the normal map is being sampled correctly, the object will turn bluish/purplish.
    //gAlbedoAO = vec4(texture(material.normalMap, TexCoords).rgb, 1.0);


    // --- Metallic + Roughness ---
    float metallic  = texture(material.metallicMap, TexCoords).r;
    float roughness = texture(material.roughnessMap, TexCoords).r;
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);

    // --- Emissive ---
    vec3 emissive = texture(material.emissiveMap, TexCoords).rgb;
    gEmissive = vec4(emissive, 1.0);
}
