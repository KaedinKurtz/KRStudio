// SensorGate5.cpp -- Phase 5 GATE COMPOSE. Proves the three layers compose correctly:
//   (1) TRUE-vs-BELIEF differ CORRECTLY: physics (L1 true) and the camera (L2 belief + L3 live noise) differ,
//       yet the belief stays within k*sigma of the truth (honest), and the live camera diverges MORE than the
//       belief alone (L3 adds on top of the recon error);
//   (2) SHARED correlation in the composed scene: corr(L2 sigma, L3 dropout rate) > 0.9 -- the same cells are
//       uncertain in belief AND dropping live, because both read the one shared material;
//   (3) TOGGLES: L1-only -> camera == L1 clean; L2 on shifts the camera base from truth to belief;
//   (4) DETERMINISM: same seed -> identical stream; seed+1 -> different draws, same statistics; the per-layer
//       seed is toggle-stable.
// NEG-CTRL (independent-draw): L3 dropout from a fixed-rate field INDEPENDENT of the material -> the
// correlation collapses to ~0. Coincident independent draws do NOT correlate; a shared cause does.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include "Layer2Gpis.hpp"
#include "Composition.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::sensor {

static ReconScene buildComposeScene(int W, int H, int K) {
    ReconScene s; s.W = W; s.H = H;
    s.trueDepth.assign(W * H, 0.0);
    s.material.assign(W * H, MaterialSample{ 0.0, 0.85, 1.0 });
    s.framesObserving.assign(W * H, K);
    auto inRect = [](int u, int v, int u0, int u1, int v0, int v1) { return u >= u0 && u < u1 && v >= v0 && v < v1; };
    for (int v = 0; v < H; ++v)
        for (int u = 0; u < W; ++u) {
            const int i = v * W + u;
            // FLAT true depth so L2 sigma is driven purely by the MATERIAL (the shared cause), not by Z-dependent
            // range noise -- which would add scatter uncorrelated with the (material-driven) L3 dropout.
            s.trueDepth[i] = 1.3;
            if (inRect(u, v, 12, 24, 8, 28)) s.material[i] = MaterialSample{ 1.0, 0.85, 1.0 };   // specular
            if (inRect(u, v, 40, 52, 8, 28)) s.material[i] = MaterialSample{ 0.0, 0.10, 1.0 };   // dark
        }
    return s;
}

// empirical per-cell live dropout rate of a composition.
static double cellDropRate(const Composition& c, int i, int T) {
    std::mt19937_64 r(c.seedL3() ^ (std::uint64_t(i) * 2654435761ull + 0x9E3779B9ull));
    int holes = 0;
    for (int t = 0; t < T; ++t) if (c.cameraRead(i, r) == DEPTH_INVALID) ++holes;
    return double(holes) / T;
}

