// ===================================
//      outline_frag.glsl
// ===================================
#version 330 core
out vec4 FragColor; // The final color of the pixel.

// A simple uniform for setting the color of the outline or caliper lines.
uniform vec3 u_color;

void main()
{
    // Output the specified color with full opacity.
    FragColor = vec4(u_color, 1.0);
}