/**
 * @file line_frag.glsl
 * @brief Fragment shader for drawing simple colored lines or objects.
 *
 * This shader outputs a single, uniform color for every fragment.
 */

// Specifies the GLSL version.
#version 330 core

// Output variable for the final color of the fragment.
out vec4 FragColor;

// A uniform variable to set the object's color from the C++ application.
uniform vec3 color;

void main() {
    // Set the fragment's color to the value of the 'color' uniform.
    // The alpha component is set to 1.0 for full opacity.
    FragColor = vec4(color, 1.0);
}