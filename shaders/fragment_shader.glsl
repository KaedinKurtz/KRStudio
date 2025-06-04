// Specifies the GLSL version.
#version 330 core

// Output variable for the final color of the fragment (pixel).
// 'out vec4 FragColor;' declares 'FragColor' as a 4-component vector (red, green, blue, alpha).
// The GPU will write the value assigned to this variable to the framebuffer for the current pixel.
out vec4 FragColor;

// Uniform variable for the object's color.
// This 'objectColor' is set from your C++ application (e.g., using shader->setVec4("objectColor", ...)).
// It allows you to draw different objects with different colors using the same shader.
uniform vec4 objectColor; 

void main()
{
    // This is the core of the fragment shader.
    // It determines the color of the current pixel being processed.
    // In this case, it simply assigns the value of the 'objectColor' uniform
    // to the output 'FragColor'.
    // So, every pixel covered by the object being drawn will get this uniform color.
    FragColor = objectColor; 
}
