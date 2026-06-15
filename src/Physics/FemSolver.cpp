// ===========================================================================
// Phase 5 — FEM oracle implementation (voxel/hex linear elasticity + heat).
// Trilinear 8-node hexes on a regular grid; one element matrix per (cubic) cell
// reused for all full cells; sparse SPD assembly via Eigen SimplicialLDLT.
// CPU/Eigen only. See ROADMAP §L for the discretisation decision + trade-offs.
// ===========================================================================
#include "FemSolver.hpp"

#include <Eigen/Sparse>
#include <Eigen/Dense>

#include <array>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace krs::fem {
namespace {

using SpMat = Eigen::SparseMatrix<double>;
using Trip  = Eigen::Triplet<double>;
using Vec   = Eigen::VectorXd;

// Local node ordering: a in 0..7, (di,dj,dk) = bit (0,1,2). Local coord sign = 2d-1.
inline void localSigns(int a, double& sx, double& sy, double& sz) {
    sx = (a & 1) ? 1.0 : -1.0;
    sy = (a & 2) ? 1.0 : -1.0;
    sz = (a & 4) ? 1.0 : -1.0;
}
// 8 corner (i,j,k) offsets in the SAME a-order (matches localSigns).
inline void cornerOffset(int a, int& di, int& dj, int& dk) {
    di = (a & 1); dj = (a >> 1) & 1; dk = (a >> 2) & 1;
}

// Shape-function physical gradients (8x3) at natural point (xi,eta,ze) for a
// CUBIC cell of side h: dN/dx = (2/h) dN/dxi. Also returns N (8).
inline void shape(double xi, double eta, double ze, double h,
                  std::array<double, 8>& N, std::array<glm::dvec3, 8>& dN) {
    const double s = 2.0 / h;
    for (int a = 0; a < 8; ++a) {
        double sx, sy, sz; localSigns(a, sx, sy, sz);
        const double ax = 1.0 + sx * xi, ay = 1.0 + sy * eta, az = 1.0 + sz * ze;
        N[a] = 0.125 * ax * ay * az;
        dN[a] = glm::dvec3(0.125 * sx * ay * az, 0.125 * sy * ax * az, 0.125 * sz * ax * ay) * s;
    }
}

const double kG = 0.5773502691896258; // 1/sqrt(3)
const double kGauss[2] = { -kG, kG };

// 6x6 isotropic elasticity (Voigt: exx,eyy,ezz,gxy,gyz,gzx; engineering shear).
Eigen::Matrix<double, 6, 6> elasticityD(double E, double nu) {
    const double lam = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double mu = E / (2.0 * (1.0 + nu));
    Eigen::Matrix<double, 6, 6> D = Eigen::Matrix<double, 6, 6>::Zero();
    D(0, 0) = D(1, 1) = D(2, 2) = lam + 2.0 * mu;
    D(0, 1) = D(0, 2) = D(1, 0) = D(1, 2) = D(2, 0) = D(2, 1) = lam;
    D(3, 3) = D(4, 4) = D(5, 5) = mu;
    return D;
}

// B (6x24) strain-displacement at a natural point.
Eigen::Matrix<double, 6, 24> strainB(double xi, double eta, double ze, double h) {
    std::array<double, 8> N; std::array<glm::dvec3, 8> dN;
    shape(xi, eta, ze, h, N, dN);
    Eigen::Matrix<double, 6, 24> B = Eigen::Matrix<double, 6, 24>::Zero();
    for (int a = 0; a < 8; ++a) {
        const double Nx = dN[a].x, Ny = dN[a].y, Nz = dN[a].z;
        const int c = 3 * a;
        B(0, c + 0) = Nx;  B(1, c + 1) = Ny;  B(2, c + 2) = Nz;
        B(3, c + 0) = Ny;  B(3, c + 1) = Nx;
        B(4, c + 1) = Nz;  B(4, c + 2) = Ny;
        B(5, c + 0) = Nz;  B(5, c + 2) = Nx;
    }
    return B;
}

// 24x24 element stiffness for a cubic hex (2x2x2 Gauss).
Eigen::Matrix<double, 24, 24> hexElastic(double E, double nu, double h) {
    const auto D = elasticityD(E, nu);
    const double detJ = (h * 0.5) * (h * 0.5) * (h * 0.5);
    Eigen::Matrix<double, 24, 24> Ke = Eigen::Matrix<double, 24, 24>::Zero();
    for (int gx = 0; gx < 2; ++gx) for (int gy = 0; gy < 2; ++gy) for (int gz = 0; gz < 2; ++gz) {
        const auto B = strainB(kGauss[gx], kGauss[gy], kGauss[gz], h);
        Ke += (B.transpose() * D * B) * detJ; // weight = 1
    }
    return Ke;
}

// 8x8 conductivity (k grad.grad) and 8x8 consistent capacitance (rho cp N.N).
void hexThermal(double k, double rho, double cp, double h,
                Eigen::Matrix<double, 8, 8>& Kt, Eigen::Matrix<double, 8, 8>& Ct) {
    const double detJ = (h * 0.5) * (h * 0.5) * (h * 0.5);
    Kt.setZero(); Ct.setZero();
    for (int gx = 0; gx < 2; ++gx) for (int gy = 0; gy < 2; ++gy) for (int gz = 0; gz < 2; ++gz) {
        std::array<double, 8> N; std::array<glm::dvec3, 8> dN;
        shape(kGauss[gx], kGauss[gy], kGauss[gz], h, N, dN);
        for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b) {
            Kt(a, b) += k * glm::dot(dN[a], dN[b]) * detJ;
            Ct(a, b) += rho * cp * N[a] * N[b] * detJ;
        }
    }
}

