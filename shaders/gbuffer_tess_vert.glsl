#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

// Define outputs
layout (location = 0) out vec2 vs_TexCoords;
layout (location = 1) out vec3 vs_Normal;

uniform mat4 model;

void main()
{
    // transform your normals into world?space (or whatever space your TES expects)
    mat3 normalMat = transpose(inverse(mat3(model)));
    vs_Normal    = normalize( normalMat * aNormal );

    // if you truly have no UVs, you can just zero these:
    vs_TexCoords = vec2(0.0);

    // push world?space position into the pipeline
    gl_Position  = model * vec4(aPos, 1.0);
}
