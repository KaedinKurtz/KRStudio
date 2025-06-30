#version 410 core

// This shader's only job is to output a single, solid color.
// We use this to draw the shape of the selected object to our glow buffer.

out vec4 FragColor; // The final output color for the pixel.

uniform vec3 emissiveColor; // A uniform to control the color from C++.

void main()
{
    // Output the specified color with full alpha.
    FragColor = vec4(emissiveColor, 1.0);
}
