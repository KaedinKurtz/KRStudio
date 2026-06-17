#version 430 core
// MLS-MPM stage 3: Grid -> Particle gather. Reconstruct velocity and the APIC
// affine C, advect, update the deformation gradient F = (I + dt C) F, then run
// the per-material plastic return map. Hu 2018 / Klar 2016 / Stomakhin 2013.
layout(local_size_x = 64) in;

struct Particle {
    vec4 posMass;
    vec4 velVol;
    vec4 c0, c1, c2;
    vec4 f0, f1, f2;
    vec4 plastic;   // x Jp (fluid: J), y temperature, z heatCap, w meltTemp
    vec4 matl;      // solids:(mu,lambda,alpha) fluid:(K,gamma) ; w = matType
    vec4 color;
    vec4 therm2;    // x thermalConductivity k (W/m.K); yzw reserved
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 2) buffer GridVel { vec4 gv[]; };

uniform int   u_count;
uniform int   u_N;
uniform vec3  u_origin;
uniform float u_dx;
uniform float u_invDx;
uniform float u_dt;
// Snow plastic clamp bounds.
uniform float u_thetaC;   // critical compression (e.g. 0.025)
uniform float u_thetaS;   // critical stretch     (e.g. 0.0075)
// Floor: no-penetration plane (world y) + particle radius, so SURFACES (not
// centres) stop at the floor.
uniform float u_floorY;   // world-y of the floor plane (= origin.y + bound*dx)
uniform float u_radius;   // particle effective radius (= splat radius = dx)
uniform float u_picBlend;    // SAND only: per-substep APIC->PIC affine blend [0..1] (0 = pure APIC)
uniform float u_velDampRate; // SAND only: dt-scaled velocity bleed [1/s] to settle skating (0 = none)

void jacobiRotateSym(inout mat3 S, inout mat3 V, int pp, int qq)
{
    float spq = S[qq][pp];
    if (abs(spq) < 1e-20) return;
    float spp = S[pp][pp];
    float sqq = S[qq][qq];
    float tau = (sqq - spp) / (2.0 * spq);
    float t = (tau == 0.0) ? 1.0 : sign(tau) / (abs(tau) + sqrt(1.0 + tau * tau));
    float c = inversesqrt(1.0 + t * t);
    float s = t * c;
    int r = 3 - pp - qq;
    float spr = S[r][pp];
    float sqr = S[r][qq];
    float npp = c*c*spp - 2.0*s*c*spq + s*s*sqq;
    float nqq = s*s*spp + 2.0*s*c*spq + c*c*sqq;
    float npr = c*spr - s*sqr;
    float nqr = s*spr + c*sqr;
    S[pp][pp] = npp; S[qq][qq] = nqq;
    S[qq][pp] = 0.0; S[pp][qq] = 0.0;
    S[r][pp] = npr; S[pp][r] = npr;
    S[r][qq] = nqr; S[qq][r] = nqr;
    float v0p = V[pp][0], v1p = V[pp][1], v2p = V[pp][2];
    float v0q = V[qq][0], v1q = V[qq][1], v2q = V[qq][2];
    V[pp][0] = c*v0p - s*v0q;  V[qq][0] = s*v0p + c*v0q;
    V[pp][1] = c*v1p - s*v1q;  V[qq][1] = s*v1p + c*v1q;
    V[pp][2] = c*v2p - s*v2q;  V[qq][2] = s*v2p + c*v2q;
}

