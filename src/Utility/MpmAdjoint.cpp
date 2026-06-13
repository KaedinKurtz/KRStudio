#include "MpmAdjoint.hpp"

#include <glm/gtc/matrix_access.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace krs::mpmad {

// glm dmat3 is column-major: m[col][row]. These helpers index in (row,col).
static inline double E(const mat3& m, int r, int c) { return m[c][r]; }
static inline void setE(mat3& m, int r, int c, double v) { m[c][r] = v; }
static inline double trace3(const mat3& m) { return m[0][0] + m[1][1] + m[2][2]; }
static inline mat3 diag3(const vec3& d) { return mat3(d.x, 0, 0, 0, d.y, 0, 0, 0, d.z); }
// outer(a,b) = a b^T : element(row i,col j) = a[i]*b[j]
static inline mat3 outer(const vec3& a, const vec3& b) {
    mat3 m(0.0);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[j][i] = a[i] * b[j];
    return m;
}

// ===========================================================================
// 3x3 SVD (double-precision cyclic Jacobi) — the math twin of the GLSL svd3x3.
// ===========================================================================
static void jacobiRotate(mat3& S, mat3& V, int p, int q)
{
    double spq = E(S, p, q);
    if (std::abs(spq) < 1e-300) return;
    double spp = E(S, p, p), sqq = E(S, q, q);
    double tau = (sqq - spp) / (2.0 * spq);                 // cot(2theta)
    double t = (tau == 0.0) ? 1.0 : (tau > 0 ? 1.0 : -1.0) / (std::abs(tau) + std::sqrt(1.0 + tau * tau));
    double c = 1.0 / std::sqrt(1.0 + t * t), s = t * c;     // Givens cos/sin
    int r = 3 - p - q;
    double spr = E(S, p, r), sqr = E(S, q, r);
    setE(S, p, p, c * c * spp - 2 * s * c * spq + s * s * sqq);  // rotated diagonal pp
    setE(S, q, q, s * s * spp + 2 * s * c * spq + c * c * sqq);  // rotated diagonal qq
    setE(S, p, q, 0.0); setE(S, q, p, 0.0);                      // eliminated off-diagonal
    setE(S, p, r, c * spr - s * sqr); setE(S, r, p, c * spr - s * sqr);
    setE(S, q, r, s * spr + c * sqr); setE(S, r, q, s * spr + c * sqr);
    for (int i = 0; i < 3; ++i) {                                // accumulate V <- V*J
        double vip = E(V, i, p), viq = E(V, i, q);
        setE(V, i, p, c * vip - s * viq);
        setE(V, i, q, s * vip + c * viq);
    }
}

void svd3(const mat3& F, mat3& U, vec3& sigma, mat3& V)
{
    mat3 S = glm::transpose(F) * F;                          // symmetric S = F^T F
    V = mat3(1.0);
    for (int sweep = 0; sweep < 6; ++sweep) {                 // fixed sweeps (double needs a couple more)
        jacobiRotate(S, V, 0, 1); jacobiRotate(S, V, 0, 2); jacobiRotate(S, V, 1, 2);
    }
    sigma = vec3(std::sqrt(std::max(E(S, 0, 0), 0.0)),        // singular values = sqrt(eigenvalues)
                 std::sqrt(std::max(E(S, 1, 1), 0.0)),
                 std::sqrt(std::max(E(S, 2, 2), 0.0)));
    // sort descending, carrying V columns
    auto swapcol = [&](int a, int b) {
        std::swap(sigma[a], sigma[b]);
        for (int i = 0; i < 3; ++i) { double t = E(V, i, a); setE(V, i, a, E(V, i, b)); setE(V, i, b, t); }
    };
    if (sigma.x < sigma.y) swapcol(0, 1);
    if (sigma.y < sigma.z) swapcol(1, 2);
    if (sigma.x < sigma.y) swapcol(0, 1);
    if (glm::determinant(V) < 0.0) { for (int i = 0; i < 3; ++i) setE(V, i, 2, -E(V, i, 2)); }  // keep det(V)>0
    mat3 FV = F * V;                                          // columns are sigma_i * u_i
    const double eps = 1e-14;
    for (int k = 0; k < 3; ++k) {
        vec3 col(E(FV, 0, k), E(FV, 1, k), E(FV, 2, k));
        if (sigma[k] > eps) col /= sigma[k]; else col = vec3(k == 0, k == 1, k == 2);
        for (int i = 0; i < 3; ++i) setE(U, i, k, col[i]);
    }
    if (sigma.z <= eps) {                                    // rebuild degenerate trailing columns
        vec3 u0(E(U, 0, 0), E(U, 1, 0), E(U, 2, 0)), u1(E(U, 0, 1), E(U, 1, 1), E(U, 2, 1));
        vec3 u2 = glm::normalize(glm::cross(u0, u1));
        for (int i = 0; i < 3; ++i) setE(U, i, 2, u2[i]);
    }
    if (glm::determinant(U) < 0.0) {                          // push reflection onto smallest sigma
        for (int i = 0; i < 3; ++i) setE(U, i, 2, -E(U, i, 2));
        sigma.z = -sigma.z;
    }
}

