#version 430 core
// MLS-MPM thermal stage 4: gather grid temperature back to particles (Shepard-
// normalised over occupied nodes), exchange heat with the ambient field, and
// run the phase change — a solid whose temperature crosses its melt point is
// converted to fluid in-solver (its constitutive coefficients are swapped).
layout(local_size_x = 64) in;

struct Particle {
    vec4 posMass; vec4 velVol; vec4 c0, c1, c2; vec4 f0, f1, f2;
    vec4 plastic; vec4 matl; vec4 color;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 5) buffer TempB { float tb[]; };

uniform int   u_count;
uniform int   u_N;
uniform vec3  u_origin;
uniform float u_invDx;
uniform float u_ambientT;       // ambient temperature (heat reservoir)
uniform float u_heatExchange;   // Newton exchange rate with ambient (1/s)
uniform float u_dtFrame;
uniform float u_fluidK;         // bulk modulus assigned to newly-melted fluid

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    if (p[i].color.w <= 0.0) return;

    vec3 Xp = (p[i].posMass.xyz - u_origin) * u_invDx;
    ivec3 base = ivec3(floor(Xp - 0.5));
    vec3 fx = Xp - vec3(base);
    vec3 w0 = 0.5 * (1.5 - fx) * (1.5 - fx);
    vec3 w1 = 0.75 - (fx - 1.0) * (fx - 1.0);
    vec3 w2 = 0.5 * (fx - 0.5) * (fx - 0.5);
    vec3 wx = vec3(w0.x, w1.x, w2.x);
    vec3 wy = vec3(w0.y, w1.y, w2.y);
    vec3 wz = vec3(w0.z, w1.z, w2.z);

    float Tsum = 0.0;
    float wsum = 0.0;
    for (int a = 0; a < 3; ++a)
    for (int b = 0; b < 3; ++b)
    for (int c = 0; c < 3; ++c) {
        ivec3 node = base + ivec3(a, b, c);
        if (any(lessThan(node, ivec3(0))) || any(greaterThanEqual(node, ivec3(u_N)))) continue;
        float Tn = tb[(node.z * u_N + node.y) * u_N + node.x];
        if (Tn < -1.0e8) continue;
        float wt = wx[a] * wy[b] * wz[c];
        Tsum += wt * Tn;
        wsum += wt;
    }
    float Tnew = (wsum > 1.0e-6) ? Tsum / wsum : p[i].plastic.y;
    // Newton exchange with the ambient field (the external heat source/sink).
    Tnew += u_heatExchange * u_dtFrame * (u_ambientT - Tnew);
    p[i].plastic.y = Tnew;

    // Phase change: a solid (matType > 0) that crosses its melt threshold
    // becomes fluid — swap the constitutive coefficients and reset F so no
    // frozen-in shear remains.
    float matType = p[i].matl.w;
    float meltT = p[i].plastic.w;
    if (matType > 0.5 && Tnew >= meltT) {
        p[i].matl = vec4(u_fluidK, 7.0, 0.0, 0.0);  // (K, gamma, -, type=Fluid)
        p[i].f0 = vec4(1, 0, 0, 0);
        p[i].f1 = vec4(0, 1, 0, 0);
        p[i].f2 = vec4(0, 0, 1, 0);
        p[i].plastic.x = 1.0;                        // J reset
        p[i].color.rgb = mix(p[i].color.rgb, vec3(0.2, 0.45, 0.85), 0.6); // tint to water
    }
}
