#version 430 core

// This layout directive tells OpenGL which color attachment of the G-Buffer FBO to write to.
layout(location = 0) out vec3 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gAlbedoSpec;

in vec3 FragPos;
in vec3 Normal;

// This comes from the MaterialComponent
uniform vec3 objectColor;

void main()
{    
    // Output 1: Store the fragment's world-space position in the first texture.
    gPosition = FragPos;
    
    // Output 2: Store the fragment's normalized world-space normal in the second texture.
    gNormal = normalize(Normal);
    
    // Output 3: Store material properties in the third texture.
    // We store the albedo (diffuse color) in the RGB channels.
    gAlbedoSpec.rgb = objectColor;
    // We store the specular intensity in the alpha channel.
    gAlbedoSpec.a = 0.5; // You can make this a uniform from MaterialComponent later
}