// Reverse of svd3 (derived from dA = U(Omega_U S - S Omega_V + dS)V^T, solving the
// 2x2 coupling per off-diagonal pair). gF = U G V^T with:
//   G_ii = gSigma_i ; G_ij = (aU_ij sig_j + aV_ij sig_i)/(sig_j^2-sig_i^2),
//   G_ji = (aU_ij sig_i + aV_ij sig_j)/(sig_j^2-sig_i^2), aU_ij = (U^T gU)_ij-(U^T gU)_ji.
mat3 svd3Adjoint(const mat3& U, const vec3& sigma, const mat3& V,
                 const mat3& gU, const vec3& gSigma, const mat3& gV)
{
    mat3 UtgU = glm::transpose(U) * gU;                      // U^T (dL/dU)
    mat3 VtgV = glm::transpose(V) * gV;                      // V^T (dL/dV)
    mat3 G(0.0);
    for (int i = 0; i < 3; ++i) setE(G, i, i, gSigma[i]);    // diagonal: dL/dsigma direct
    const double eps = 1e-9;
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j) {
            double D = sigma[j] * sigma[j] - sigma[i] * sigma[i];   // 2x2 coupling determinant
            if (std::abs(D) < eps) continue;                         // repeated sigma: drop coupling
            double aU = E(UtgU, i, j) - E(UtgU, j, i);               // skew part of U^T gU
            double aV = E(VtgV, i, j) - E(VtgV, j, i);               // skew part of V^T gV
            setE(G, i, j, (aU * sigma[j] + aV * sigma[i]) / D);
            setE(G, j, i, (aU * sigma[i] + aV * sigma[j]) / D);
        }
    return U * G * glm::transpose(V);                        // dL/dF = U G V^T
}

// ===========================================================================
// Constitutive stresses (Kirchhoff tau = J*sigma_cauchy) and their adjoints.
// ===========================================================================
// Fixed-corotated Neo-Hookean: tau = 2mu U(Sig^2-Sig)U^T + lambda(J-1)J I, J=det F.
static mat3 elasticTau(const mat3& F, double mu, double lambda)
{
    mat3 U, V; vec3 s; svd3(F, U, s, V);
    double J = s.x * s.y * s.z;                              // det(F) (signed)
    vec3 D(s.x * s.x - s.x, s.y * s.y - s.y, s.z * s.z - s.z);
    return 2.0 * mu * (U * diag3(D) * glm::transpose(U)) + diag3(vec3(lambda * (J - 1.0) * J)); // Kirchhoff
}
static mat3 elasticTauAdjoint(const mat3& F, double mu, double lambda, const mat3& gTau)
{
    mat3 U, V; vec3 s; svd3(F, U, s, V);
    double J = s.x * s.y * s.z;                              // det(F)
    vec3 D(s.x * s.x - s.x, s.y * s.y - s.y, s.z * s.z - s.z);
    mat3 M = glm::transpose(U) * gTau * U;                   // tau1=U D U^T -> M_kk pairs with D_k
    mat3 gU = 2.0 * mu * (gTau + glm::transpose(gTau)) * U * diag3(D); // dL/dU from 2mu U D U^T
    double tr = trace3(gTau);
    vec3 gS(0.0);
    for (int k = 0; k < 3; ++k) {
        gS[k] += 2.0 * mu * M[k][k] * (2.0 * s[k] - 1.0);    // d D_k/d sig_k = 2 sig_k - 1
        double sk = (std::abs(s[k]) < 1e-9) ? 1e-9 : s[k];
        gS[k] += tr * lambda * (2.0 * J - 1.0) * (J / sk);   // d[lambda(J-1)J]/d sig_k = ...*(J/sig_k)
    }
    return svd3Adjoint(U, s, V, gU, gS, mat3(0.0));          // chain to dL/dF
}

// Weakly-compressible fluid: tau = -J*p I, p = K(J^-gamma - 1).  (mu=K, lambda=gamma)
static mat3 fluidTau(double J, double K, double gamma)
{
    double p = K * (std::pow(1.0 / std::max(J, 1e-4), gamma) - 1.0);
    return diag3(vec3(-J * p));
}

