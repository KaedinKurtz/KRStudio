// FidelityFem.cpp -- Phase 5 FEM gates. CANTILEVER tip deflection vs Euler-Bernoulli PL^3/(3EI), and
// 1D steady CONDUCTION vs the linear heat-equation profile. CPU/Eigen (no GL, no PhysX). Each gate states
// the analytic ground truth, measures the solver, and reports the fidelity gap honestly:
//   - CANTILEVER: trilinear voxel hexes SHEAR-LOCK -> they under-predict bending (delta/EB < 1). That deficit
//     is a REAL fidelity gap (the finding/upgrade spec: incompatible-modes or higher-order elements). But the
//     bending LAW is validated independently of the deficit: delta scales as L^3 (the Euler-Bernoulli signature,
//     robust because the shear-lock factor cancels in the L->2L ratio) and is linear in load. A wrong bending
//     model would give the wrong exponent (linear=>2x, quadratic=>4x, not 8x).
//   - CONDUCTION: trilinear elements are EXACT for a linear profile, so this should match to <1 C. NEG-CONTROL:
//     adding an internal heat source bows the profile parabolic (mid > 50 C), which the linear check must reject.
#include "FidelityGates.hpp"
#include "FemSolver.hpp"

#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::fidelity {

using krs::fem::FemSolver;
using krs::fem::FemMaterial;
using krs::fem::VoxelFemModel;
using krs::fem::ElasticBC;
using krs::fem::ElasticResult;
using krs::fem::ThermalBC;
using krs::fem::ThermalResult;

namespace {

// nodes whose x-coordinate is within eps of `x` (a clamp/tip face selector).
std::vector<int> nodesAtX(const VoxelFemModel& m, double x) {
    std::vector<int> out;
    for (int n = 0; n < m.numNodes; ++n)
        if (std::fabs(m.nodePos[size_t(n)].x - x) < 1e-6) out.push_back(n);
    return out;
}

// Average tip y-deflection magnitude of a cantilever (clamped x=min face, transverse point load P split over
// the free x=max face). Returns |mean tip uy|; ok=false if the solve failed.
double cantileverTip(double L, double b, double hC, double h, double P, double E, double nu, bool& ok) {
    VoxelFemModel m = FemSolver::voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, hC / 2, b / 2), h);
    double xmin = 1e30, xmax = -1e30;
    for (int n = 0; n < m.numNodes; ++n) { xmin = std::min(xmin, m.nodePos[size_t(n)].x); xmax = std::max(xmax, m.nodePos[size_t(n)].x); }
    ElasticBC bc;
    bc.fixedNodes = nodesAtX(m, xmin);
    const std::vector<int> tip = nodesAtX(m, xmax);
    for (int n : tip) bc.nodalForces.push_back({ n, glm::dvec3(0, P / double(tip.size()), 0) });
    FemMaterial mat; mat.E = E; mat.nu = nu;
    const ElasticResult r = FemSolver::solveElastic(m, mat, bc);
    ok = r.ok && !tip.empty() && !bc.fixedNodes.empty();
    if (!ok) return 0.0;
    double uy = 0.0; for (int n : tip) uy += r.displacement[size_t(n)].y; uy /= double(tip.size());
    return std::fabs(uy);
}

} // namespace

