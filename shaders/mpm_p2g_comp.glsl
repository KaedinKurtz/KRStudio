#version 430 core
// MLS-MPM stage 1: Particle -> Grid scatter (mass, APIC momentum, internal
// force impulse folded in). Hu et al. 2018; D^-1 = 4/dx^2 for quadratic
// B-splines. Fixed-point int atomics (GL 4.3 has no float atomics).
layout(local_size_x = 64) in;

// Particle layout: 11 vec4 (see MpmSystem.cpp). C and F are stored as the
// THREE COLUMNS of each mat3 (c0/c1/c2 = columns of C; f0/f1/f2 = cols of F).
struct Particle {
    vec4 posMass;   // xyz pos (world), w mass
    vec4 velVol;    // xyz vel, w V0 (rest volume)
    vec4 c0, c1, c2; // APIC affine matrix C (columns)
    vec4 f0, f1, f2; // deformation gradient F (columns)
    vec4 plastic;   // x Jp (fluid: J), y temperature, z heatCap, w meltTemp
    vec4 matl;      // solids:(mu,lambda,alpha) fluid:(K,gamma) ; w = matType
    vec4 color;     // rgb, w alive(>0)
    vec4 therm2;    // x thermalConductivity k (W/m.K); yzw reserved
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) coherent buffer GridInt { int gi[]; };

uniform int   u_count;
uniform int   u_N;
uniform vec3  u_origin;
uniform float u_dx;
uniform float u_invDx;
uniform float u_dt;

const float SCALE = 1.0e7;

int enc(float f) { return clamp(int(round(f * SCALE)), -2000000000, 2000000000); }

// ---------------------------------------------------------------------------
// Robust branch-light 3x3 SVD (McAdams 2011 / Stomakhin 2013 convention):
// cyclic Jacobi on S=F^T F -> V, then U=F V Sigma^-1, sign pushed onto the
// smallest singular value so det(U),det(V) > 0 and U diag(S) V^T == F.
// ---------------------------------------------------------------------------
void jacobiRotateSym(inout mat3 S, inout mat3 V, int pq0, int pq1)
{
    int pp = pq0; int qq = pq1;
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
        } else {
            U = mat3(1.0);
        }
    }
    if (determinant(U) < 0.0) { U[2] = -U[2]; Sigma.z = -Sigma.z; }
}

mat3 diagm(vec3 d) { return mat3(d.x,0,0, 0,d.y,0, 0,0,d.z); }

// Kirchhoff stress tau = P F^T = J * sigma_cauchy, as the matrix used by the
// MLS-MPM affine: affine = m*C - dt * V0 * (4/dx^2) * tau.
mat3 computeTau(in Particle pp)
{
    float matType = pp.matl.w;
    mat3 F = mat3(pp.f0.xyz, pp.f1.xyz, pp.f2.xyz);

    if (matType < 0.5) {
        // FLUID — weakly-compressible Navier-Stokes: Tait EOS pressure plus an
        // explicit Newtonian viscous stress from the velocity gradient C.
        float J = max(pp.plastic.x, 1e-4);
        float K = pp.matl.x;
        float gamma = pp.matl.y;
        float visc = pp.matl.z;            // dynamic viscosity (Pa.s)
        float pres = K * (pow(1.0 / J, gamma) - 1.0);
        mat3 tau = mat3(-J * pres);        // Kirchhoff = J * (-p I)
        if (visc > 0.0) {
            mat3 C = mat3(pp.c0.xyz, pp.c1.xyz, pp.c2.xyz);
            mat3 D = 0.5 * (C + transpose(C));            // strain-rate tensor
            float trD = (D[0][0] + D[1][1] + D[2][2]) / 3.0;
            mat3 dev = D - mat3(trD);                     // deviatoric
            tau += (2.0 * visc * J) * dev;                // Kirchhoff = J * Cauchy
        }
        return tau;
    }

    float mu = pp.matl.x;
    float lambda = pp.matl.y;
    mat3 U; vec3 sig; mat3 V;
    svd3x3(F, U, sig, V);

    if (matType > 1.5 && matType < 2.5) {
        // SAND (2) — StVK-Hencky elastic stress in the principal frame.
        vec3 lnSig = log(max(abs(sig), vec3(1e-9)));
        float trEps = lnSig.x + lnSig.y + lnSig.z;
        vec3 tauP = 2.0 * mu * lnSig + vec3(lambda * trEps);
        return U * diagm(tauP) * transpose(U);
    }

    // ELASTIC (1) and SNOW (3): fixed-corotated. Snow's hardening is already
    // folded into mu/lambda by the G2P pass (see mpm_g2p).
    mat3 R = U * transpose(V);
    float J = sig.x * sig.y * sig.z; // det(F), sign-consistent with the SVD
    mat3 Ft = transpose(F);
    return 2.0 * mu * (F - R) * Ft + mat3(lambda * (J - 1.0) * J);
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    Particle pp = p[i];
    if (pp.color.w <= 0.0) return;
    float mass = pp.posMass.w;
    if (mass <= 0.0) return;

    vec3 pos = pp.posMass.xyz;
    vec3 vel = pp.velVol.xyz;
    float V0 = pp.velVol.w;
    mat3 C = mat3(pp.c0.xyz, pp.c1.xyz, pp.c2.xyz);

    float Dinv = 4.0 * u_invDx * u_invDx;
    mat3 tau = computeTau(pp);
    mat3 affine = mass * C - (u_dt * V0 * Dinv) * tau;

    vec3 Xp = (pos - u_origin) * u_invDx;
    ivec3 base = ivec3(floor(Xp - 0.5));
    vec3 fx = Xp - vec3(base);
    vec3 w0 = 0.5 * (1.5 - fx) * (1.5 - fx);
    vec3 w1 = 0.75 - (fx - 1.0) * (fx - 1.0);
    vec3 w2 = 0.5 * (fx - 0.5) * (fx - 0.5);
    vec3 wx = vec3(w0.x, w1.x, w2.x);
    vec3 wy = vec3(w0.y, w1.y, w2.y);
    vec3 wz = vec3(w0.z, w1.z, w2.z);

    for (int a = 0; a < 3; ++a)
    for (int b = 0; b < 3; ++b)
    for (int c = 0; c < 3; ++c) {
        ivec3 node = base + ivec3(a, b, c);
        if (any(lessThan(node, ivec3(0))) || any(greaterThanEqual(node, ivec3(u_N)))) continue;
        float wt = wx[a] * wy[b] * wz[c];
        vec3 dpos = (vec3(a, b, c) - fx) * u_dx;
        vec3 mom = wt * (mass * vel + affine * dpos);
        float dm = wt * mass;
        int cell = (node.z * u_N + node.y) * u_N + node.x;
        atomicAdd(gi[cell * 4 + 0], enc(mom.x));
        atomicAdd(gi[cell * 4 + 1], enc(mom.y));
        atomicAdd(gi[cell * 4 + 2], enc(mom.z));
        atomicAdd(gi[cell * 4 + 3], enc(dm));
    }
}
