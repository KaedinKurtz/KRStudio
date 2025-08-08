#version 430 core

out vec4 FragColor;

// These uniforms must be declared here so the compiler knows they exist,
// even if they are only used in other stages.
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Start with a bright green color. If you see green, it means
    // both view and projection matrices seem to have valid, non-zero values.
    vec3 color = vec3(0.0, 1.0, 0.0); // Green = Success

    // Check for a common sign of an uninitialized matrix.
    if (view[0][0] == 0.0 && view[1][1] == 0.0) {
        color = vec3(1.0, 0.0, 0.0); // Turn RED if the view matrix is bad.
    }
    
    // Check for a common sign of a bad projection matrix.
    if (projection[0][0] == 0.0 || projection[1][1] == 0.0) {
        color = vec3(0.0, 0.0, 1.0); // Turn BLUE if the projection matrix is bad.
    }

    FragColor = vec4(color, 1.0);
}