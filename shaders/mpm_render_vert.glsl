#version 430 core
// MLS-MPM particle billboards. Positions pulled from the solver SSBO; the splat
// is recoloured per visualization mode (Phase 3): temperature, von Mises stress
// or strain, computed per-particle from the deformation gradient F.
struct Particle {
    vec4 posMass;
    vec4 velVol;
    vec4 c0, c1, c2;
    vec4 f0, f1, f2;
    vec4 plastic;   // x Jp/J, y temperature(C), z heatCap, w meltTemp
    vec4 matl;      // x mu, y lambda, z alpha, w materialType
    vec4 color;     // rgb, w alive
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_viewportHeight;
uniform float u_radius;
uniform int  u_vizMode;     // 0 Default(albedo) 1 Thermal 2 VonMises 3 Strain
uniform float u_rangeMin;   // scalar mapped to the cold end of the ramp
uniform float u_rangeMax;   // scalar mapped to the hot end

out vec3 vColor;
out float vSpeed;
out float vViz;             // >0.5 in a viz mode: flatten shading so the ramp reads

// Perceptual cold->hot ramp (blue -> cyan -> green -> yellow -> red -> white).
vec3 ramp(float t)
{
    t = clamp(t, 0.0, 1.0);
    const vec3 c0 = vec3(0.05, 0.05, 0.35); // cold
    const vec3 c1 = vec3(0.10, 0.55, 0.85);
    const vec3 c2 = vec3(0.15, 0.80, 0.30);
    const vec3 c3 = vec3(0.95, 0.85, 0.15);
    const vec3 c4 = vec3(0.90, 0.25, 0.10);
    const vec3 c5 = vec3(1.00, 0.95, 0.90); // hot
    if (t < 0.2) return mix(c0, c1, t / 0.2);
    if (t < 0.4) return mix(c1, c2, (t - 0.2) / 0.2);
    if (t < 0.6) return mix(c2, c3, (t - 0.4) / 0.2);
    if (t < 0.8) return mix(c3, c4, (t - 0.6) / 0.2);
    return mix(c4, c5, (t - 0.8) / 0.2);
}

void main()
{
    Particle pp = p[gl_VertexID];
    if (pp.color.w <= 0.0) {        // dead/parked particle -> cull off-screen
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        return;
    }
    vSpeed = length(pp.velVol.xyz);
    vViz = (u_vizMode == 0) ? 0.0 : 1.0;

    if (u_vizMode == 0) {
        vColor = pp.color.rgb;                      // photoreal / body albedo
    } else {
        mat3 F = mat3(pp.f0.xyz, pp.f1.xyz, pp.f2.xyz);
        float scalar = 0.0;
        if (u_vizMode == 1) {                       // Thermal: particle temperature
            scalar = pp.plastic.y;
        } else {
            // Green-Lagrange strain E = 0.5(F^T F - I): SVD-free, frame-invariant.
            mat3 E = 0.5 * (transpose(F) * F - mat3(1.0));
            if (u_vizMode == 3) {                   // Strain: ||E|| (Frobenius)
                scalar = sqrt(dot(E[0], E[0]) + dot(E[1], E[1]) + dot(E[2], E[2]));
            } else {                                // VonMises: StVK stress invariant
                float mu = pp.matl.x, lambda = pp.matl.y;
                float trE = E[0][0] + E[1][1] + E[2][2];
                mat3 s = 2.0 * mu * E + lambda * trE * mat3(1.0); // 2nd Piola-Kirchhoff (~Cauchy, small strain)
                float a = s[0][0], b = s[1][1], c = s[2][2];
                float d = s[1][0], e = s[2][1], f = s[2][0];
                scalar = sqrt(0.5 * ((a - b) * (a - b) + (b - c) * (b - c) + (c - a) * (c - a))
                              + 3.0 * (d * d + e * e + f * f));
            }
        }
        float t = (scalar - u_rangeMin) / max(u_rangeMax - u_rangeMin, 1e-6);
        vColor = ramp(t);
    }

    vec4 vp = u_view * vec4(pp.posMass.xyz, 1.0);
    gl_Position = u_projection * vp;
    float dist = max(0.1, -vp.z);
    gl_PointSize = clamp(u_radius * 2.2 * u_viewportHeight * u_projection[1][1] / dist, 1.0, 64.0);
}
