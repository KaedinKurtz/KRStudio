#version 430 core

out vec4 FragColor;
in vec2 TexCoords;

// This will be the primary texture on the mesh
uniform sampler2D texture_diffuse1;

void main()
{    
    // Sample the texture at the given texture coordinates and output the color.
    FragColor = texture(texture_diffuse1, TexCoords);
}