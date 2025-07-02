#version 430 core

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// --- GPU Data Structures (must match GpuResources.hpp) ---
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

// --- Buffer Definitions ---
struct InstanceData {
    mat4 modelMatrix;
    vec4 color;
};

layout(std430, binding = 0) readonly buffer SamplePointsBuffer { vec4 samplePoints[]; };
layout(std430, binding = 1) buffer InstanceOutputBuffer { InstanceData instanceData[]; };
layout(std430, binding = 2) buffer DrawCommandUbo {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
} drawCommand;

layout(std140, binding = 3) uniform EffectorDataUbo {
    PointEffectorGpu pointEffectors[256];
    DirectionalEffectorGpu directionalEffectors[16];
};

layout(std430, binding = 4) readonly buffer TriangleEffectorBuffer {
    TriangleGpu triangleEffectors[];
};

// --- Uniforms ---
uniform mat4 u_visualizerModelMatrix;
uniform float u_vectorScale;
uniform float u_arrowHeadScale;
uniform float u_cullingThreshold;
uniform int u_pointEffectorCount;
uniform int u_directionalEffectorCount;
uniform int u_triangleEffectorCount;

// --- Helper Functions ---
mat4 rotationBetweenVectors(vec3 start, vec3 dest) {
    start = normalize(start);
    dest = normalize(dest);
    vec3 v = cross(start, dest);
    float c = dot(start, dest);
    if (c > 0.99999) {
        return mat4(1.0);
    }
    if (c < -0.99999) {
        // Handle 180 degree rotation by creating a rotation matrix around an arbitrary axis (e.g., Y-axis)
        // This is more robust than returning mat4(-1.0) which is not a valid rotation matrix.
        mat4 rotation;
        rotation[0] = vec4(-1, 0, 0, 0);
        rotation[1] = vec4(0, -1, 0, 0);
        rotation[2] = vec4(0, 0, -1, 0);
        rotation[3] = vec4(0, 0, 0, 1);
        return rotation;
    }
    mat3 vx = mat3(0, v.z, -v.y, -v.z, 0, v.x, v.y, -v.x, 0);
    mat3 R = mat3(1.0) + vx + vx * vx * (1.0 / (1.0 + c));
    return mat4(R);
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
    if (gid >= samplePoints.length()) return;

    if (gid == 0) drawCommand.instanceCount = 0;

    vec3 worldPos = (u_visualizerModelMatrix * samplePoints[gid]).xyz;
    vec3 totalField = vec3(0.0);

    // Point and Spline Proxy Effectors
    for (int i = 0; i < u_pointEffectorCount; ++i) {
        // CORRECTED: Vector points from effector to sample point (worldPos - position).
        // A positive strength (emitter) pushes away. A negative strength (attractor) pulls inward.
        vec3 diff = worldPos - pointEffectors[i].position.xyz;
        float dist = length(diff);
        if (dist < pointEffectors[i].radius && dist > 0.001) {
            float strength = pointEffectors[i].strength;
            vec3 forceDir;
            if (pointEffectors[i].falloffType == 1) { // Linear Falloff
                strength *= (1.0 - dist / pointEffectors[i].radius);
            }
            if (length(pointEffectors[i].normal.xyz) > 0.1) { // If a specific direction is provided
                forceDir = normalize(pointEffectors[i].normal.xyz);
            } else { // Otherwise, use the direction to/from the point
                forceDir = normalize(diff);
            }
            totalField += forceDir * strength;
        }
    }

    // Triangle Mesh Effectors
    for (int i = 0; i < u_triangleEffectorCount; ++i) {
        TriangleGpu tri = triangleEffectors[i];
        vec3 closestPoint = closestPointOnTriangle(worldPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
        
        // CORRECTED: Vector points from mesh surface to sample point (worldPos - closestPoint).
        vec3 diff = worldPos - closestPoint;
        float dist = length(diff);
        
        float radius = tri.normal.w; // Radius is packed in the normal's w component.
        if (dist > 0.001 && dist < radius) {
            float strength = tri.v0.w; // Strength is packed in v0.w
            strength *= (1.0 - dist / radius); // Linear falloff
            
            totalField += normalize(diff) * strength;
        }
    }

    // Directional Effectors
    for (int i = 0; i < u_directionalEffectorCount; ++i) {
        totalField += directionalEffectors[i].direction.xyz * directionalEffectors[i].strength;
    }

    // --- Build Arrow Instance ---
    float magnitude = length(totalField);
    if (magnitude > u_cullingThreshold) {
        uint instanceIndex = atomicAdd(drawCommand.instanceCount, 1);
        
        mat4 trans = mat4(1.0);
        trans[3] = vec4(worldPos, 1.0);

        // CORRECTED: The final field vector is negated to flip all arrow directions.
        mat4 rot = rotationBetweenVectors(vec3(0.0, 0.0, -1.0), normalize(-totalField));

        // MODIFIED: Scale the arrow's width (X and Y axes) by the field magnitude,
        // while keeping its length (Z axis) constant.
        mat4 scale = mat4(1.0);
        float dynamicWidth = u_arrowHeadScale * abs(magnitude); // Use abs() for width scaling
        scale[0][0] = dynamicWidth;                             // Scale X axis (width).
        scale[1][1] = dynamicWidth;                             // Scale Y axis (width).
        scale[2][2] = u_vectorScale;                            // Keep Z axis (length) at a constant scale.
        
        // Color interpolation based on field magnitude
        float normMag = clamp(abs(magnitude) / 5.0, 0.0, 1.0);
        vec4 color = vec4(mix(vec3(0.2, 0.5, 1.0), vec3(1.0, 0.3, 0.3), normMag), 1.0);
        
        instanceData[instanceIndex].modelMatrix = trans * rot * scale;
        instanceData[instanceIndex].color = color;
    }
}
