// File: prefilter_env_frag.glsl
#version 450 core
out vec4 fragColor;

// This 'in' name must exactly match the 'out' name from your vertex shader.
// If your vertex shader uses 'out vec3 WorldDir;', change this to 'in vec3 WorldDir;'
in vec3 localPos;

// These uniform names must exactly match what your C++ code is looking for.
uniform samplerCube environmentMap;
uniform float roughness;

const float PI = 3.14159265359;
// You can lower this sample count to 1024 or 512 for faster generation
const uint SAMPLE_COUNT = 2048u; 

// --- Helper Functions (unchanged) ---
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

// --- Main Function ---
void main()
{		
    vec3 N = normalize(localPos);
    vec3 R = N;
    vec3 V = R;

    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            // The blur comes from averaging many sharp samples.
            // Always sample from the highest resolution (mip 0).
            prefilteredColor += textureLod(environmentMap, L, 0.0).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }
    
    prefilteredColor = (totalWeight > 0.0) ? (prefilteredColor / totalWeight) : vec3(0.0);

    fragColor = vec4(prefilteredColor, 1.0);
}