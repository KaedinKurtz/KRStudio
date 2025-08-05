#version 450 core
out vec4 fragColor;
in vec3 localPos;

uniform samplerCube environmentMap;
uniform float roughness;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 8192u; // Increased for final quality

// Unchanged helper functions
vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

void main()
{		
    vec3 N = normalize(localPos);
    vec3 R = N;
    vec3 V = R;

    vec3 prefilteredColor = vec3(0.0);
    
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        // --- THE SOLUTION ---
        // Your C++ code generates 5 mip levels (0-4). The max level is 4.0.
        const float MAX_REFLECTION_LOD = 4.0;
        
        // Calculate the desired mip level (the "blur") based ONLY on roughness.
        float mipLevel = roughness * MAX_REFLECTION_LOD;
        
        // Use textureLod() to sample the environment map at our calculated mipLevel,
        // IGNORING the screen-space distance. This is the key.
        prefilteredColor += textureLod(environmentMap, L, mipLevel).rgb;
    }
    
    // Normalize by the total number of samples.
    prefilteredColor = prefilteredColor / float(SAMPLE_COUNT);

    fragColor = vec4(prefilteredColor, 1.0);
}