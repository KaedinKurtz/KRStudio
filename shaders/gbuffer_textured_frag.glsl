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
    vec3      emissiveColor; // flat emissive added on top of the map (light-emitter glow)
    float     emissiveStrength; // magnitude for the flat emissiveColor
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

uniform float u_texture_scale; // UV tiling multiplier (artist control)

void main()
{
    vec2 uv = TexCoords * max(u_texture_scale, 1e-4);
    // --- Position ---
    gPosition = vec4(FragPos, 1.0);

    // --- Normal (geometric, with optional tangent-space normal mapping) ---
    // 1. The interpolated geometric vertex normal. This is ALWAYS valid (the mesh
    //    importer guarantees it); it is the ground truth we must preserve.
    vec3 N = normalize(Normal);

    // 2. Decide whether a usable tangent basis exists. CAD/BRep-imported meshes
    //    carry ZERO tangents/bitangents and have NO normal map. Running them through
    //    the TBN normal-mapping path below built a degenerate (NaN/zero) basis and
    //    wrote a zero-length normal to the G-buffer -> the deferred lighting pass then
    //    treated every such fragment as a silhouette/background pixel (no shading, no
    //    area light). Gate on the tangent length so meshes without a tangent basis
    //    fall back to the geometric normal instead of corrupting it.
    vec3 worldSpaceNormal;
    if (dot(Tangent, Tangent) < 1e-8) {
        // No tangent basis (CAD/BRep): use the geometric normal directly.
        worldSpaceNormal = N;
    } else {
        // Full normal mapping. Re-orthogonalize T against N (Gram-Schmidt), rebuild B.
        vec3 T = normalize(Tangent);
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T);
        mat3 TBN = mat3(T, B, N);
        vec3 tangentSpaceNormal = texture(material.normalMap, uv).rgb * 2.0 - 1.0;
        worldSpaceNormal = normalize(TBN * tangentSpaceNormal);
    }
    gNormal = vec4(worldSpaceNormal, 1.0);


    // --- Albedo + AO ---
    vec3 albedo = texture(material.albedoMap, uv).rgb;
    float ao    = texture(material.aoMap, uv).r;
    gAlbedoAO   = vec4(albedo, ao);

    // --- DEBUG: Visualize the Normal Map ---
    // Uncomment the line below to write the raw normal map color to the albedo buffer.
    // If the normal map is being sampled correctly, the object will turn bluish/purplish.
    //gAlbedoAO = vec4(texture(material.normalMap, uv).rgb, 1.0);


    // --- Metallic + Roughness ---
    float metallic  = texture(material.metallicMap, uv).r;
    float roughness = texture(material.roughnessMap, uv).r;
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);

    // --- Emissive ---
    // Sampled emissive MAP plus the flat emissiveColor*strength (so a textured/CAD body
    // turned into a light emitter glows even with no emissive texture). The lighting pass
    // scales the result to nits.
    vec3 emissive = texture(material.emissiveMap, uv).rgb + material.emissiveColor * material.emissiveStrength;
    gEmissive = vec4(emissive, 1.0);
}