// StVK-Hencky principal stress for sand: tau = U diag(tauP) U^T, tauP_k = 2mu ln s_k + lambda*sum(ln s).
static mat3 sandTau(const mat3& F, double mu, double lambda)
{
    mat3 U, V; vec3 s; svd3(F, U, s, V);
    vec3 ln(std::log(std::max(std::abs(s.x), 1e-9)), std::log(std::max(std::abs(s.y), 1e-9)),
            std::log(std::max(std::abs(s.z), 1e-9)));
    double trln = ln.x + ln.y + ln.z;
    vec3 tauP = 2.0 * mu * ln + vec3(lambda * trln);
    return U * diag3(tauP) * glm::transpose(U);
}
static mat3 sandTauAdjoint(const mat3& F, double mu, double lambda, const mat3& gTau)
{
    mat3 U, V; vec3 s; svd3(F, U, s, V);
    vec3 sa(std::max(std::abs(s.x), 1e-9), std::max(std::abs(s.y), 1e-9), std::max(std::abs(s.z), 1e-9));
    vec3 ln(std::log(sa.x), std::log(sa.y), std::log(sa.z));
    double trln = ln.x + ln.y + ln.z;
    vec3 tauP = 2.0 * mu * ln + vec3(lambda * trln);
    mat3 M = glm::transpose(U) * gTau * U;
    mat3 gU = (gTau + glm::transpose(gTau)) * U * diag3(tauP);  // dL/dU from U diag(tauP) U^T
    vec3 gTauP(M[0][0], M[1][1], M[2][2]);                      // dL/dtauP from diagonal
    vec3 gS(0.0);
    double sumGtauP = gTauP.x + gTauP.y + gTauP.z;
    for (int j = 0; j < 3; ++j)
        gS[j] = (gTauP[j] * 2.0 * mu + sumGtauP * lambda) / s[j]; // d tauP_k/d sig_j; d ln/d sig = 1/sig
    return svd3Adjoint(U, s, V, gU, gS, mat3(0.0));
}

// ===========================================================================
// Differentiable MLS-MPM rollout: forward (with tape) + tape-based reverse.
// ===========================================================================
struct Adj { vec3 ax{ 0.0 }, av{ 0.0 }; mat3 aC{ 0.0 }, aF{ 0.0 }; };

class Sim {
public:
    Config cfg;
    std::vector<Particle> p;            // live particle state
    std::vector<std::vector<Particle>> tape; // particle state at the start of each step

    int N3() const { return cfg.N * cfg.N * cfg.N; }
    int idx(int x, int y, int z) const { return (z * cfg.N + y) * cfg.N + x; }

    // --- per-particle quadratic B-spline stencil data (weights + derivatives) ---
    struct Stencil { glm::ivec3 base; vec3 fx; vec3 w[3], dw[3]; };
    Stencil stencil(const vec3& x) const {
        Stencil st;
        vec3 Xp = (x - cfg.origin) / cfg.dx;                 // grid-space position
        st.base = glm::ivec3(glm::floor(Xp - 0.5));
        st.fx = Xp - vec3(st.base);
        st.w[0] = 0.5 * (1.5 - st.fx) * (1.5 - st.fx);       // quadratic B-spline weights
        st.w[1] = 0.75 - (st.fx - 1.0) * (st.fx - 1.0);
        st.w[2] = 0.5 * (st.fx - 0.5) * (st.fx - 0.5);
        st.dw[0] = st.fx - 1.5;                              // d w/d fx
        st.dw[1] = -2.0 * (st.fx - 1.0);
        st.dw[2] = st.fx - 0.5;
        return st;
    }

    mat3 stress(const Particle& pp) const {                  // Kirchhoff tau by material
        if (pp.material == Mat::Fluid) return fluidTau(pp.Jp, pp.mu, pp.lambda);
        if (pp.material == Mat::Sand)  return sandTau(pp.F, pp.mu, pp.lambda);
        return elasticTau(pp.F, pp.mu, pp.lambda);
    }

