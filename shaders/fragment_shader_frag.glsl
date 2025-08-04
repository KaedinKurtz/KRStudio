#version 450 core

// G-Buffer outputs
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;

// Data received from the vertex shader (in world space)
in vec3 FragPos;
in vec3 Normal;

// Uniform for the object's base color
uniform vec3 objectColor;

void main()
{
    // 1. Position and Normal
    gPosition = vec4(FragPos, 1.0);
    gNormal   = vec4(normalize(Normal), 1.0);

    // 2. Albedo and AO
    // The albedo is the object's color. We assume full ambient occlusion (1.0).
    gAlbedoAO = vec4(objectColor, 1.0);

    // 3. Metallic and Roughness
    // A standard Phong material is non-metallic (0.0).
    // We'll give it a medium roughness (0.5) to approximate the shininess.
    gMetalRough = vec4(0.0, 0.5, 0.0, 1.0);

    // 4. Emissive
    // A standard Phong material is not emissive.
    gEmissive = vec4(0.0, 0.0, 0.0, 1.0);
}