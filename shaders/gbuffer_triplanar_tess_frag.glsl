#version 450 core
// G-Buffer Outputs
layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout (location = 4) out vec4 gEmissive;

// Inputs from the TES, matched by location
layout (location = 0) in vec3 fs_FragPos;
layout (location = 1) in vec2 fs_TexCoords; // Unused by triplanar, but part of the interface
layout (location = 2) in vec3 fs_Normal;

// --- Your PBR Material and Triplanar Logic ---
struct Material {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D aoMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D emissiveMap;
};
uniform Material material;
uniform float u_texture_scale;

vec3 triplanar_map(sampler2D tex, vec3 world_pos, vec3 world_normal) {
    vec3 blend_weights = pow(abs(world_normal), vec3(3.0));
    blend_weights = blend_weights / (blend_weights.x + blend_weights.y + blend_weights.z);

    vec2 uv_x = world_pos.yz * u_texture_scale;
    vec2 uv_y = world_pos.xz * u_texture_scale;
    vec2 uv_z = world_pos.xy * u_texture_scale;

    vec3 color_x = texture(tex, uv_x).rgb;
    vec3 color_y = texture(tex, uv_y).rgb;
    vec3 color_z = texture(tex, uv_z).rgb;

    return color_x * blend_weights.x + color_y * blend_weights.y + color_z * blend_weights.z;
}

void main()
{
    vec3 baseNormal = normalize(fs_Normal);

    // Sample all PBR textures using the tri-planar mapping function
    vec3 albedo    = triplanar_map(material.albedoMap, fs_FragPos, baseNormal);
    float ao       = triplanar_map(material.aoMap, fs_FragPos, baseNormal).r;
    float metallic = triplanar_map(material.metallicMap, fs_FragPos, baseNormal).r;
    float roughness= triplanar_map(material.roughnessMap, fs_FragPos, baseNormal).r;
    vec3 emissive  = triplanar_map(material.emissiveMap, fs_FragPos, baseNormal);

    // Write final values to G-Buffer
    gPosition   = vec4(fs_FragPos, 1.0);
    gNormal     = vec4(baseNormal, 1.0);
    gAlbedoAO   = vec4(albedo, ao);
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);
    gEmissive   = vec4(emissive, 1.0);
}