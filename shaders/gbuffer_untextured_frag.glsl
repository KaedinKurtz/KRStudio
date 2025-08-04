// File: gbuffer_untextured_frag.glsl
#version 450 core

struct Material {
    vec3  albedoColor;
    float metallic;
    float roughness;
    vec3  emissiveColor;
};
uniform Material material;

layout(location = 0) out vec4 gPosition;   layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;   layout(location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;

in vec3 FragPos;
in vec3 Normal;

void main()
{
    gPosition   = vec4(FragPos, 1.0);
    gNormal     = vec4(normalize(Normal), 1.0);
    gAlbedoAO   = vec4(material.albedoColor, 1.0); // Use color, assume full AO
    gMetalRough = vec4(material.metallic, material.roughness, 0.0, 1.0);
    gEmissive   = vec4(material.emissiveColor, 1.0);
}