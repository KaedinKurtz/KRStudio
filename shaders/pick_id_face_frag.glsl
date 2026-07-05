#version 430 core
// PICK-ID face pass: writes (entityId, typeBits|featureId) into the RG32UI pick target.
// faceId per triangle via gl_PrimitiveID -> triFace TBO (value stored +1; 0 = body with no
// B-Rep face map, which still picks the BODY). Type bits: 0 = face (high 2 bits of G).
uniform uint uEntityId;
uniform usamplerBuffer uTriFace;   // R32UI: triangle index -> faceId + 1 (0 = none)
uniform int uTriCount;             // guards gl_PrimitiveID lookups on malformed meshes

layout(location = 0) out uvec2 outId;

void main()
{
    uint fid = 0u;
    if (gl_PrimitiveID >= 0 && gl_PrimitiveID < uTriCount)
        fid = texelFetch(uTriFace, gl_PrimitiveID).r;
    outId = uvec2(uEntityId, fid);   // type bits 00 = face
}