double maxDiag(const SpMat& A) {
    double m = 0.0;
    for (int i = 0; i < A.rows(); ++i) m = std::max(m, std::abs(A.coeff(i, i)));
    return m > 0.0 ? m : 1.0;
}

} // namespace

int VoxelFemModel::nearestNode(const glm::dvec3& p) const {
    int best = -1; double bd = 1e300;
    for (int n = 0; n < numNodes; ++n) {
        const double d = glm::dot(nodePos[n] - p, nodePos[n] - p);
        if (d < bd) { bd = d; best = n; }
    }
    return best;
}

VoxelFemModel FemSolver::voxelize(const glm::dvec3& origin, double h, int nx, int ny, int nz,
                                  const std::function<bool(const glm::dvec3&)>& inside) {
    VoxelFemModel m;
    m.origin = origin; m.h = h; m.nx = nx; m.ny = ny; m.nz = nz;
    m.nodeId.assign(size_t(nx + 1) * (ny + 1) * (nz + 1), -1);
    auto activate = [&](int i, int j, int k) {
        int& id = m.nodeId[m.gridIdx(i, j, k)];
        if (id < 0) { id = m.numNodes++; m.nodePos.push_back(origin + glm::dvec3(i, j, k) * h); }
        return id;
    };
    for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
        const glm::dvec3 c = origin + (glm::dvec3(i, j, k) + 0.5) * h;
        if (!inside(c)) continue;
        std::array<int, 8> e{};
        for (int a = 0; a < 8; ++a) { int di, dj, dk; cornerOffset(a, di, dj, dk); e[a] = activate(i + di, j + dj, k + dk); }
        m.elements.push_back(e);
    }
    return m;
}

VoxelFemModel FemSolver::voxelizeBox(const glm::dvec3& center, const glm::dvec3& half, double h) {
    const glm::dvec3 origin = center - half;
    const int nx = std::max(1, int(std::lround(2.0 * half.x / h)));
    const int ny = std::max(1, int(std::lround(2.0 * half.y / h)));
    const int nz = std::max(1, int(std::lround(2.0 * half.z / h)));
    return voxelize(origin, h, nx, ny, nz, [](const glm::dvec3&) { return true; });
}

