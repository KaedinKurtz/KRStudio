#version 450 core

// --- Vertex Attributes ---
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
// We accept tangent/bitangent to use the same VAO, but they won't be used.
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

// --- Outputs to Fragment Shader with EXPLICIT LOCATIONS ---
// This interface MUST perfectly match the 'in' block of the untextured fragment shader.
layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoords;
// Note: No Tangent/Bitangent outputs needed for the simple untextured fragment shader.
layout(location = 5) out vec2 v_motionVector;

// --- Uniforms ---
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 previousView;
uniform mat4 previousProjection;

void main()
{
    // Pass world-space position to fragment shader
    FragPos = vec3(model * vec4(aPos, 1.0));

    // Pass transformed normal to fragment shader
    Normal = mat3(transpose(inverse(model))) * aNormal;

    // Pass UVs through, even if unused, to maintain the interface
    TexCoords = aTexCoords;

    // --- MOTION VECTOR CALCULATION ---
    vec4 currentClipPos = projection * view * model * vec4(aPos, 1.0);
    vec4 previousClipPos = previousProjection * previousView * model * vec4(aPos, 1.0);
    vec2 currentNDC = currentClipPos.xy / currentClipPos.w;
    vec2 previousNDC = previousClipPos.xy / previousClipPos.w;
    vec2 currentUV = currentNDC * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;
    v_motionVector = currentUV - previousUV;

    // Final clip space position
    gl_Position = currentClipPos;
}