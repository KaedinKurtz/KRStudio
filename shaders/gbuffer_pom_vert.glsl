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

    // Calculate view direction in WORLD space. The camera position is in the 4th column of the inverse view matrix.
    vec3 viewPos_world = vec3(inverse(view)[3]);
    vec3 viewDir_world = normalize(viewPos_world - fs_WorldPos);

    // The triplanar-POM fragment shader uses the view direction in WORLD space (it projects
    // V onto the three world-aligned planes: vp_x=(V.z,V.y), vNormal=V.x, etc). Pass it
    // straight through. The old code transformed it into a per-vertex tangent frame built as
    // T = normalize(cross((0,1,0), N)) -- which is BOTH the wrong space AND degenerate: on a
    // horizontal floor's top/bottom face N is (0,+/-1,0), so the cross is the zero vector and
    // normalize() yields NaN. That NaN poisoned the POM march -> NaN albedo AND NaN gNormal
    // -> the deferred lighting pass treated the whole top face as background -> it rendered
    // BLACK (while the non-degenerate side faces still showed). Passing world-space V fixes
    // both the space mismatch and the black-floor NaN.
    fs_ViewDir_tangent = viewDir_world;

    // Calculate the final clip-space position
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}