VoxelFemModel FemSolver::voxelizeMesh(const std::vector<glm::vec3>& verts,
                                      const std::vector<unsigned int>& indices, double h) {
    // AABB.
    glm::dvec3 lo(1e300), hi(-1e300);
    for (const auto& v : verts) { lo = glm::min(lo, glm::dvec3(v)); hi = glm::max(hi, glm::dvec3(v)); }
    const glm::dvec3 pad(h);
    lo -= pad; hi += pad;
    const int nx = std::max(1, int(std::ceil((hi.x - lo.x) / h)));
    const int ny = std::max(1, int(std::ceil((hi.y - lo.y) / h)));
    const int nz = std::max(1, int(std::ceil((hi.z - lo.z) / h)));
    // Inside test: +x ray vs triangles (parity). Watertight-solid assumption.
    auto inside = [&](const glm::dvec3& p) -> bool {
        int crossings = 0;
        const glm::dvec3 dir(1.0, 0.0, 0.0);
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const glm::dvec3 a = glm::dvec3(verts[indices[t]]);
            const glm::dvec3 b = glm::dvec3(verts[indices[t + 1]]);
            const glm::dvec3 c = glm::dvec3(verts[indices[t + 2]]);
            // Moller-Trumbore.
            const glm::dvec3 e1 = b - a, e2 = c - a;
            const glm::dvec3 pv = glm::cross(dir, e2);
            const double det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-12) continue;
            const double inv = 1.0 / det;
            const glm::dvec3 tv = p - a;
            const double u = glm::dot(tv, pv) * inv; if (u < 0.0 || u > 1.0) continue;
            const glm::dvec3 qv = glm::cross(tv, e1);
            const double v = glm::dot(dir, qv) * inv; if (v < 0.0 || u + v > 1.0) continue;
            const double tt = glm::dot(e2, qv) * inv;
            if (tt > 1e-9) ++crossings;
        }
        return (crossings & 1) != 0;
    };
    return voxelize(lo, h, nx, ny, nz, inside);
}

ElasticResult FemSolver::solveElastic(const VoxelFemModel& m, const FemMaterial& mat, const ElasticBC& bc) {
    ElasticResult r;
    if (!m.valid()) return r;
    // Well-posedness: a body with NO fixed nodes but under load (gravity / applied
    // force) has rigid-body modes -> no static equilibrium. The penalty cannot fix
    // an unconstrained system; bail (ok=false) instead of returning garbage.
    const bool loaded = glm::length(bc.gravity) > 1e-12 || !bc.nodalForces.empty();
    if (bc.fixedNodes.empty() && loaded) {
        std::printf("[FEM] solveElastic: no fixed nodes under load -> unrestrained; skipped.\n");
        return r;
    }
    const int nDof = 3 * m.numNodes;
    const auto Ke = hexElastic(mat.E, mat.nu, m.h);
    std::vector<Trip> trips; trips.reserve(m.elements.size() * 24 * 24);
    Vec f = Vec::Zero(nDof);
    const double cellW = mat.rho * (m.h * m.h * m.h) / 8.0; // body-force lump per node
    for (const auto& e : m.elements) {
        for (int a = 0; a < 8; ++a) {
            for (int c = 0; c < 3; ++c) f[3 * e[a] + c] += cellW * bc.gravity[c];
            for (int b = 0; b < 8; ++b)
                for (int ci = 0; ci < 3; ++ci) for (int cj = 0; cj < 3; ++cj)
                    trips.emplace_back(3 * e[a] + ci, 3 * e[b] + cj, Ke(3 * a + ci, 3 * b + cj));
        }
    }
    SpMat K(nDof, nDof); K.setFromTriplets(trips.begin(), trips.end());
    for (const auto& nf : bc.nodalForces) for (int c = 0; c < 3; ++c) f[3 * nf.first + c] += nf.second[c];
    const double P = 1.0e9 * maxDiag(K);
    for (int n : bc.fixedNodes) for (int c = 0; c < 3; ++c) K.coeffRef(3 * n + c, 3 * n + c) += P; // u=0 penalty
    Eigen::SimplicialLDLT<SpMat> solver; solver.compute(K);
    if (solver.info() != Eigen::Success) return r;
    const Vec u = solver.solve(f);
    if (solver.info() != Eigen::Success) return r;

    // Net constraint reaction = sum over fixed DOFs of the penalty spring force P*u. At static
    // equilibrium this balances the applied load (nodalForces + mass*gravity), per Newton.
    glm::dvec3 react(0.0);
    for (int n : bc.fixedNodes) for (int c = 0; c < 3; ++c) react[c] += P * u[3 * n + c];
    r.netReaction = react;

    r.displacement.resize(m.numNodes);
    for (int n = 0; n < m.numNodes; ++n) r.displacement[n] = glm::dvec3(u[3 * n], u[3 * n + 1], u[3 * n + 2]);
    // Per-element CENTROID stress -> averaged onto nodes. For trilinear hexes this
    // is the representative element value and is smoothed by nodal averaging --
    // adequate for the VISUALIZATION field. (Accuracy upgrade: sample the 8 Gauss
    // points and extrapolate to nodes; ~5-10% better, ROADMAP §L.)
    const auto D = elasticityD(mat.E, mat.nu);
    const auto B0 = strainB(0.0, 0.0, 0.0, m.h);
    std::vector<double> vmAcc(m.numNodes, 0.0), enAcc(m.numNodes, 0.0); std::vector<int> cnt(m.numNodes, 0);
    for (const auto& e : m.elements) {
        Eigen::Matrix<double, 24, 1> ue;
        for (int a = 0; a < 8; ++a) for (int c = 0; c < 3; ++c) ue[3 * a + c] = u[3 * e[a] + c];
        const Eigen::Matrix<double, 6, 1> eps = B0 * ue;
        const Eigen::Matrix<double, 6, 1> s = D * eps;
        const double vm = std::sqrt(std::max(0.0,
            0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0]))
            + 3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5])));
        const double en = std::sqrt(eps[0] * eps[0] + eps[1] * eps[1] + eps[2] * eps[2]
            + 0.5 * (eps[3] * eps[3] + eps[4] * eps[4] + eps[5] * eps[5]));
        for (int a = 0; a < 8; ++a) { vmAcc[e[a]] += vm; enAcc[e[a]] += en; ++cnt[e[a]]; }
    }
    r.vonMises.resize(m.numNodes); r.strainNorm.resize(m.numNodes);
    for (int n = 0; n < m.numNodes; ++n) {
        const int c = std::max(1, cnt[n]);
        r.vonMises[n] = vmAcc[n] / c; r.strainNorm[n] = enAcc[n] / c;
        r.maxVonMises = std::max(r.maxVonMises, r.vonMises[n]);
        r.maxStrain = std::max(r.maxStrain, r.strainNorm[n]);
        r.maxDisp = std::max(r.maxDisp, glm::length(r.displacement[n]));
    }
    r.ok = true;
    return r;
}

