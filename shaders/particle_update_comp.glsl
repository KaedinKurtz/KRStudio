#version 430 core

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// --- GPU Data Structures ---
struct PointEffectorGpu {
    vec4 position;
    vec4 normal;
    float strength;
    float radius;
    int falloffType;
    float padding;
};

struct DirectionalEffectorGpu {
    vec4 direction;
    float strength;
    float padding1, padding2, padding3;
};

struct TriangleGpu {
    vec4 v0; // w component stores strength
    vec4 v1;
    vec4 v2;
    vec4 normal; // w component stores radius
};

// NEW: Particle Data Structure
struct Particle {
    vec4 position;
    vec4 velocity;
    vec4 color;
    float age;
    float lifetime;
    float size;
    float padding;
};

// --- Buffer Definitions ---
layout(std140, binding = 3) uniform EffectorDataUbo {
    PointEffectorGpu pointEffectors[256];
    DirectionalEffectorGpu directionalEffectors[16];
};

layout(std430, binding = 4) readonly buffer TriangleEffectorBuffer {
    TriangleGpu triangleEffectors[];
};

// NEW: Particle Ping-Pong Buffers
layout(std430, binding = 5) readonly buffer ParticleInputBuffer { Particle particlesIn[]; };
layout(std430, binding = 6) buffer ParticleOutputBuffer { Particle particlesOut[]; };

// --- Uniforms ---
uniform mat4 u_visualizerModelMatrix; // To transform spawn points
uniform float u_deltaTime;
uniform float u_time; // For random seed
uniform vec3 u_boundsMin;
uniform vec3 u_boundsMax;

uniform int u_pointEffectorCount;
uniform int u_directionalEffectorCount;
uniform int u_triangleEffectorCount;

// --- Helper Functions ---

// Simple pseudo-random number generator
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 closestPointOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 ap = p - a;
    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;

    vec3 bp = p - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    vec3 cp = p - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0 / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}


// --- Main Logic ---
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= particlesIn.length()) return;

    Particle p = particlesIn[gid]; // Get the current particle state

    // --- 1. FIELD CALCULATION (Your existing logic) ---
    vec3 worldPos = p.position.xyz;
    vec3 totalField = vec3(0.0);

    for (int i = 0; i < u_pointEffectorCount; ++i) {
        vec3 diff = worldPos - pointEffectors[i].position.xyz;
        float dist = length(diff);
        if (dist < pointEffectors[i].radius && dist > 0.001) {
            float strength = pointEffectors[i].strength;
            if (pointEffectors[i].falloffType == 1) { strength *= (1.0 - dist / pointEffectors[i].radius); }
            totalField += normalize(diff) * strength;
        }
    }

    for (int i = 0; i < u_triangleEffectorCount; ++i) {
        TriangleGpu tri = triangleEffectors[i];
        vec3 closestPoint = closestPointOnTriangle(worldPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
        vec3 diff = worldPos - closestPoint;
        float dist = length(diff);
        float radius = tri.normal.w;
        if (dist > 0.001 && dist < radius) {
            float strength = tri.v0.w * (1.0 - dist / radius);
            totalField += normalize(diff) * strength;
        }
    }

    for (int i = 0; i < u_directionalEffectorCount; ++i) {
        totalField += directionalEffectors[i].direction.xyz * directionalEffectors[i].strength;
    }

    // --- 2. PARTICLE INTEGRATION (Basic Physics) ---
    vec3 acceleration = totalField;
    p.velocity.xyz += acceleration * u_deltaTime;
    p.position.xyz += p.velocity.xyz * u_deltaTime;
    p.velocity.xyz *= 0.985; // Velocity damping

    // --- 3. LIFETIME MANAGEMENT ---
    p.age += u_deltaTime;
    if (p.age > p.lifetime) {
        // Respawn the particle at a random location
        vec2 seed = vec2(gid, u_time);
        p.position.x = mix(u_boundsMin.x, u_boundsMax.x, random(seed * 1.1));
        p.position.y = mix(u_boundsMin.y, u_boundsMax.y, random(seed * 2.2));
        p.position.z = mix(u_boundsMin.z, u_boundsMax.z, random(seed * 3.3));
        p.position = u_visualizerModelMatrix * p.position; // Transform to world space
        p.velocity = vec4(0.0);
        p.age = 0.0;
    }

    // --- 4. WRITE TO OUTPUT ---
    particlesOut[gid] = p;
}
