#version 450 core

// G-Buffer outputs
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;

// Data received from the vertex shader
in vec3 FragPos;
in vec3 Normal;

// Uniform for the object's emissive color
uniform vec3 emissiveColor;

void main()
{
    // 1. Position and Normal
    gPosition = vec4(FragPos, 1.0);
    gNormal   = vec4(normalize(Normal), 1.0);

    // 2. Albedo and AO
    // A purely emissive object has no albedo (it's black). We assume full AO.
    gAlbedoAO = vec4(0.0, 0.0, 0.0, 1.0);

    // 3. Metallic and Roughness
    // These properties don't apply to a purely emissive surface.
    // We'll use defaults: not metallic (0.0) and fully rough (1.0).
    gMetalRough = vec4(0.0, 1.0, 0.0, 1.0);

    // 4. Emissive
    // Output the specified emissive color.
    gEmissive = vec4(emissiveColor, 1.0);
}