static ThermalResult solveThermalImpl(const VoxelFemModel& m, const FemMaterial& mat, const ThermalBC& bc,
                                       const std::vector<double>* Tprev, double dt) {
    ThermalResult r;
    if (!m.valid()) return r;
    const int n = m.numNodes;
    Eigen::Matrix<double, 8, 8> Kt, Ct; hexThermal(mat.k, mat.rho, mat.cp, m.h, Kt, Ct);
    const bool transient = (Tprev != nullptr) && dt > 0.0;
    // Well-posedness: a STEADY conduction solve with neither a Dirichlet pin nor a
    // convective sink is rank-deficient (constant null space) -> SimplicialLDLT may
    // "succeed" with an arbitrary constant. Require a temperature reference; bail
    // otherwise. (Transient is always well-posed via the Ct/dt mass term.)
    if (!transient && bc.dirichlet.empty() && (bc.surfaceNodes.empty() || bc.convection <= 0.0)) {
        std::printf("[FEM] solveThermalSteady: no Dirichlet pin or convective sink -> singular; skipped.\n");
        return r;
    }
    std::vector<Trip> trips; trips.reserve(m.elements.size() * 64);
    Vec f = Vec::Zero(n);
    for (const auto& e : m.elements) {
        for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b) {
            double v = Kt(a, b);
            if (transient) v += Ct(a, b) / dt;
            trips.emplace_back(e[a], e[b], v);
        }
        if (transient) { // RHS: (Ct/dt) T_prev
            for (int a = 0; a < 8; ++a) { double acc = 0.0; for (int b = 0; b < 8; ++b) acc += Ct(a, b) / dt * (*Tprev)[e[b]]; f[e[a]] += acc; }
        }
    }
    SpMat K(n, n); K.setFromTriplets(trips.begin(), trips.end());
    for (const auto& src : bc.nodalSource) f[src.first] += src.second;   // W lumped
    for (int sn : bc.surfaceNodes) { K.coeffRef(sn, sn) += bc.convection; f[sn] += bc.convection * bc.ambientT; }
    const double P = 1.0e9 * maxDiag(K);
    for (const auto& d : bc.dirichlet) { K.coeffRef(d.first, d.first) += P; f[d.first] += P * d.second; }
    Eigen::SimplicialLDLT<SpMat> solver; solver.compute(K);
    if (solver.info() != Eigen::Success) return r;
    const Vec T = solver.solve(f);
    if (solver.info() != Eigen::Success) return r;
    r.temperature.resize(n); r.minT = 1e300; r.maxT = -1e300;
    for (int i = 0; i < n; ++i) { r.temperature[i] = T[i]; r.minT = std::min(r.minT, T[i]); r.maxT = std::max(r.maxT, T[i]); }
    r.ok = true;
    return r;
}