    // Forward grid (mass, momentum, velocity, BC mask) from the current particles.
    void buildGrid(std::vector<double>& gM, std::vector<vec3>& gMom,
                   std::vector<vec3>& gV, std::vector<glm::ivec3>& mask) const {
        const double Dinv = 4.0 / (cfg.dx * cfg.dx);
        std::fill(gM.begin(), gM.end(), 0.0);
        std::fill(gMom.begin(), gMom.end(), vec3(0.0));
        for (const auto& pp : p) {                            // ---- P2G ----
            Stencil st = stencil(pp.x);
            mat3 affine = pp.mass * pp.C - (cfg.dt * pp.vol * Dinv) * stress(pp); // APIC + internal force
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int c = 0; c < 3; ++c) {
                glm::ivec3 nd = st.base + glm::ivec3(a, b, c);
                if (nd.x < 0 || nd.y < 0 || nd.z < 0 || nd.x >= cfg.N || nd.y >= cfg.N || nd.z >= cfg.N) continue;
                double wt = st.w[a].x * st.w[b].y * st.w[c].z;
                vec3 dpos = (vec3(a, b, c) - st.fx) * cfg.dx;
                int n = idx(nd.x, nd.y, nd.z);
                gM[n] += wt * pp.mass;                        // scatter mass
                gMom[n] += wt * (pp.mass * pp.v + affine * dpos); // scatter momentum
            }
        }
        for (int n = 0; n < N3(); ++n) {                      // ---- grid update ----
            mask[n] = glm::ivec3(0);
            if (gM[n] > 1e-12) {
                vec3 v = gMom[n] / gM[n] + cfg.dt * cfg.gravity; // momentum->velocity + gravity
                int z = n / (cfg.N * cfg.N), rem = n - z * cfg.N * cfg.N, y = rem / cfg.N, x = rem - y * cfg.N;
                if (x < cfg.bound && v.x < 0) { v.x = 0; mask[n].x = 1; }   // separating wall BC
                if (x >= cfg.N - cfg.bound && v.x > 0) { v.x = 0; mask[n].x = 1; }
                if (y < cfg.bound && v.y < 0) { v.y = 0; mask[n].y = 1; }
                if (y >= cfg.N - cfg.bound && v.y > 0) { v.y = 0; mask[n].y = 1; }
                if (z < cfg.bound && v.z < 0) { v.z = 0; mask[n].z = 1; }
                if (z >= cfg.N - cfg.bound && v.z > 0) { v.z = 0; mask[n].z = 1; }
                gV[n] = v;
            } else gV[n] = vec3(0.0);
        }
    }

    void stepForward() {
        const double Dinv = 4.0 / (cfg.dx * cfg.dx);
        std::vector<double> gM(N3()); std::vector<vec3> gMom(N3()), gV(N3()); std::vector<glm::ivec3> mask(N3());
        buildGrid(gM, gMom, gV, mask);
        for (auto& pp : p) {                                  // ---- G2P ----
            Stencil st = stencil(pp.x);
            vec3 vnew(0.0); mat3 Cnew(0.0);
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int c = 0; c < 3; ++c) {
                glm::ivec3 nd = st.base + glm::ivec3(a, b, c);
                if (nd.x < 0 || nd.y < 0 || nd.z < 0 || nd.x >= cfg.N || nd.y >= cfg.N || nd.z >= cfg.N) continue;
                double wt = st.w[a].x * st.w[b].y * st.w[c].z;
                vec3 dpos = (vec3(a, b, c) - st.fx) * cfg.dx;
                vec3 g = gV[idx(nd.x, nd.y, nd.z)];
                vnew += wt * g;                               // gather velocity
                Cnew += (Dinv * wt) * outer(g, dpos);         // APIC affine reconstruction
            }
            pp.x += cfg.dt * vnew;                            // advect
            mat3 Fnew = (mat3(1.0) + cfg.dt * Cnew) * pp.F;   // F <- (I + dt C) F
            if (pp.material == Mat::Fluid) {
                pp.Jp *= (1.0 + cfg.dt * trace3(Cnew));       // fluid tracks J only
            } else if (pp.material == Mat::Sand) {
                pp.F = sandReturnMap(Fnew, pp.alpha, pp.mu, pp.lambda); // DP projection
            } else {
                pp.F = Fnew;                                  // elastic: no plasticity
            }
            pp.v = vnew; pp.C = Cnew;
        }
    }

    // --- Drucker-Prager return map: project log-singular-values onto the cone ---
    static vec3 dpProject(const vec3& s, double alpha, double mu, double lambda, bool& projected) {
        vec3 ln(std::log(std::max(std::abs(s.x), 1e-9)), std::log(std::max(std::abs(s.y), 1e-9)),
                std::log(std::max(std::abs(s.z), 1e-9)));
        double trEps = ln.x + ln.y + ln.z;
        vec3 epsHat = ln - vec3(trEps / 3.0);
        double n = glm::length(epsHat);
        projected = false;
        if (trEps > 0.0) return vec3(1.0);                    // tension -> cone tip
        if (n < 1e-12) return s;                              // purely volumetric -> elastic
        double dg = n + (3.0 * lambda + 2.0 * mu) / (2.0 * mu) * trEps * alpha;
        if (dg <= 0.0) return s;                              // inside cone -> elastic
        projected = true;
        vec3 H = ln - dg * (epsHat / n);                      // return to cone surface
        return vec3(std::exp(H.x), std::exp(H.y), std::exp(H.z));
    }
    static mat3 sandReturnMap(const mat3& F, double alpha, double mu, double lambda) {
        mat3 U, V; vec3 s; svd3(F, U, s, V);
        bool proj; vec3 sp = dpProject(s, alpha, mu, lambda, proj);
        return U * diag3(sp) * glm::transpose(V);
    }

    // Adjoint of the deformation-gradient plasticity (F'' = plastic(F')): aF'' -> aF'.
    mat3 plasticAdjoint(const Particle& pp, const mat3& Fp, const mat3& aFpp) const {
        if (pp.material != Mat::Sand) return aFpp;            // elastic/fluid: identity map
        mat3 U, V; vec3 s; svd3(Fp, U, s, V);
        bool proj; vec3 sp = dpProject(s, pp.alpha, pp.mu, pp.lambda, proj);
        // recon F'' = U diag(sp) V^T : split aF'' into (gU, g sp, gV)
        mat3 gU = aFpp * V * diag3(sp);
        mat3 gV = glm::transpose(aFpp) * U * diag3(sp);
        vec3 gSp((glm::transpose(U) * aFpp * V)[0][0], (glm::transpose(U) * aFpp * V)[1][1],
                 (glm::transpose(U) * aFpp * V)[2][2]);
        vec3 gS(0.0);
        if (!proj) {                                          // identity / tip
            // sp == s (elastic branch) -> gS = gSp ; tip branch -> sp const -> gS = 0
            vec3 ln(std::log(std::abs(s.x)), std::log(std::abs(s.y)), std::log(std::abs(s.z)));
            double trEps = ln.x + ln.y + ln.z;
            if (trEps <= 0.0) gS = gSp;                       // inside-cone elastic: sp = s
        } else {
            // projecting branch: sp = exp(H), H = eps - dg*eh/|eh|, eps = ln(s).
            vec3 ln(std::log(std::abs(s.x)), std::log(std::abs(s.y)), std::log(std::abs(s.z)));
            double t = ln.x + ln.y + ln.z;
            vec3 eh = ln - vec3(t / 3.0);
            double nn = glm::length(eh);
            vec3 nhat = eh / nn;
            double k = (3.0 * pp.lambda + 2.0 * pp.mu) / (2.0 * pp.mu);
            double dg = nn + k * t * pp.alpha;
            vec3 gH = gSp * sp;                               // d sp/d H = exp(H) = sp
            // J_H = dH/d eps = I - n n^T - k*alpha n 1^T - (dg/nn)(I - (1/3)11^T + n n^T)
            mat3 JH(0.0);
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
                double Iij = (i == j) ? 1.0 : 0.0;
                double v = Iij - nhat[i] * nhat[j] - k * pp.alpha * nhat[i]
                         - (dg / nn) * (Iij - (1.0 / 3.0) + nhat[i] * nhat[j]);
                setE(JH, i, j, v);
            }
            vec3 gEps(0.0);                                   // gEps = J_H^T gH
            for (int j = 0; j < 3; ++j) for (int i = 0; i < 3; ++i) gEps[j] += E(JH, i, j) * gH[i];
            for (int i = 0; i < 3; ++i) gS[i] = gEps[i] / s[i]; // d eps/d sig = 1/sig
        }
        return svd3Adjoint(U, s, V, gU, gS, gV);
    }

    // Reverse pass over one step: output adjoints `ao` -> input adjoints `ai`.
    void stepBackward(const std::vector<Adj>& ao, std::vector<Adj>& ai) {
        const double Dinv = 4.0 / (cfg.dx * cfg.dx);
        std::vector<double> gM(N3()); std::vector<vec3> gMom(N3()), gV(N3()); std::vector<glm::ivec3> mask(N3());
        buildGrid(gM, gMom, gV, mask);                        // recompute forward grid from input state
        std::vector<vec3> agV(N3(), vec3(0.0)), agMom(N3(), vec3(0.0));
        std::vector<double> agM(N3(), 0.0);
        ai.assign(p.size(), Adj{});

        for (size_t pi = 0; pi < p.size(); ++pi) {            // ---- adjoint G2P ----
            const Particle& pp = p[pi];
            Stencil st = stencil(pp.x);
            vec3 vnew(0.0); mat3 Cnew(0.0);                   // recompute forward G2P outputs
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int c = 0; c < 3; ++c) {
                glm::ivec3 nd = st.base + glm::ivec3(a, b, c);
                if (nd.x < 0 || nd.y < 0 || nd.z < 0 || nd.x >= cfg.N || nd.y >= cfg.N || nd.z >= cfg.N) continue;
                double wt = st.w[a].x * st.w[b].y * st.w[c].z;
                vec3 dpos = (vec3(a, b, c) - st.fx) * cfg.dx;
                vec3 g = gV[idx(nd.x, nd.y, nd.z)];
                vnew += wt * g; Cnew += (Dinv * wt) * outer(g, dpos);
            }
            mat3 A = mat3(1.0) + cfg.dt * Cnew;               // F' = A F
            mat3 Fp = A * pp.F;
            mat3 aFp = plasticAdjoint(pp, Fp, ao[pi].aF);     // adjoint of plasticity
            mat3 aA = aFp * glm::transpose(pp.F);             // dL/dA = aF' F^T
            ai[pi].aF += glm::transpose(A) * aFp;             // dL/dF_in += A^T aF'
            vec3 AVN = ao[pi].av + cfg.dt * ao[pi].ax;        // grad of vnew (from v' and x'=x+dt v)
            mat3 ACN = ao[pi].aC + cfg.dt * aA;               // grad of Cnew (from C' and F-update)
            ai[pi].ax += ao[pi].ax;                           // x_in passes straight to x'
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int c = 0; c < 3; ++c) {
                glm::ivec3 nd = st.base + glm::ivec3(a, b, c);
                if (nd.x < 0 || nd.y < 0 || nd.z < 0 || nd.x >= cfg.N || nd.y >= cfg.N || nd.z >= cfg.N) continue;
                double wt = st.w[a].x * st.w[b].y * st.w[c].z;
                vec3 dpos = (vec3(a, b, c) - st.fx) * cfg.dx;
                vec3 gwt = (1.0 / cfg.dx) * vec3(st.dw[a].x * st.w[b].y * st.w[c].z, // d wt/d x
                                                 st.w[a].x * st.dw[b].y * st.w[c].z,
                                                 st.w[a].x * st.w[b].y * st.dw[c].z);
                vec3 g = gV[idx(nd.x, nd.y, nd.z)];
                agV[idx(nd.x, nd.y, nd.z)] += wt * AVN + (Dinv * wt) * (ACN * dpos); // grad grid velocity
                ai[pi].ax += glm::dot(AVN, g) * gwt;          // vnew via weight(x)
                ai[pi].ax += Dinv * glm::dot(g, ACN * dpos) * gwt;          // Cnew via weight(x)
                ai[pi].ax += -Dinv * wt * (glm::transpose(ACN) * g);        // Cnew via dpos(x)
            }
        }
        for (int n = 0; n < N3(); ++n) {                      // ---- adjoint grid update ----
            if (gM[n] <= 1e-12) continue;
            vec3 a = agV[n];
            if (mask[n].x) a.x = 0; if (mask[n].y) a.y = 0; if (mask[n].z) a.z = 0; // BC kills masked grads
            agMom[n] = a / gM[n];                             // d v/d momentum = 1/m
            agM[n] = glm::dot(a, -gMom[n] / (gM[n] * gM[n])); // d v/d mass = -mom/m^2
        }
        for (size_t pi = 0; pi < p.size(); ++pi) {            // ---- adjoint P2G ----
            const Particle& pp = p[pi];
            Stencil st = stencil(pp.x);
            mat3 affine = pp.mass * pp.C - (cfg.dt * pp.vol * Dinv) * stress(pp);
            mat3 aAffine(0.0);
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int c = 0; c < 3; ++c) {
                glm::ivec3 nd = st.base + glm::ivec3(a, b, c);
                if (nd.x < 0 || nd.y < 0 || nd.z < 0 || nd.x >= cfg.N || nd.y >= cfg.N || nd.z >= cfg.N) continue;
                double wt = st.w[a].x * st.w[b].y * st.w[c].z;
                vec3 dpos = (vec3(a, b, c) - st.fx) * cfg.dx;
                vec3 gwt = (1.0 / cfg.dx) * vec3(st.dw[a].x * st.w[b].y * st.w[c].z,
                                                 st.w[a].x * st.dw[b].y * st.w[c].z,
                                                 st.w[a].x * st.w[b].y * st.dw[c].z);
                int n = idx(nd.x, nd.y, nd.z);
                vec3 q = pp.mass * pp.v + affine * dpos;       // scattered momentum contribution
                ai[pi].av += wt * pp.mass * agMom[n];          // dL/dv_in
                aAffine += wt * outer(agMom[n], dpos);         // dL/d affine
                ai[pi].ax += agM[n] * pp.mass * gwt;           // mass-scatter via weight(x)
                ai[pi].ax += glm::dot(agMom[n], q) * gwt;      // momentum via weight(x)
                ai[pi].ax += -wt * (glm::transpose(affine) * agMom[n]); // momentum via dpos(x)
            }
            ai[pi].aC += pp.mass * aAffine;                    // affine = mass*C - ...
            mat3 aTau = -(cfg.dt * pp.vol * Dinv) * aAffine;   // dL/d tau
            if (pp.material == Mat::Elastic) ai[pi].aF += elasticTauAdjoint(pp.F, pp.mu, pp.lambda, aTau);
            else if (pp.material == Mat::Sand) ai[pi].aF += sandTauAdjoint(pp.F, pp.mu, pp.lambda, aTau);
            // fluid stress depends on Jp (scalar), handled via Jp chain (omitted: not a control here)
        }
    }
};

