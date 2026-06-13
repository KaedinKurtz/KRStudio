#version 430 core
// Stream-compaction: copy live particles (posLife.w > 0) from the source
// buffer to the front of the destination buffer, packed by an atomic
// counter. Ordering is not preserved (particle identity is irrelevant).
// The CPU reads back liveCount and copies the packed front back into the
// canonical particle SSBO.
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
layout(std430, binding = 0) buffer SrcBuf { Particle src[]; };
layout(std430, binding = 1) buffer DstBuf { Particle dst[]; };
layout(std430, binding = 2) buffer Counter { uint liveCount; };

uniform int u_particleCount;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (src[i].posLife.w <= 0.0) return;
    uint d = atomicAdd(liveCount, 1u);
    dst[d] = src[i];
}