ThermalResult FemSolver::solveThermalSteady(const VoxelFemModel& m, const FemMaterial& mat, const ThermalBC& bc) {
    return solveThermalImpl(m, mat, bc, nullptr, 0.0);
}
ThermalResult FemSolver::stepThermalTransient(const VoxelFemModel& m, const FemMaterial& mat,
                                              const ThermalBC& bc, const std::vector<double>& Tprev, double dt) {
    return solveThermalImpl(m, mat, bc, &Tprev, dt);
}

std::future<ElasticResult> FemSolver::solveElasticAsync(VoxelFemModel m, FemMaterial mat, ElasticBC bc) {
    return std::async(std::launch::async, [m = std::move(m), mat, bc = std::move(bc)]() { return solveElastic(m, mat, bc); });
}
std::future<ThermalResult> FemSolver::solveThermalSteadyAsync(VoxelFemModel m, FemMaterial mat, ThermalBC bc) {
    return std::async(std::launch::async, [m = std::move(m), mat, bc = std::move(bc)]() { return solveThermalSteady(m, mat, bc); });
}

// ---------------------------------------------------------------------------
// KRS_FEM_SELFTEST — analytic verifications.
// ---------------------------------------------------------------------------
bool FemSolver::runSelfTests() {
    using std::printf;
    bool all = true;
    auto check = [&](const char* name, bool cond, const std::string& detail) {
        printf("[FEM selftest] %-5s %s  (%s)\n", cond ? "PASS" : "FAIL", name, detail.c_str());
        all = all && cond;
    };
    auto fmt = [](const char* k, double v) { char b[64]; std::snprintf(b, 64, "%s=%.6g", k, v); return std::string(b); };

    // Nodes on the x = origin.x face (clamp) and x = far face (load).
    auto faceNodes = [](const VoxelFemModel& m, int axis, bool maxSide) {
        std::vector<int> out;
        const double target = maxSide ? (m.origin[axis] + (axis == 0 ? m.nx : axis == 1 ? m.ny : m.nz) * m.h)
                                      : m.origin[axis];
        for (int n = 0; n < m.numNodes; ++n) if (std::abs(m.nodePos[n][axis] - target) < 1e-6) out.push_back(n);
        return out;
    };

    const FemMaterial steel{ 68.9e9, 0.33, 2700.0, 167.0, 896.0 };

    // --- Test 1: axial bar (uniaxial tension) vs delta = F L / (A E). nu=0 -> exact. ---
    {
        FemMaterial m0 = steel; m0.nu = 0.0;
        const double L = 1.0, a = 0.1, h = 0.05;
        VoxelFemModel mdl = voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, a / 2, a / 2), h);
        ElasticBC bc; bc.fixedNodes = faceNodes(mdl, 0, false);
        auto tip = faceNodes(mdl, 0, true);
        const double F = 1.0e6;
        for (int n : tip) bc.nodalForces.push_back({ n, glm::dvec3(F / tip.size(), 0, 0) });
        ElasticResult r = solveElastic(mdl, m0, bc);
        // Average tip displacement (total force is conserved, so the mean equals
        // F L/(A E) exactly; individual nodes vary by Saint-Venant near the load).
        double ux = 0.0; for (int n : tip) ux += r.displacement[n].x; ux /= double(tip.size());
        const double analytic = F * L / (a * a * m0.E);
        const double rel = std::abs(ux - analytic) / analytic;
        check("axial bar deflection vs F L/(A E)", r.ok && rel < 0.03,
              fmt("fem", ux) + " " + fmt("analytic", analytic) + " " + fmt("rel", rel));
    }
    // --- Test 2: cantilever tip deflection vs Euler-Bernoulli F L^3/(3 E I). ---
    {
        const double L = 1.0, b = 0.1, hC = 0.1, h = 0.1 / 6.0; // ~6 elements through thickness
        VoxelFemModel mdl = voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, hC / 2, b / 2), h);
        ElasticBC bc; bc.fixedNodes = faceNodes(mdl, 0, false);
        auto tip = faceNodes(mdl, 0, true);
        const double F = -1000.0;
        for (int n : tip) bc.nodalForces.push_back({ n, glm::dvec3(0, F / tip.size(), 0) });
        ElasticResult r = solveElastic(mdl, steel, bc);
        double uy = 0.0; for (int n : tip) uy = std::min(uy, r.displacement[n].y);
        const double I = b * hC * hC * hC / 12.0;
        const double analytic = std::abs(F) * L * L * L / (3.0 * steel.E * I);
        const double ratio = std::abs(uy) / analytic;
        // Trilinear voxel hexes shear-lock (under-predict bending); ratio in [0.7,1.15]
        // is the expected band at this resolution (ROADMAP §L; incompatible modes = upgrade).
        check("cantilever tip vs Euler-Bernoulli", r.ok && ratio > 0.7 && ratio < 1.15,
              fmt("fem", std::abs(uy)) + " " + fmt("EB", analytic) + " " + fmt("ratio", ratio));
    }
    // --- Test 3: 1D bar steady conduction vs linear gradient (exact for trilinear). ---
    {
        const double L = 1.0, a = 0.1, h = 0.05;
        VoxelFemModel mdl = voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, a / 2, a / 2), h);
        ThermalBC bc;
        for (int n : faceNodes(mdl, 0, false)) bc.dirichlet.push_back({ n, 100.0 });
        for (int n : faceNodes(mdl, 0, true))  bc.dirichlet.push_back({ n, 0.0 });
        ThermalResult r = solveThermalSteady(mdl, steel, bc);
        int mid = mdl.nearestNode(glm::dvec3(L / 2, 0, 0));
        const double Tmid = r.ok ? r.temperature[mid] : -1e9;
        check("1D bar conduction (mid = 50 C)", r.ok && std::abs(Tmid - 50.0) < 1.5, fmt("Tmid", Tmid));
    }
    // --- Test 4: plate with a hole, uniaxial tension -> stress concentration (Kt). ---
    {
        const double Lx = 0.4, Ly = 0.2, Lz = 0.05, h = 0.01, holeR = 0.04;
        const glm::dvec3 origin(0, 0, 0);
        const int nx = int(std::lround(Lx / h)), ny = int(std::lround(Ly / h)), nz = int(std::lround(Lz / h));
        const glm::dvec2 hole(Lx / 2, Ly / 2);
        VoxelFemModel mdl = voxelize(origin, h, nx, ny, nz, [&](const glm::dvec3& c) {
            return glm::length(glm::dvec2(c.x, c.y) - hole) > holeR; });
        ElasticBC bc; bc.fixedNodes = faceNodes(mdl, 0, false);
        auto tip = faceNodes(mdl, 0, true);
        const double F = 1.0e6;
        for (int n : tip) bc.nodalForces.push_back({ n, glm::dvec3(F / tip.size(), 0, 0) });
        ElasticResult r = solveElastic(mdl, steel, bc);
        const double sigNom = F / (Ly * Lz); // gross nominal stress
        const double Kt = r.ok ? r.maxVonMises / sigNom : 0.0;
        check("plate-with-hole stress concentration (Kt>2)", r.ok && Kt > 2.0,
              fmt("maxVM", r.maxVonMises) + " " + fmt("sigNom", sigNom) + " " + fmt("Kt", Kt));
    }

    printf("[FEM selftest] overall: %s\n", all ? "ALL PASS" : "FAILURES PRESENT");
    std::fflush(stdout);
    return all;
}

