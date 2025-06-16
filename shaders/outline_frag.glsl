// ===================================
//      outline_frag.glsl
// ===================================
#version 330 core
out vec4 FragColor; // The final color of the pixel.

// A simple uniform for setting the color of the outline or caliper lines.
uniform vec3 u_outlineColor;
void main() {

   FragColor = vec4(u_outlineColor, 1.0);

}