// ===========================================================================
// Verification modules
// ===========================================================================
static double relErr(double a, double b) {
    double d = std::abs(a - b), s = std::max(std::abs(a), std::abs(b));
    return s < 1e-12 ? d : d / s;
}

GradCheck checkSvdAdjoint()
{
    GradCheck gc;
    // f(F) = <Xi,U> + sum c_k sigma_k + <Psi,V> for fixed Xi,c,Psi -> df/dF via adjoint.
    mat3 F(1.0); F[0][0] = 1.3; F[1][0] = 0.2; F[0][1] = -0.15; F[2][2] = 0.8; F[1][2] = 0.1; F[2][0] = 0.05;
    mat3 Xi(0.0); Xi[0][0] = 0.7; Xi[1][2] = -0.4; Xi[2][1] = 0.3; Xi[0][2] = 0.2;
    mat3 Psi(0.0); Psi[1][1] = 0.5; Psi[2][0] = -0.25; Psi[0][1] = 0.15;
    vec3 c(0.6, -0.3, 0.9);
    auto f = [&](const mat3& M) {
        mat3 U, V; vec3 s; svd3(M, U, s, V);
        double r = c.x * s.x + c.y * s.y + c.z * s.z;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r += Xi[j][i] * U[j][i] + Psi[j][i] * V[j][i];
        return r;
    };
    mat3 U, V; vec3 s; svd3(F, U, s, V);
    mat3 gF = svd3Adjoint(U, s, V, Xi, c, Psi);
    const double h = 1e-6; gc.maxRelErr = 0; gc.pass = true;
    for (int j = 0; j < 3; ++j) for (int i = 0; i < 3; ++i) {
        mat3 Fp = F, Fm = F; Fp[j][i] += h; Fm[j][i] -= h;
        double num = (f(Fp) - f(Fm)) / (2 * h);
        double ana = E(gF, i, j);
        gc.analytic.push_back(ana); gc.numeric.push_back(num);
        gc.maxRelErr = std::max(gc.maxRelErr, relErr(ana, num));
    }
    gc.pass = gc.maxRelErr < 1e-5;
    return gc;
}

