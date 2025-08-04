#version 430 core
in vec2 TexCoords;
out vec2 FragColor;
const float PI = 3.14159265359;

// you can copy a standard BRDF integration routine here:
void main() {
    float NdotV = TexCoords.x;
    float roughness = TexCoords.y;
    vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);

    const uint SAMPLE_COUNT = 1024u;
    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, V, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);

        if (NoL > 0.0) {
            float G = GeometrySmith(NdotV, NoL, roughness);
            float G_Vis = (G * VoH) / (NoH * NdotV);
            float Fc = pow(1.0 - VoH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    FragColor = vec2(A, B);
}
