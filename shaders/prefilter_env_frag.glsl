#version 430 core
in vec3 WorldDir;
out vec4 FragColor;

uniform sampler2D equirectangularMap; // or samplerCube if you already converted
uniform float roughness;
uniform mat4 view;
uniform mat4 projection;

// … same importance?sampling functions (GGX, Hammersley) as in lighting …

void main() {
    vec3 N = normalize(WorldDir);
    vec3 R = N, V = R; // view=reflect direction
    const uint SAMPLE_COUNT = 1024u;
    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            prefiltered += texture(environmentMap, L).rgb * NoL;
            totalWeight += NoL;
        }
    }
    prefiltered = prefiltered / totalWeight;
    FragColor = vec4(prefiltered, 1.0);
}