void svd3x3(in mat3 F, out mat3 U, out vec3 Sigma, out mat3 V)
{
    mat3 S = transpose(F) * F;
    V = mat3(1.0);
    for (int sweep = 0; sweep < 4; ++sweep) {
        jacobiRotateSym(S, V, 0, 1);
        jacobiRotateSym(S, V, 0, 2);
        jacobiRotateSym(S, V, 1, 2);
    }
    vec3 lambda = vec3(max(S[0][0], 0.0), max(S[1][1], 0.0), max(S[2][2], 0.0));
    Sigma = sqrt(lambda);
    if (Sigma.x < Sigma.y) { float ts=Sigma.x; Sigma.x=Sigma.y; Sigma.y=ts; vec3 tv=V[0]; V[0]=V[1]; V[1]=tv; }
    if (Sigma.y < Sigma.z) { float ts=Sigma.y; Sigma.y=Sigma.z; Sigma.z=ts; vec3 tv=V[1]; V[1]=V[2]; V[2]=tv; }
    if (Sigma.x < Sigma.y) { float ts=Sigma.x; Sigma.x=Sigma.y; Sigma.y=ts; vec3 tv=V[0]; V[0]=V[1]; V[1]=tv; }
    if (determinant(V) < 0.0) V[2] = -V[2];
    mat3 FV = F * V;
    const float EPS = 1e-12;
    U[0] = (Sigma.x > EPS) ? FV[0] / Sigma.x : vec3(1.0, 0.0, 0.0);
    U[1] = (Sigma.y > EPS) ? FV[1] / Sigma.y : vec3(0.0, 1.0, 0.0);
    U[2] = (Sigma.z > EPS) ? FV[2] / Sigma.z : vec3(0.0, 0.0, 1.0);
    if (Sigma.z <= EPS) {
        if (Sigma.y > EPS) {
            U[2] = normalize(cross(U[0], U[1]));
        } else if (Sigma.x > EPS) {
            vec3 u0 = U[0];
            vec3 tt = (abs(u0.x) < 0.9) ? vec3(1,0,0) : vec3(0,1,0);
            U[1] = normalize(cross(u0, tt));
            U[2] = normalize(cross(u0, U[1]));
        } else { U = mat3(1.0); }
    }
    if (determinant(U) < 0.0) { U[2] = -U[2]; Sigma.z = -Sigma.z; }
}

