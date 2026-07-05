#version 430 core
// PICK-ID primitive pass fragment: (entityId, typeBits | featureId+1).
// uTypeBits: 1 = edge, 2 = vertex (packed into the top 2 bits of G).
uniform uint uEntityId;
uniform uint uTypeBits;

flat in uint vId;

layout(location = 0) out uvec2 outId;

void main()
{
    outId = uvec2(uEntityId, (uTypeBits << 30) | (vId + 1u));
}