// Phase 1 GATE 1.5 -- FEM static equilibrium: the net constraint REACTION balances the applied LOAD
// (Newton's 1st law for the whole body). A clamped bar under an axial force + gravity is solved; the
// reaction (sum of penalty forces at the fixed nodes, ElasticResult::netReaction) must equal the
// applied load (nodal forces + mass*gravity) in magnitude and line of action. NEG-CTRL A: an
// UNBALANCED system (load but no fixed nodes) has no static equilibrium and must be REJECTED
// (ok=false). NEG-CTRL B: a corrupted applied-load (x0.5) must violate the balance (non-vacuous).
bool FemSolver::runEquilibriumGate1_5()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fem-equil] GATE 1.5 -- FEM static equilibrium: net reaction balances applied load + 2 neg-ctrls\n");

    auto faceNodes = [](const VoxelFemModel& m, int axis, bool maxSide) {
        std::vector<int> out;
        const double target = maxSide ? (m.origin[axis] + (axis == 0 ? m.nx : axis == 1 ? m.ny : m.nz) * m.h)
                                      : m.origin[axis];
        for (int n = 0; n < m.numNodes; ++n)
            if (std::abs(m.nodePos[n][axis] - target) < 1e-6) out.push_back(n);
        return out;
    };

    const FemMaterial steel{ 68.9e9, 0.33, 2700.0, 167.0, 896.0 };
    const double L = 0.6, a = 0.2, h = 0.05;
    const VoxelFemModel mdl = voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, a / 2, a / 2), h);
    const double totalMass = double(mdl.elements.size()) * steel.rho * (h * h * h);
    const glm::dvec3 gravity(0.0, -9.81, 0.0);
    const double F = 1.0e5;                        // axial force at the +x face
    const auto tip = faceNodes(mdl, 0, true);

    ElasticBC bc;
    bc.fixedNodes = faceNodes(mdl, 0, false);      // clamp the -x face
    bc.gravity = gravity;
    for (int n : tip) bc.nodalForces.push_back({ n, glm::dvec3(F / double(tip.size()), 0, 0) });
    const glm::dvec3 appliedLoad = glm::dvec3(F, 0, 0) + totalMass * gravity;  // nodal force + weight

    const ElasticResult r = solveElastic(mdl, steel, bc);
    // sign-convention robust: reaction balances load -> netReaction == -applied (or +applied per the
    // penalty sign); either way one of |netReaction +/- applied| is ~0.
    const double balErr = std::min(glm::length(r.netReaction + appliedLoad),
                                   glm::length(r.netReaction - appliedLoad));
    const double appMag = std::max(1e-9, glm::length(appliedLoad));
    const double relErr = balErr / appMag;
    const bool equilibrium = r.ok && relErr < 0.01;

    // NEG-CTRL A: load but NO fixed nodes -> unrestrained -> solveElastic must bail (ok=false).
    ElasticBC bcUn; bcUn.gravity = gravity;
    for (int n : tip) bcUn.nodalForces.push_back({ n, glm::dvec3(F / double(tip.size()), 0, 0) });
    const ElasticResult rUn = solveElastic(mdl, steel, bcUn);
    const bool unbalancedRejected = !rUn.ok;

    // NEG-CTRL B: a corrupted applied load (half) must FAIL the balance (proves it isn't vacuous).
    const glm::dvec3 corrupt = 0.5 * appliedLoad;
    const double corruptErr = std::min(glm::length(r.netReaction + corrupt),
                                       glm::length(r.netReaction - corrupt));
    const double corruptRel = corruptErr / appMag;
    const bool corruptCaught = corruptRel > 0.1;

    const bool pass = equilibrium && unbalancedRejected && corruptCaught;
    printf("[fem-equil]   |netReaction|=%.1f N ; |appliedLoad|=%.1f N ; balance residual=%.3f (rel %.3f%%, bound<1%%)  %s\n",
           glm::length(r.netReaction), glm::length(appliedLoad), balErr, relErr * 100.0,
           equilibrium ? "BALANCED" : "VIOLATED");
    printf("[fem-equil]   NEG-CTRL A (load, no fixed nodes): solveElastic ok=%d (must be 0 = unrestrained rejected)  %s\n",
           int(rUn.ok), unbalancedRejected ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[fem-equil]   NEG-CTRL B (corrupted load x0.5): balance residual rel=%.1f%% (>>1%% -> caught)  %s\n",
           corruptRel * 100.0, corruptCaught ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[fem-equil] %s\n", pass ? "ALL PASS (reaction balances load; unbalanced + corrupt caught)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::fem