bool runComposeGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE COMPOSE] L1 true / L2 belief / L3 live -- shared correlation + toggles + determinism\n");

    const int W = 64, H = 48, K = 24;
    SensorProfile prof;
    DepthModel model = DepthModel::fromProfile(prof.depth);
    const ReconScene scene = buildComposeScene(W, H, K);

    CompositionConfig cfgAll; cfgAll.seed = prof.seed;       // all three layers
    const Composition comp(scene, model, cfgAll);

    // (1) TRUE-vs-BELIEF differ correctly.
    std::mt19937_64 rcam(comp.seedL3());
    double dCam = 0, dBel = 0; int cov = 0, obs = 0, validCam = 0;
    for (int i = 0; i < W * H; ++i) {
        const double t = comp.physicsDepth(i);
        const double mu = comp.beliefMu(i), sg = comp.beliefSigma(i);
        if (!comp.beliefHole(i)) { dBel += std::fabs(t - mu); ++obs; if (std::fabs(t - mu) < 2.0 * sg) ++cov; }
        const double cam = comp.cameraRead(i, rcam);
        if (cam != DEPTH_INVALID) { dCam += std::fabs(t - cam); ++validCam; }
    }
    dBel /= obs; dCam /= validCam;
    const double coverage = double(cov) / obs;
    const bool t_divergence = dCam > 0.002 && dCam > dBel && coverage >= 0.88;
    std::printf("  divergence: |L1-camera|=%.5f > |L1-belief|=%.5f ; belief coverage=%.3f -> %s\n",
                dCam, dBel, coverage, t_divergence ? "ok (live diverges more; belief honest)" : "FAIL");

    // (2) SHARED correlation in the composed scene: L2 sigma vs L3 dropout rate over cells.
    std::vector<double> l2sig, l3drop;
    for (int i = 0; i < W * H; ++i) {
        if (comp.beliefHole(i)) continue;
        l2sig.push_back(comp.beliefSigma(i));
        l3drop.push_back(cellDropRate(comp, i, 1000));
    }
    const LinFit shFit = linearFit(l2sig, l3drop);
    const bool t_shared = shFit.slope > 0 && shFit.r2 > 0.81;
    std::printf("  shared    : corr(L2 sigma, L3 dropout) slope>0 r2=%.3f (exp >0.81) -> %s\n",
                shFit.r2, t_shared ? "ok (same cells uncertain + dropping)" : "FAIL");

    // NEG-CTRL: independent-draw L3 dropout (fixed rate, not material) -> correlation collapses.
    CompositionConfig cfgInd = cfgAll; cfgInd.independentL3 = true;
    const Composition compInd(scene, model, cfgInd);
    std::vector<double> l3dropInd;
    for (int i = 0; i < W * H; ++i) if (!compInd.beliefHole(i)) l3dropInd.push_back(cellDropRate(compInd, i, 1000));
    const LinFit indFit = linearFit(l2sig, l3dropInd);
    const bool t_neg = indFit.r2 < 0.2;
    std::printf("  NEG-CTRL  : independent-draw L3 -> corr r2=%.3f (FAILS >0.81; was shared) -> %s\n",
                indFit.r2, t_neg ? "ok (no shared cause -> no correlation)" : "FAIL");

    // (3) TOGGLES: L1-only camera == truth clean; L2 toggles the camera base truth<->belief.
    CompositionConfig cfgL1; cfgL1.seed = prof.seed; cfgL1.useL2 = false; cfgL1.useL3 = false;
    const Composition compL1(scene, model, cfgL1);
    std::mt19937_64 rt(compL1.seedL3());
    bool cleanEqualsTrue = true;
    for (int i = 0; i < W * H; ++i) if (compL1.cameraRead(i, rt) != compL1.physicsDepth(i)) { cleanEqualsTrue = false; break; }
    // belief base differs with L2 on vs off
    CompositionConfig cfgNoL2 = cfgAll; cfgNoL2.useL2 = false;
    const Composition compNoL2(scene, model, cfgNoL2);
    double belDiff = 0; int bn = 0;
    for (int i = 0; i < W * H; ++i) { belDiff += std::fabs(comp.beliefMu(i) - compNoL2.beliefMu(i)); ++bn; }
    belDiff /= bn;
    const bool t_toggle = cleanEqualsTrue && belDiff > 1e-6;     // L2-off belief == truth; L2-on belief != truth
    std::printf("  toggles   : L1-only camera==truth=%s ; |belief(L2on)-belief(L2off)|=%.5f (>0) -> %s\n",
                cleanEqualsTrue ? "yes" : "no", belDiff, t_toggle ? "ok" : "FAIL");

    // (4) DETERMINISM: same seed -> identical stream; seed+1 -> different draws, same mean; seed toggle-stable.
    std::mt19937_64 ra(comp.seedL3()), rb(comp.seedL3());
    bool identical = true;
    for (int i = 0; i < W * H; ++i) if (comp.cameraRead(i, ra) != comp.cameraRead(i, rb)) { identical = false; break; }
    CompositionConfig cfg2 = cfgAll; cfg2.seed = prof.seed + 1;
    const Composition comp2(scene, model, cfg2);
    std::mt19937_64 r1(comp.seedL3()), r2(comp2.seedL3());
    double m1 = 0, m2 = 0; int c1 = 0, c2 = 0; bool anyDiff = false;
    for (int i = 0; i < W * H; ++i) {
        const double a = comp.cameraRead(i, r1), b = comp2.cameraRead(i, r2);
        if (a != DEPTH_INVALID) { m1 += a; ++c1; } if (b != DEPTH_INVALID) { m2 += b; ++c2; }
        if (a != b) anyDiff = true;
    }
    m1 /= c1; m2 /= c2;
    const bool seedStable = comp.seedL3() == compNoL2.seedL3();   // toggling L2 didn't shift L3's seed
    const bool t_determ = identical && anyDiff && std::fabs(m1 - m2) < 0.02 && seedStable;
    std::printf("  determ    : same-seed identical=%s ; seed+1 differs=%s mean %.4f vs %.4f ; L3 seed toggle-stable=%s -> %s\n",
                identical ? "yes" : "no", anyDiff ? "yes" : "no", m1, m2, seedStable ? "yes" : "no", t_determ ? "ok" : "FAIL");

    const bool pass = t_divergence && t_shared && t_neg && t_toggle && t_determ;
    std::printf("[SENSOR GATE COMPOSE] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