// Build a small deforming elastic block with a fixed shear field + control v0.
static Sim makeElasticBlock(const vec3& v0, Mat material)
{
    Sim sim; sim.cfg = Config{};
    sim.cfg.N = 20; sim.cfg.dx = 0.1; sim.cfg.origin = vec3(-1.0, -0.2, -1.0);
    sim.cfg.dt = 2.0e-3; sim.cfg.steps = 30; sim.cfg.bound = 2;
    const double E = 1.0e4, nu = 0.2, density = 1000.0, sp = 0.06;
    const double mu = E / (2.0 * (1.0 + nu)), lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double sphi = std::sin(35.0 * 3.14159265358979 / 180.0);
    const double alpha = std::sqrt(2.0 / 3.0) * (2.0 * sphi) / (3.0 - sphi);
    vec3 c0(0.0, 0.30, 0.0);
    for (int ix = 0; ix < 4; ++ix) for (int iy = 0; iy < 4; ++iy) for (int iz = 0; iz < 4; ++iz) {
        Particle pp;
        pp.x = c0 + (vec3(ix, iy, iz) - 1.5) * sp;
        pp.v = v0 + vec3(0.8 * (pp.x.y - c0.y), 0.0, 0.0); // control + fixed shear -> deforms F
        pp.mass = density * sp * sp * sp; pp.vol = sp * sp * sp;
        pp.mu = mu; pp.lambda = lambda; pp.alpha = alpha; pp.material = material;
        sim.p.push_back(pp);
    }
    return sim;
}