mat3 diagm(vec3 d) { return mat3(d.x,0,0, 0,d.y,0, 0,0,d.z); }

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    if (p[i].color.w <= 0.0) return;

    vec3 pos = p[i].posMass.xyz;
    vec3 Xp = (pos - u_origin) * u_invDx;
    ivec3 base = ivec3(floor(Xp - 0.5));
    vec3 fx = Xp - vec3(base);
    vec3 w0 = 0.5 * (1.5 - fx) * (1.5 - fx);
    vec3 w1 = 0.75 - (fx - 1.0) * (fx - 1.0);
    vec3 w2 = 0.5 * (fx - 0.5) * (fx - 0.5);
    vec3 wx = vec3(w0.x, w1.x, w2.x);
    vec3 wy = vec3(w0.y, w1.y, w2.y);
    vec3 wz = vec3(w0.z, w1.z, w2.z);

    float Dinv = 4.0 * u_invDx * u_invDx;
    vec3 newV = vec3(0.0);
    mat3 newC = mat3(0.0);
    for (int a = 0; a < 3; ++a)
    for (int b = 0; b < 3; ++b)
    for (int c = 0; c < 3; ++c) {
        ivec3 node = base + ivec3(a, b, c);
        if (any(lessThan(node, ivec3(0))) || any(greaterThanEqual(node, ivec3(u_N)))) continue;
        float wt = wx[a] * wy[b] * wz[c];
        vec3 dpos = (vec3(a, b, c) - fx) * u_dx;
        int cell = (node.z * u_N + node.y) * u_N + node.x;
        vec3 g = gv[cell].xyz;
        newV += wt * g;
        newC += (Dinv * wt) * outerProduct(g, dpos);
    }

    // SAND SETTLING FIX (u_picBlend>0): an explicit MLS-MPM granular column never settles -- it skates because
    // the AFFINE modes (newC -> F -> stress -> grid force) keep INJECTING energy faster than the floor friction
    // (mu*g*dt) removes it. Dissipate it: blend the affine matrix toward PIC (newC*(1-blend) -- pure PIC is C=0,
    // the maximally-dissipative limit) and bleed a small fraction of the spurious velocity. Applied ONLY to SAND
    // (a dissipative material, so this is physical, not a hack); fluid/elastic/snow are untouched (blend skipped).
    float matType = p[i].matl.w;
    if (matType > 1.5 && matType < 2.5) {                 // SAND only (fluid/elastic/snow untouched)
        // Settle a granular column that otherwise SKATES (~15 m/s, spurious MLS-MPM energy the weak floor
        // friction can't remove). Two dissipations, both OFF in the neg-control (rates = 0):
        //  - u_picBlend: a small per-substep affine (APIC->PIC) blend (the high-frequency injecting modes).
        //  - u_velDampRate: a dt-SCALED velocity bleed [1/s] -- dt-scaled so it is INDEPENDENT of the substep
        //    count. Tuned so the FAST gravity slump (~0.3 s) survives but the PERSISTENT skating (~1.5 s) decays,
        //    leaving the Drucker-Prager friction to set the repose angle.
        if (u_picBlend > 0.0)    newC *= (1.0 - u_picBlend);
        if (u_velDampRate > 0.0) newV *= max(0.0, 1.0 - u_velDampRate * u_dt);
    }

    // Advect, then clamp so the 3^3 stencil always stays inside the grid.
    pos += u_dt * newV;
    float domain = float(u_N) * u_dx;
    vec3 lo = u_origin + 1.6 * u_dx;
    vec3 hi = u_origin + vec3(domain) - 1.6 * u_dx;
    pos = clamp(pos, lo, hi);
    // Floor: keep the particle SURFACE above the floor plane (centre >= floor +
    // radius), so the rendered splat rests ON the floor instead of sinking ~half
    // a cell. This is the position-level backstop to the grid velocity BC.
    pos.y = max(pos.y, u_floorY + u_radius);

    mat3 F = mat3(p[i].f0.xyz, p[i].f1.xyz, p[i].f2.xyz);   // matType read above (for the sand settling blend)

    if (matType < 0.5) {
        // FLUID — evolve volume only (track J), never accumulate shear in F.
        float J = p[i].plastic.x;
        J *= (1.0 + u_dt * (newC[0][0] + newC[1][1] + newC[2][2]));
        J = clamp(J, 0.1, 4.0);
        p[i].plastic.x = J;
        // F kept as identity for the fluid branch.
    } else {
        mat3 Fnew = (mat3(1.0) + u_dt * newC) * F;
        if (matType > 0.5 && matType < 1.5) {
            // ELASTIC — purely reversible, no return map.
            F = Fnew;
        } else if (matType > 1.5 && matType < 2.5) {
            // SAND — Drucker-Prager return map in STRESS space (pressure-dependent friction cone).
            // The earlier Hencky-STRAIN form coupled friction to the volumetric STRAIN trEps, so a near-isochoric
            // granular flow (trEps~0) saw ~zero friction -> phi-INDEPENDENT piles (the repose red). Here the
            // friction resists shear UNDER PRESSURE: the cone radius -3*alpha*p grows with the confining pressure
            // p, so a confined pile holds shear at the friction angle while a frictionless (alpha=0) pile, whose
            // cone radius is 0, spreads flat. phi now controls the repose angle.
            float mu = p[i].matl.x;
            float lambda = p[i].matl.y;
            float alpha = p[i].matl.z;
            mat3 U; vec3 sig; mat3 V;
            svd3x3(Fnew, U, sig, V);
            vec3 eps = log(max(abs(sig), vec3(1e-9)));
            float trEps = eps.x + eps.y + eps.z;
            vec3 tau = 2.0 * mu * eps + vec3(lambda * trEps);   // trial principal Kirchhoff stress
            float pmean = (tau.x + tau.y + tau.z) / 3.0;        // mean stress (< 0 under compression)
            vec3 s = tau - vec3(pmean);                         // deviatoric stress
            float sNorm = length(s);
            vec3 SigProj;
            if (pmean >= 0.0) {
                SigProj = vec3(1.0);                            // tension: cohesionless -> stress-free tip
            } else {
                float coneR = -3.0 * alpha * pmean;             // DP cone radius (>=0): max deviatoric stress at p
                if (sNorm <= coneR || sNorm < 1e-12) {
                    SigProj = sig;                              // inside the cone -> elastic, HOLDS the shear
                } else {
                    vec3 tauProj = s * (coneR / sNorm) + vec3(pmean);   // radial return to the cone surface
                    float trTau = tauProj.x + tauProj.y + tauProj.z;
                    vec3 epsNew = (tauProj - vec3(lambda * trTau / (2.0 * mu + 3.0 * lambda))) / (2.0 * mu);
                    SigProj = exp(epsNew);                      // invert stress -> strain -> stretches
                }
            }
            F = U * diagm(SigProj) * transpose(V);
        } else {
            // SNOW — box-clamp principal stretches (Stomakhin 2013).
            mat3 U; vec3 sig; mat3 V;
            svd3x3(Fnew, U, sig, V);
            vec3 SigC = clamp(sig, vec3(1.0 - u_thetaC), vec3(1.0 + u_thetaS));
            F = U * diagm(SigC) * transpose(V);
        }
    }

    // Write back.
    p[i].posMass.xyz = pos;
    p[i].velVol.xyz = newV;
    p[i].c0.xyz = newC[0]; p[i].c1.xyz = newC[1]; p[i].c2.xyz = newC[2];
    p[i].f0.xyz = F[0]; p[i].f1.xyz = F[1]; p[i].f2.xyz = F[2];
}
