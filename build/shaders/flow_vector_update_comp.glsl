#version 430 core

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// --- GPU Data Structures (Corrected to match C++) ---
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

struct InstanceData {
    mat4 modelMatrix;
    vec4 color;
    vec4 padding;
};

// --- Buffer Definitions ---
layout(std140, binding = 3) uniform EffectorDataUbo { PointEffectorGpu pointEffectors[256]; DirectionalEffectorGpu directionalEffectors[16]; };
layout(std430, binding = 4) readonly buffer TriangleEffectorBuffer { TriangleGpu triangleEffectors[]; };
layout(std430, binding = 5) readonly buffer ParticleInputBuffer { Particle particlesIn[]; };
layout(std430, binding = 6) buffer ParticleOutputBuffer { Particle particlesOut[]; };
layout(std430, binding = 7) buffer InstanceOutputBuffer { InstanceData instanceData[]; };

// --- Uniforms ---
uniform mat4 u_visualizerModelMatrix;
uniform float u_deltaTime;
uniform float u_time;
uniform vec3 u_boundsMin;
uniform vec3 u_boundsMax;
uniform float u_baseSpeed;
uniform float u_velocityMultiplier;
uniform float u_flowScale;
uniform float u_fadeInPercent;
uniform float u_fadeOutPercent;
uniform vec3 u_colorStart;
uniform vec3 u_colorMid;
uniform vec3 u_colorEnd;
uniform int u_pointEffectorCount;
uniform int u_directionalEffectorCount;
uniform int u_triangleEffectorCount;
uniform float u_seedOffset; // NEW: Per-frame random seed from CPU

// --- Helper Functions ---
mat4 rotationBetweenVectors(vec3 start, vec3 dest) {
    start = normalize(start); dest = normalize(dest);
    vec3 v = cross(start, dest); float c = dot(start, dest);
    if (c > 0.99999) { return mat4(1.0); }
    if (c < -0.99999) { mat4 R; R[0]=vec4(-1,0,0,0); R[1]=vec4(0,-1,0,0); R[2]=vec4(0,0,-1,0); R[3]=vec4(0,0,0,1); return R; }
    mat3 vx = mat3(0,v.z,-v.y, -v.z,0,v.x, v.y,-v.x,0);
    return mat4(mat3(1.0) + vx + vx*vx*(1.0/(1.0+c)));
}
vec3 closestPointOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b-a; vec3 ac = c-a; vec3 ap = p-a;
    float d1 = dot(ab,ap); float d2 = dot(ac,ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    vec3 bp = p-b; float d3 = dot(ab,bp); float d4 = dot(ac,bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    float vc = d1*d4 - d3*d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) { float v = d1/(d1-d3); return a + v*ab; }
    vec3 cp = p-c; float d5 = dot(ab,cp); float d6 = dot(ac,cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    float vb = d5*d2 - d1*d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) { float w = d2/(d2-d6); return a + w*ac; }
    float va = d3*d6 - d5*d4;
    if (va <= 0.0 && (d4-d3) >= 0.0 && (d5-d6) >= 0.0) { float w = (d4-d3)/((d4-d3)+(d5-d6)); return b + w*(c-b); }
    float denom = 1.0/(va+vb+vc); float v = vb*denom; float w = vc*denom;
    return a + ab*v + ac*w;
}
float random(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123); }
float getTrapezoidalScale(float age, float lifetime) {
    float fadeInDuration = lifetime * u_fadeInPercent;
    float fadeOutDuration = lifetime * u_fadeOutPercent;
    if (lifetime <= (fadeInDuration + fadeOutDuration)) { return smoothstep(0.0, 1.0, age / lifetime); }
    float fullScaleDuration = lifetime - fadeInDuration - fadeOutDuration;
    if (age < fadeInDuration) { return smoothstep(0.0, 1.0, age / fadeInDuration); }
    else if (age < fadeInDuration + fullScaleDuration) { return 1.0; }
    else if (age < lifetime) { return 1.0 - smoothstep(0.0, 1.0, (age - (fadeInDuration + fullScaleDuration)) / fadeOutDuration); }
    return 0.0;
}
vec3 getColorFromLifetime(float age, float lifetime) {
    float t = age / lifetime;
    if (t < 0.5) { return mix(u_colorStart, u_colorMid, t * 2.0); }
    else { return mix(u_colorMid, u_colorEnd, (t - 0.5) * 2.0); }
}

float random(vec3 seed) {
    return fract(sin(dot(seed, vec3(12.9898, 78.233, 151.7182))) * 43758.5453);
}

// --- Main Logic ---
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= particlesIn.length()) return;

    Particle p = particlesIn[gid];
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

    float fieldMagnitude = length(totalField);
    vec3 fieldDir = (fieldMagnitude > 0.001) ? normalize(totalField) : vec3(0.0);
    p.velocity.xyz = fieldDir * (u_baseSpeed + fieldMagnitude);
    p.position.xyz += p.velocity.xyz * u_deltaTime * u_velocityMultiplier;

    p.age += u_deltaTime;
    bool outOfBounds = p.position.y < u_boundsMin.y - 5.0 || p.position.y > u_boundsMax.y + 5.0 ||
                       p.position.x < u_boundsMin.x - 5.0 || p.position.x > u_boundsMax.x + 5.0 ||
                       p.position.z < u_boundsMin.z - 5.0 || p.position.z > u_boundsMax.z + 5.0;

    if (p.age > p.lifetime || outOfBounds) {
        // UPDATED: The seed now incorporates the per-frame CPU seed offset for true randomness.
        vec3 seed = vec3(gid, u_time + p.position.x, u_seedOffset + p.position.y);
        
        p.position.x = mix(u_boundsMin.x, u_boundsMax.x, random(seed * 1.1));
        p.position.y = mix(u_boundsMin.y, u_boundsMax.y, random(seed * 2.2));
        p.position.z = mix(u_boundsMin.z, u_boundsMax.z, random(seed * 3.3));
        p.position = u_visualizerModelMatrix * p.position;
        
        p.velocity = vec4(0.0);
        p.age = 0.0;
    }

    float lifetimeScale = getTrapezoidalScale(p.age, p.lifetime);
    if (fieldMagnitude > 0.01 && lifetimeScale > 0.01) {
        mat4 trans = mat4(1.0); trans[3] = vec4(p.position.xyz, 1.0);
        
        // CORRECTED: Negate the field direction to flip the arrow 180 degrees.
        mat4 rot = rotationBetweenVectors(vec3(0.0, 0.0, -1.0), -fieldDir);
        
        mat4 scaleMat = mat4(1.0);
        float finalScale = u_flowScale * lifetimeScale;
        scaleMat[0][0] = finalScale;
        scaleMat[1][1] = finalScale;
        scaleMat[2][2] = finalScale;
        instanceData[gid].modelMatrix = trans * rot * scaleMat;
        instanceData[gid].color = vec4(getColorFromLifetime(p.age, p.lifetime), 1.0);
    } else {
        instanceData[gid].modelMatrix = mat4(0.0);
    }

    particlesOut[gid] = p;
}