// Loss = 0.5 sum_p |x_p - x_p^target|^2 (minimize the elastic block's final
// displacement). dL/dx_p = (x_p - x_p^target); seeds the final-state adjoint.
static double dispLoss(Sim& sim, const std::vector<vec3>& xt, std::vector<Adj>* aFinal)
{
    double L = 0.0;
    for (size_t i = 0; i < sim.p.size(); ++i) {
        vec3 d = sim.p[i].x - xt[i];
        L += 0.5 * glm::dot(d, d);
        if (aFinal) (*aFinal)[i].ax = d;                     // dL/dx_p^final
    }
    return L;
}

static double rolloutLoss(const vec3& v0, Mat mat, const std::vector<vec3>& xt)
{
    Sim sim = makeElasticBlock(v0, mat);
    for (int s = 0; s < sim.cfg.steps; ++s) sim.stepForward();
    return dispLoss(sim, xt, nullptr);
}

static GradCheck checkControlGradient(Mat material, double tol)
{
    GradCheck gc;
    vec3 v0(0.3, -2.0, 0.15);                                // control: uniform initial velocity
    Sim sim = makeElasticBlock(v0, material);
    std::vector<vec3> xt(sim.p.size());                      // target = initial positions
    for (size_t i = 0; i < sim.p.size(); ++i) xt[i] = sim.p[i].x;
    sim.tape.clear();
    for (int s = 0; s < sim.cfg.steps; ++s) { sim.tape.push_back(sim.p); sim.stepForward(); }
    std::vector<Adj> a(sim.p.size());                        // final-state adjoints
    dispLoss(sim, xt, &a);
    for (int s = sim.cfg.steps - 1; s >= 0; --s) {           // reverse march
        sim.p = sim.tape[s];
        std::vector<Adj> ai; sim.stepBackward(a, ai); a = ai;
    }
    vec3 dLdv0(0.0); for (auto& ad : a) dLdv0 += ad.av;      // v_p^0 = v0 for all p => sum av
    const double h = 1e-6;
    double scale = 1e-12;                                    // normalize error by gradient magnitude
    for (int k = 0; k < 3; ++k) scale = std::max(scale, std::abs(dLdv0[k]));
    for (int k = 0; k < 3; ++k) {                            // central FD per component
        vec3 vp = v0, vm = v0; vp[k] += h; vm[k] -= h;
        double num = (rolloutLoss(vp, material, xt) - rolloutLoss(vm, material, xt)) / (2 * h);
        gc.analytic.push_back(dLdv0[k]); gc.numeric.push_back(num);
        gc.maxRelErr = std::max(gc.maxRelErr, std::abs(dLdv0[k] - num) / scale); // vector-relative error
    }
    gc.pass = gc.maxRelErr < tol;
    return gc;
}

