#version 430 core
in vec3 WorldDir;
out vec4 FragColor;

uniform samplerCube environmentMap;
const float PI = 3.14159265359;

// Van der Corput radical inverse (base 2) -> low-discrepancy Hammersley sequence.
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

void main() {
    vec3 N = normalize(WorldDir);

    // Orthonormal tangent basis around N.
    vec3 up    = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    const uint SAMPLE_COUNT = 4096u;
    vec3 irradiance = vec3(0.0);

    // CORRECT diffuse-irradiance estimator: cosine-weighted hemisphere importance
    // sampling (Malley's method). Draw directions with pdf = cosTheta / PI, so the
    // cosTheta weight in the irradiance integral and the 1/pdf factor cancel, leaving
    //     E(N) = (PI / N) * sum_i L(omega_i)
    // which is unbiased for E = integral_hemisphere L(omega) cos(theta) dω.
    // Analytic check (the IRRADIANCE-CORRECT gate): a uniform environment of radiance
    // L gives E = (PI/N) * N * L = PI*L. (The previous code drew a non-cosine theta
    // ramp, multiplied by an EXTRA cosTheta, and normalized by PI/N -> it returned
    // ~PI/2*L, i.e. half the correct irradiance, which a hand-tuned low iblIntensity
    // was silently compensating for.)
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        float phi      = 2.0 * PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);   // cosine-weighted (Malley)
        float sinTheta = sqrt(xi.y);
        vec3 tangentSample = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 L = normalize(tangentSample.x * right + tangentSample.y * up + tangentSample.z * N);

        // HDR radiance clamp (kept for now; Phase 3 reconciles whether it is still
        // needed with correct normalization). Does not affect the analytic gate
        // (uniform L=1 < 4) — only reins in over-bright sky/sun spikes.
        irradiance += min(texture(environmentMap, L).rgb, vec3(4.0));
    }
    irradiance = PI * irradiance / float(SAMPLE_COUNT);
    FragColor = vec4(irradiance, 1.0);
}
