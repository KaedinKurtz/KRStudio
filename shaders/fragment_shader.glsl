#version 330 core
out vec4 FragColor;

// This uniform must be declared to receive the color from C++
uniform vec4 objectColor; 

void main()
{
    // This line must use the uniform, not a hardcoded color
    FragColor = objectColor; 
}