GradCheck checkElasticGradient() { return checkControlGradient(Mat::Elastic, 1e-5); }
GradCheck checkSandGradient() { return checkControlGradient(Mat::Sand, 1e-4); }

// von Mises of the Cauchy stress sigma = tau/J for a Neo-Hookean F.
static double vonMises(const mat3& F, double mu, double lambda)
{
    mat3 tau = elasticTau(F, mu, lambda);                   // Kirchhoff stress
    double J = glm::determinant(F);
    double inv = 1.0 / std::max(std::abs(J), 1e-9);
    mat3 s = inv * tau;                                     // Cauchy stress
    double s11 = s[0][0], s22 = s[1][1], s33 = s[2][2];
    double s12 = 0.5 * (s[1][0] + s[0][1]), s23 = 0.5 * (s[2][1] + s[1][2]), s31 = 0.5 * (s[0][2] + s[2][0]);
    return std::sqrt(0.5 * ((s11 - s22) * (s11 - s22) + (s22 - s33) * (s22 - s33) + (s33 - s11) * (s33 - s11))
                     + 3.0 * (s12 * s12 + s23 * s23 + s31 * s31)); // von Mises invariant
}

StressEval evaluatePeakStress(double accel, double youngsE, double nu,
                              double density, double yieldStress)
{
    Sim sim; sim.cfg.N = 24; sim.cfg.dx = 0.08; sim.cfg.origin = vec3(-1.0, -0.16, -1.0);
    sim.cfg.dt = 1.0e-3; sim.cfg.steps = 60; sim.cfg.bound = 2;
    // The trajectory segment's peak acceleration loads the sample as an
    // amplified body force (inertial load), with a lateral component so the
    // block shears against the floor constraint rather than only compressing.
    sim.cfg.gravity = vec3(0.35 * accel, -(accel + 9.81), 0.0);
    const double mu = youngsE / (2.0 * (1.0 + nu));
    const double lambda = youngsE * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double sp = 0.04;
    vec3 c0(0.0, 0.18, 0.0);                                // block rests just above the floor (y=0)
    for (int ix = 0; ix < 6; ++ix) for (int iy = 0; iy < 6; ++iy) for (int iz = 0; iz < 6; ++iz) {
        Particle p; p.x = c0 + (vec3(ix, iy, iz) - 2.5) * sp;
        p.mass = density * sp * sp * sp; p.vol = sp * sp * sp;
        p.mu = mu; p.lambda = lambda; p.material = Mat::Elastic;
        sim.p.push_back(p);
    }
    double maxVM = 0.0;
    for (int s = 0; s < sim.cfg.steps; ++s) {
        sim.stepForward();
        for (const auto& p : sim.p) maxVM = std::max(maxVM, vonMises(p.F, mu, lambda)); // peak over rollout
    }
    StressEval e; e.maxVonMises = maxVM; e.yield = yieldStress;
    // A non-finite peak means the load drove the sample unstable — also unsafe.
    e.exceeded = !std::isfinite(maxVM) || maxVM > yieldStress; e.steps = sim.cfg.steps;
    return e;
}

bool runSelfTests()
{
    auto log = [](const char* name, const GradCheck& g) {
        std::fprintf(stderr, "[ADJOINT] %-26s %s  maxRelErr=%.3e\n", name, g.pass ? "PASS" : "FAIL", g.maxRelErr);
        for (size_t i = 0; i < g.analytic.size(); ++i)
            std::fprintf(stderr, "[ADJOINT]     [%zu] analytic=% .8e  numeric=% .8e\n",
                         i, g.analytic[i], g.numeric[i]);
        return g.pass;
    };
    bool ok = true;
    std::fprintf(stderr, "[ADJOINT] === ADJOINT_GRADIENT_CHECK ===\n");
    ok &= log("svd3 adjoint", checkSvdAdjoint());
    ok &= log("elastic dL/dv0 (<1e-5)", checkElasticGradient());
    ok &= log("sand dL/dv0 (DP, <1e-4)", checkSandGradient());
    std::fprintf(stderr, "[ADJOINT] overall: %s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok;
}

} // namespace krs::mpmad
