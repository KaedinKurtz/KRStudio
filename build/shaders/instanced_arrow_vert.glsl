/*
================================================================================
|                           instanced_arrow_vert.glsl                          |
================================================================================
*/
#version 430 core

// Per-vertex attributes (for the base arrow mesh)
layout (location = 0) in vec3 aPos;    // The position of a vertex in the arrow model.
layout (location = 1) in vec3 aNormal; // The normal of a vertex in the arrow model.

/*
 * Per-instance attributes.
 * A mat4 attribute consumes 4 consecutive locations. We explicitly define each
 * column to ensure maximum compatibility across different graphics drivers.
 * This now perfectly matches the setup in RenderingSystem.cpp.
*/
layout (location = 2) in vec4 aInstanceMatCol0; // Column 0 of the model matrix.
layout (location = 3) in vec4 aInstanceMatCol1; // Column 1 of the model matrix.
layout (location = 4) in vec4 aInstanceMatCol2; // Column 2 of the model matrix.
layout (location = 5) in vec4 aInstanceMatCol3; // Column 3 of the model matrix.
layout (location = 6) in vec4 aInstanceColor;   // The unique color for this instance.

// Uniforms (constant for the entire draw call)
uniform mat4 view;       // The camera's view matrix.
uniform mat4 projection; // The camera's projection matrix.

// Output variable to pass the final color to the fragment shader.
out vec4 vs_color;

void main()
{
    // Reconstruct the full model matrix from its individual column vectors.
    mat4 modelMatrix = mat4(
        aInstanceMatCol0, 
        aInstanceMatCol1, 
        aInstanceMatCol2, 
        aInstanceMatCol3
    );
    
    // Transform the vertex position into clip space.
    // The order of multiplication (Projection * View * Model * Position) is crucial.
    gl_Position = projection * view * modelMatrix * vec4(aPos, 1.0);
    
    // Pass the instance's color directly to the fragment shader.
    vs_color = aInstanceColor;
}
