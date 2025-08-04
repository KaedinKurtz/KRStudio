#version 430 core
in vec3 WorldDir;
out vec4 FragColor;

uniform samplerCube environmentMap;
const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(WorldDir);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    // sample parameters
    const uint SAMPLE_COUNT = 1024u;
    vec3 irradiance = vec3(0.0);

    // tangent space integration
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        // Hammersley / importance sampling omitted for brevity;
        // here just uniformly sample hemisphere
        vec2 xi = vec2(
            float(i) / float(SAMPLE_COUNT),
            fract(sin(float(i) * 12.9898) * 43758.5453)
        );
        float phi = 2.0 * PI * xi.x;
        float cosTheta = 1.0 - xi.y;
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 sampleDir = vec3(cos(phi)*sinTheta, sinTheta*0.0 + cosTheta, sin(phi)*sinTheta);
        // build world-space sample vector
        vec3 L = normalize(sampleDir.x * right + sampleDir.y * up + sampleDir.z * N);
        irradiance += texture(environmentMap, L).rgb * cosTheta;
    }
    irradiance = PI * irradiance / float(SAMPLE_COUNT);
    FragColor = vec4(irradiance, 1.0);
}
