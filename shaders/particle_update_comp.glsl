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
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 normal;
};

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
layout(std430, binding = 4) readonly buffer TriangleEffectorBuffer { TriangleGpu triangleEffectors[]; };
layout(std430, binding = 5) readonly buffer ParticleInputBuffer { Particle particlesIn[]; };
layout(std430, binding = 6) buffer ParticleOutputBuffer { Particle particlesOut[]; };

// --- Uniforms ---
uniform mat4 u_visualizerModelMatrix;
uniform float u_deltaTime;
uniform float u_time;
uniform vec3 u_boundsMin;
uniform vec3 u_boundsMax;
uniform int u_pointEffectorCount;
uniform int u_directionalEffectorCount;
uniform int u_triangleEffectorCount;

// Particle-specific uniforms
uniform float u_lifetime;
uniform float u_baseSpeed;
uniform float u_speedIntensityMultiplier;
uniform float u_baseSize;
uniform float u_peakSizeMultiplier;
uniform float u_minSize;
uniform float u_randomWalkStrength;
uniform int u_coloringMode; // 0=Intensity, 1=Lifetime, 2=Directional

// Gradient uniforms
uniform int u_stopCount;
uniform float u_stopPos[8];
uniform vec4 u_stopColor[8];

// --- Helper Functions ---
float random(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yxz + 19.19);
    return fract((p.x + p.y) * p.z);
}

vec3 closestPointOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    vec3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + (d1 / (d1 - d3)) * ab;
    vec3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + (d2 / (d2 - d6)) * ac;
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4-d3)>=0.0 && (d5-d6)>=0.0) return b + ((d4-d3)/((d4-d3)+(d5-d6))) * (c-b);
    float denom = 1.0/(va+vb+vc);
    return a + ab*(vb*denom) + ac*(vc*denom);
}

vec3 getColorFromGradient(float t) {
    if (u_stopCount <= 1) {
        return u_stopColor[0].rgb;
    }
    t = clamp(t, 0.0, 1.0);
    for (int i = 0; i < u_stopCount - 1; ++i) {
        if (t >= u_stopPos[i] && t <= u_stopPos[i+1]) {
            float local_t = (t - u_stopPos[i]) / (u_stopPos[i+1] - u_stopPos[i]);
            return mix(u_stopColor[i].rgb, u_stopColor[i+1].rgb, local_t);
        }
    }
    return u_stopColor[u_stopCount - 1].rgb;
}

// --- Main Logic ---
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= particlesIn.length()) return;

    Particle p = particlesIn[gid];
    vec3 worldPos = p.position.xyz;
    vec3 totalField = vec3(0.0);

    // --- Field Calculation ---
    for (int i = 0; i < u_pointEffectorCount; ++i) {
        vec3 diff = worldPos - pointEffectors[i].position.xyz;
        float dist = length(diff);
        if (dist > 0.001 && dist < pointEffectors[i].radius) {
            float falloff = 1.0 - dist / pointEffectors[i].radius;
            totalField += normalize(diff) * pointEffectors[i].strength * falloff;
        }
    }
    for (int i = 0; i < u_directionalEffectorCount; ++i) {
        totalField += directionalEffectors[i].direction.xyz * directionalEffectors[i].strength;
    }
    
    if (u_randomWalkStrength > 0.0) {
        vec3 seed = vec3(p.position.xyz + u_time);
        vec3 turbulence = vec3(random(seed) - 0.5, random(seed.yxz) - 0.5, random(seed.zyx) - 0.5);
        totalField += normalize(turbulence) * u_randomWalkStrength;
    }

    // --- Particle Integration ---
    float fieldMag = length(totalField);
    vec3 fieldDir = fieldMag > 0.001 ? normalize(totalField) : vec3(0,0,0);
    
    vec3 acceleration = fieldDir * (u_baseSpeed + fieldMag * u_speedIntensityMultiplier);
    p.velocity.xyz += acceleration * u_deltaTime;
    p.position.xyz += p.velocity.xyz * u_deltaTime;
    p.velocity.xyz *= 0.98;

    // --- Lifetime, Respawn, Size, and Color ---
    p.age += u_deltaTime;
    if (p.age > u_lifetime) {
        vec3 seed = vec3(gid, u_time, gid + u_time);
        p.position.x = mix(u_boundsMin.x, u_boundsMax.x, random(seed * 1.1));
        p.position.y = mix(u_boundsMin.y, u_boundsMax.y, random(seed * 2.2));
        p.position.z = mix(u_boundsMin.z, u_boundsMax.z, random(seed * 3.3));
        p.position = u_visualizerModelMatrix * p.position;
        p.velocity = vec4(0.0);
        p.age = 0.0;
    }

    // --- Calculate size and color ---
    float age_t = p.age / u_lifetime;
    float intensity_t = clamp(fieldMag, 0.0, 1.0);
    p.size = u_baseSize + (u_peakSizeMultiplier * u_baseSize - u_baseSize) * intensity_t;
    p.size = max(p.size, u_minSize);

    if (u_coloringMode == 0) { // Intensity
        p.color.rgb = getColorFromGradient(intensity_t);
    } else if (u_coloringMode == 1) { // Lifetime
        p.color.rgb = getColorFromGradient(age_t);
    }
    p.color.a = 1.0; // Ensure alpha is always 1
    
    particlesOut[gid] = p;
}