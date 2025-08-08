#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords; // Unused, but part of the VAO
layout (location = 3) in vec3 aTangent;   // We'll use this to pass view direction

// Outputs for the Fragment Shader
layout (location = 0) out vec3 fs_WorldPos;
layout (location = 1) out vec3 fs_WorldNormal;
layout (location = 2) out vec3 fs_ViewDir_tangent; // View direction in tangent space

// ===================================================================
// ## THE FIX IS HERE ##
// These uniform declarations were missing.
// ===================================================================
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
// ===================================================================

void main()
{
    // Calculate world-space position and normal
    fs_WorldPos   = vec3(model * vec4(aPos, 1.0));
    fs_WorldNormal = mat3(transpose(inverse(model))) * aNormal;

    // Calculate view direction in world space. The camera position is in the 4th column of the inverse view matrix.
    vec3 viewPos_world = vec3(inverse(view)[3]);
    vec3 viewDir_world = normalize(viewPos_world - fs_WorldPos);

    // Create TBN matrix to transform view direction into tangent space
    // NOTE: This is a simplified TBN calculation. For models with real tangents,
    // you would pass aTangent and aBitangent and use them here.
    vec3 N = normalize(fs_WorldNormal);
    vec3 T = normalize(cross(vec3(0.0, 1.0, 0.0), N));
    vec3 B = cross(N, T);
    mat3 TBN = transpose(mat3(T, B, N));

    // Pass the tangent-space view direction to the fragment shader
    fs_ViewDir_tangent = TBN * viewDir_world;

    // Calculate the final clip-space position
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}