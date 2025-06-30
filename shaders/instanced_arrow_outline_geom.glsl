#version 410 core

// We receive triangles from the Vertex Shader
layout (triangles) in;
// We will output triangle strips (more efficient than separate triangles)
layout (triangle_strip, max_vertices = 6) out;

// Inputs from the Vertex Shader (for each vertex of the triangle)
in VS_OUT {
    vec3 color;
    vec3 normal;
} gs_in[];

// Output to the Fragment Shader
out vec3 fColor;

// Uniforms that are constant for the whole draw call
uniform mat4 projection;
uniform mat4 view;
uniform float outlineThickness = 0.08; // How thick the outline should be

void main() {
    // --- First, draw the main colored arrow triangle ---
    fColor = gs_in[0].color; // Use the color passed from the VS
    for (int i = 0; i < 3; i++) {
        gl_Position = projection * view * gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();

    // --- Second, draw the black outline triangle ---
    fColor = vec3(0.0, 0.0, 0.0); // Set outline color to black
    for (int i = 0; i < 3; i++) {
        // Calculate the outline offset by pushing the vertex along its normal
        vec4 newPos = gl_in[i].gl_Position + vec4(normalize(gs_in[i].normal), 0.0) * outlineThickness;
        gl_Position = projection * view * newPos;
        EmitVertex();
    }
    EndPrimitive();
}
