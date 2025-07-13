#version 430 core
layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

/*  GPU-side structs */
struct PointEffectorGpu      { vec4 position; vec4 normal;   float strength; float radius; int falloffType; float pad; };
struct DirectionalEffectorGpu{ vec4 direction; float strength; float p1,p2,p3; };
struct TriangleGpu           { vec4 v0; vec4 v1; vec4 v2; vec4 normal; };
struct Particle              { vec4 position; vec4 velocity; vec4 color; float age; float lifetime; float size; float pad; };

/*  buffer bindings */
layout(std140, binding = 3) uniform EffectorDataUbo {
    PointEffectorGpu      pointEffectors[256];
    DirectionalEffectorGpu directionalEffectors[16];
};

layout(std430,  binding = 4) readonly  buffer TriangleEffectorBuffer { TriangleGpu  triangleEffectors[]; };
layout(std430,  binding = 5) readonly  buffer ParticleInputBuffer    { Particle     particlesIn[];      };
layout(std430,  binding = 6) writeonly buffer ParticleOutputBuffer   { Particle     particlesOut[];     };

/* uniforms  */
uniform mat4  u_visualizerModelMatrix;
uniform float u_deltaTime;
uniform float u_time;
uniform vec3  u_boundsMin;
uniform vec3  u_boundsMax;

uniform int   u_pointEffectorCount;
uniform int   u_directionalEffectorCount;
uniform int   u_triangleEffectorCount;

uniform float u_lifetime;
uniform float u_baseSpeed;
uniform float u_speedIntensityMultiplier;
uniform float u_baseSize;
uniform float u_peakSizeMultiplier;
uniform float u_minSize;
uniform float u_randomWalkStrength;
uniform float u_randomWalkScale;   /*  = deltaTime (set from C++)           */
uniform float u_intensityMax;      /*  max field strength that maps to 1.0  */
uniform int   u_coloringMode;      /* 0=Intensity,1=Lifetime,2=Directional  */

/* gradient (uploaded from C++ with uploadGradientToCurrentProgram)   */
uniform int   u_stopCount;
uniform float u_stopPos[8];
uniform vec4  u_stopColor[8];

/*  helpers */
float random(vec3 p)
{
    p  = fract(p * 0.1031);
    p += dot(p, p.yxz + 19.19);
    return fract( (p.x + p.y) * p.z );
}

vec3 getColorFromGradient(float t)
{
    if (u_stopCount <= 1) return u_stopColor[0].rgb;

    t = clamp(t, 0.0, 1.0);
    for (int i = 0; i < u_stopCount - 1; ++i) {
        if (t >= u_stopPos[i] && t <= u_stopPos[i+1]) {
            float lt = (t - u_stopPos[i]) / (u_stopPos[i+1] - u_stopPos[i]);
            return mix(u_stopColor[i].rgb, u_stopColor[i+1].rgb, lt);
        }
    }
    return u_stopColor[u_stopCount-1].rgb;
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

/*  main */
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= particlesIn.length()) return;

    Particle p      = particlesIn[gid];
    vec3     wPos   = p.position.xyz;
    vec3     field  = vec3(0.0);

    /*  accumulate point & directional effectors  */
    for (int i = 0; i < u_pointEffectorCount; ++i) {
        vec3 diff = wPos - pointEffectors[i].position.xyz;
        float d   = length(diff);
        if (d > 0.001 && d < pointEffectors[i].radius) {
            float falloff = 1.0 - d / pointEffectors[i].radius;
            field += normalize(diff) * pointEffectors[i].strength * falloff;
        }
    }
    for (int i = 0; i < u_directionalEffectorCount; ++i)
        field += directionalEffectors[i].direction.xyz * directionalEffectors[i].strength;

    // 2. Accumulate Triangle Mesh Effectors
    for (int i = 0; i < u_triangleEffectorCount; ++i) {
        // This logic was missing. It's similar to the C++ FieldSolver.
        TriangleGpu tri = triangleEffectors[i];
        // The closestPointOnTriangle helper function already exists in your shader
        vec3 closestPoint = closestPointOnTriangle(wPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
        vec3 diff = wPos - closestPoint;
        float dist = length(diff);
        float radius = tri.normal.w; // Radius is stored in normal.w
        if (dist > 0.001 && dist < radius) {
            float strength = tri.v0.w; // Strength is stored in v0.w
            strength *= (1.0 - dist / radius);
            field += normalize(diff) * strength;
        }
    }

    /*  optional random-walk noise */
    if (u_randomWalkStrength > 0.0) {
        vec3 seed = wPos + vec3(u_time);
        vec3 turb = vec3( random(seed)      - 0.5,
                          random(seed.yxz) - 0.5,
                          random(seed.zyx) - 0.5 );
        field += normalize(turb) * u_randomWalkStrength * u_randomWalkScale;
    }

    /*  integrate motion */
    float fieldMag  = length(field);
    vec3  fieldDir  = (fieldMag > 0.001) ? normalize(field) : vec3(0.0);

    vec3  accel     = fieldDir * (u_baseSpeed + fieldMag * u_speedIntensityMultiplier);
    p.velocity.xyz += accel * u_deltaTime;
    p.position.xyz += p.velocity.xyz * u_deltaTime;
    p.velocity.xyz *= 0.98;   /* damp */

    /*  lifetime / respawn  */
    p.age += u_deltaTime;
    if (p.age > u_lifetime) {
        // --- START OF FIX #2: IMPROVED RANDOM RESPAWNING ---
        // Use different offsets for each component's seed to get more varied random numbers.
        vec3 seedX = vec3(gid, u_time, gid * 17.0 + u_time);
        vec3 seedY = vec3(gid * 3.0, u_time * 0.9, gid * 23.0 + u_time);
        vec3 seedZ = vec3(gid * 5.0, u_time * 1.1, gid * 29.0 + u_time);

        p.position.x = mix(u_boundsMin.x, u_boundsMax.x, random(seedX));
        p.position.y = mix(u_boundsMin.y, u_boundsMax.y, random(seedY));
        p.position.z = mix(u_boundsMin.z, u_boundsMax.z, random(seedZ));

        // Transform the newly spawned particle by the visualizer's model matrix
        p.position = u_visualizerModelMatrix * p.position;
        // --- END OF FIX #2 ---

        p.velocity = vec4(0.0);
        p.age      = 0.0;
    }

    /*  size & colour  */
    float age_t       = p.age / u_lifetime;
    float intensity_t = smoothstep(0.0, u_intensityMax, fieldMag);   /* 0-1 */

    /* size */
    p.size = max(u_minSize,
                 u_baseSize + (u_peakSizeMultiplier*u_baseSize - u_baseSize) * intensity_t);

    /* colour */
    if      (u_coloringMode == 0) p.color.rgb = getColorFromGradient(intensity_t);
    else if (u_coloringMode == 1) p.color.rgb = getColorFromGradient(age_t);
    else if (u_coloringMode == 2) p.color.rgb = 0.5 + 0.5*fieldDir;  /* direction RGB */
    p.color.a = 1.0;

    particlesOut[gid] = p;
}
