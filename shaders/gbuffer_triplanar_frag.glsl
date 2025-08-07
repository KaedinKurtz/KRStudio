#version 450 core
layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout (location = 4) out vec4 gEmissive;

in VS_OUT {
    vec3 WorldPos;
    vec3 WorldNormal;
} fs_in;

struct Material {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D aoMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D emissiveMap;
};
uniform Material material;

uniform float u_texture_scale = 1.0;

// --- Tri-Planar Helper Function ---
vec3 triplanar_map(sampler2D tex, vec3 world_pos, vec3 world_normal) {
    // Use the absolute value of the normal as blend weights.
    // pow() sharpens the transition between the three projections.
    vec3 blend_weights = pow(abs(world_normal), vec3(3.0));
    // Normalize the weights so they sum to 1.0.
    blend_weights = blend_weights / (blend_weights.x + blend_weights.y + blend_weights.z);

    // Define the UV coordinates for each projection plane.
    vec2 uv_x = world_pos.yz * u_texture_scale; // Projection from X-axis uses YZ coordinates
    vec2 uv_y = world_pos.xz * u_texture_scale; // Projection from Y-axis uses XZ coordinates
    vec2 uv_z = world_pos.xy * u_texture_scale; // Projection from Z-axis uses XY coordinates

    // Sample the texture from each of the 3 directions.
    vec3 color_x = texture(tex, uv_x).rgb;
    vec3 color_y = texture(tex, uv_y).rgb;
    vec3 color_z = texture(tex, uv_z).rgb;

    // Blend the 3 samples together based on the surface normal.
    return color_x * blend_weights.x + color_y * blend_weights.y + color_z * blend_weights.z;
}

void main()
{
    vec3 baseNormal = normalize(fs_in.WorldNormal);

    // Sample all PBR textures using the tri-planar mapping function.
    vec3 albedo    = triplanar_map(material.albedoMap, fs_in.WorldPos, baseNormal);
    float ao       = triplanar_map(material.aoMap, fs_in.WorldPos, baseNormal).r;
    float metallic = triplanar_map(material.metallicMap, fs_in.WorldPos, baseNormal).r;
    float roughness= triplanar_map(material.roughnessMap, fs_in.WorldPos, baseNormal).r;
    vec3 emissive  = triplanar_map(material.emissiveMap, fs_in.WorldPos, baseNormal);

    // Write final values to G-Buffer.
    gPosition   = vec4(fs_in.WorldPos, 1.0);
    gNormal     = vec4(baseNormal, 1.0); // Using vertex normals for now.
    gAlbedoAO   = vec4(albedo, ao);
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);
    gEmissive   = vec4(emissive, 1.0);
}