// ---- CANTILEVER : tip deflection vs PL^3/(3EI), plus the L^3 bending law (shear-lock-robust) ----
bool runFidelityCantileverGate() {
    std::printf("\n[fidelity] FEM-CANTILEVER : tip deflection vs Euler-Bernoulli PL^3/(3EI) (I=b*h^3/12)\n");
    // LOCKED experiment constants (a material/geometry, asserted by construction: the analytic uses the SAME E,
    // so softening E cannot move delta/EB -- the ratio is E-invariant; only the discretisation changes it).
    const double E = 68.9e9, nu = 0.33;          // 6061-T6 aluminium
    const double b = 0.10, hC = 0.10, P = -1000.0;
    const double hFine = hC / 6.0, hCoarse = hC / 3.0;
    std::printf("  LOCKED: E=%.3gPa nu=%.2f  beam b=%.2f hC=%.2f  tip load P=%.0fN\n", E, nu, b, hC, P);

    const double I   = b * hC * hC * hC / 12.0;

    bool okF = false, okC = false, ok2L = false, ok2P = false;
    const double dFine   = cantileverTip(1.0, b, hC, hFine,   P,       E, nu, okF);   // L=1, fine
    const double dCoarse = cantileverTip(1.0, b, hC, hCoarse, P,       E, nu, okC);   // L=1, coarse (convergence)
    const double d2L     = cantileverTip(2.0, b, hC, hFine,   P,       E, nu, ok2L);  // L=2 (L^3 law)
    const double d2P     = cantileverTip(1.0, b, hC, hFine, 2.0 * P,   E, nu, ok2P);  // 2P (load linearity)
    if (!(okF && okC && ok2L && ok2P)) { std::printf("[fidelity] FEM-CANTILEVER  FAIL (solve error)\n"); return false; }

    const double ebFine   = std::fabs(P) * 1.0 * 1.0 * 1.0 / (3.0 * E * I);
    const double ratioFine   = dFine   / ebFine;
    const double ratioCoarse = dCoarse / ebFine;
    const double scaleL  = d2L / dFine;     // expect 8 (L^3)
    const double scaleP  = d2P / dFine;     // expect 2 (linear in load)

    // (1) absolute fidelity: delta/EB. Shear-lock makes this < 1; the documented expected band is ~[0.7,1.15].
    FidelityResult rAbs{ "fem", "cantilever delta / EB", ratioFine, 1.0, 0.30 };
    reportFidelity(rAbs);
    // (2) the BENDING LAW (shear-lock-robust): delta ~ L^3 and delta ~ P.
    FidelityResult rL3{ "fem", "cantilever L^3 scaling d(2L)/d(L)", scaleL, 8.0, 0.15 };
    reportFidelity(rL3);
    FidelityResult rP { "fem", "cantilever load linearity d(2P)/d(P)", scaleP, 2.0, 0.05 };
    reportFidelity(rP);

    // (3) convergence: refining h moves delta/EB toward 1 -> proves the deficit is shear-lock discretisation,
    //     not a bug. (the finding/upgrade spec.)
    std::printf("  convergence: delta/EB coarse(h=hC/3)=%.3f -> fine(h=hC/6)=%.3f (toward 1 as h->0)\n",
                ratioCoarse, ratioFine);
    const bool converging = std::fabs(ratioFine - 1.0) <= std::fabs(ratioCoarse - 1.0) + 1e-6;
    std::printf("  FINDING: voxel hexes shear-lock -> tip deflection under-predicts by %.0f%% at this resolution;\n",
                (1.0 - ratioFine) * 100.0);
    std::printf("           the L^3 + load-linear bending LAW is FAITHFUL. UPGRADE SPEC: incompatible-modes /\n");
    std::printf("           higher-order elements to remove the shear-lock deficit (a linear bending model would\n");
    std::printf("           give d(2L)/d(L)=2, quadratic=4, not the measured %.2f ~ 8).\n", scaleL);

    // PASS: the bending LAW holds (L^3 + linear-in-load) AND the absolute deflection sits in the documented
    // shear-lock band AND refinement converges toward EB. The shear-lock deficit is the reported finding.
    const bool pass = rL3.pass() && rP.pass() && rAbs.pass() && converging;
    std::printf("  L^3-law=%d load-linear=%d in-shearlock-band=%d converging=%d\n",
                rL3.pass(), rP.pass(), rAbs.pass(), converging);
    std::printf("[fidelity] FEM-CANTILEVER  %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

// ---- THERMAL : 1D steady conduction vs the linear profile T(x)=T0+(T1-T0)x/L ----
bool runFidelityThermalGate() {
    std::printf("\n[fidelity] THERMAL-ANALYTIC : 1D steady conduction vs linear profile T0+(T1-T0)x/L\n");
    const double L = 1.0, a = 0.10, h = 0.05;     // bar geometry (locked)
    const double T0 = 100.0, T1 = 0.0;            // end temperatures (locked, asserted)
    std::printf("  LOCKED: L=%.2f a=%.2f  T(0)=%.0fC  T(L)=%.0fC\n", L, a, T0, T1);

    VoxelFemModel m = FemSolver::voxelizeBox(glm::dvec3(L / 2, 0, 0), glm::dvec3(L / 2, a / 2, a / 2), h);
    double xmin = 1e30, xmax = -1e30;
    for (int n = 0; n < m.numNodes; ++n) { xmin = std::min(xmin, m.nodePos[size_t(n)].x); xmax = std::max(xmax, m.nodePos[size_t(n)].x); }
    const double span = xmax - xmin;

    ThermalBC bc;
    for (int n : nodesAtX(m, xmin)) bc.dirichlet.push_back({ n, T0 });
    for (int n : nodesAtX(m, xmax)) bc.dirichlet.push_back({ n, T1 });
    FemMaterial mat;                               // conductivity k = 167 W/mK (uniform -> profile is k-invariant)

    const ThermalResult r = FemSolver::solveThermalSteady(m, mat, bc);
    if (!r.ok) { std::printf("[fidelity] THERMAL-ANALYTIC  FAIL (solve error)\n"); return false; }

    // full-profile max deviation from the analytic line over INTERIOR nodes (exclude the two fixed faces).
    auto lineT = [&](double x) { return T0 + (T1 - T0) * (x - xmin) / span; };
    double maxDev = 0.0;
    for (int n = 0; n < m.numNodes; ++n) {
        const double x = m.nodePos[size_t(n)].x;
        if (x <= xmin + 1e-6 || x >= xmax - 1e-6) continue;
        maxDev = std::max(maxDev, std::fabs(r.temperature[size_t(n)] - lineT(x)));
    }
    const int midN = m.nearestNode(glm::dvec3((xmin + xmax) / 2, 0, 0));
    const double Tmid = r.temperature[size_t(midN)];

    FidelityResult rMid{ "fem", "conduction midpoint T", Tmid, 50.0, 0.03 };   // 50 C +-1.5
    reportFidelity(rMid);
    std::printf("  full-profile max deviation from linear = %.3f C (trilinear is EXACT for a linear profile)\n", maxDev);

    // NEG-CONTROL with TEETH: an internal heat source at mid-span bows the steady profile PARABOLIC (mid > 50),
    // so the same linear check must now FAIL -- proving the gate detects the source term, not just any profile.
    ThermalBC bcSrc = bc;
    double Qtotal = 0.0;
    for (int n = 0; n < m.numNodes; ++n)
        if (std::fabs(m.nodePos[size_t(n)].x - (xmin + xmax) / 2) < h) { bcSrc.nodalSource.push_back({ n, 50.0 }); Qtotal += 50.0; }
    const ThermalResult rs = FemSolver::solveThermalSteady(m, mat, bcSrc);
    double srcMaxDev = 0.0; double TmidSrc = rs.ok ? rs.temperature[size_t(midN)] : -1e9;
    if (rs.ok) for (int n = 0; n < m.numNodes; ++n) {
        const double x = m.nodePos[size_t(n)].x;
        if (x <= xmin + 1e-6 || x >= xmax - 1e-6) continue;
        srcMaxDev = std::max(srcMaxDev, std::fabs(rs.temperature[size_t(n)] - lineT(x)));
    }
    std::printf("  NEG-CTRL (internal source %.0fW at mid): T_mid=%.1fC (was 50), profile max-dev=%.1fC -> %s\n",
                Qtotal, TmidSrc, srcMaxDev, (TmidSrc > 52.0 && srcMaxDev > 5.0) ? "bows PARABOLIC (teeth OK)" : "no bow?!");

    const bool linearOk = rMid.pass() && maxDev < 1.0;
    const bool teeth    = rs.ok && TmidSrc > 52.0 && srcMaxDev > 5.0;
    const bool pass = linearOk && teeth;
    std::printf("  linear-profile(mid=50,maxdev<1C)=%d  source-bows-profile(teeth)=%d\n", linearOk, teeth);
    std::printf("[fidelity] THERMAL-ANALYTIC  %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::fidelity
