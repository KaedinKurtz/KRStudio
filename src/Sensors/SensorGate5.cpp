// SensorGate5.cpp -- Phase 5 GATE COMPOSE (hardened after adversarial review). Proves the three layers
// compose correctly -- and proves the L2/L3 correlation comes from ONE SHARED SCALAR material cause, not
// merely two quantities co-elevated in the same regions:
//   (1) CAMERA READS THE BELIEF surface: with L2 on, the mean live read tracks the L2 belief mu (NOT the L1
//       truth); with L2 off it tracks truth. So the toggle genuinely re-bases the camera (not a tautology).
//   (2) SHARED SCALAR CAUSE: over a 2D material sweep (specular x albedo varied INDEPENDENTLY), corr(L2 sigma,
//       L3 dropout) is a TIGHT curve (r2>0.9) ONLY because both read the same materialDrive=max(spec,dark).
//       NEG-CTRL: an L3 dropout wired to a DIFFERENT channel (specular-only) SCATTERS against L2 sigma
//       (r2<0.75), because a dark-but-non-specular material is uncertain in L2 yet wouldn't drop in L3.
//   (3) DIVERGENCE sanity: physics(L1) != camera(L2+L3); belief honest. (4) DETERMINISM + toggle-stable seeds.
// ALL of this is INTERNAL self-consistency of the synthetic layers -- it makes NO claim about real hardware.
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
    s.trueDepth.assign(W * H, 1.3);                       // flat: L2 sigma driven by material, not Z
    s.material.assign(W * H, MaterialSample{ 0.0, 0.85, 1.0 });
    s.framesObserving.assign(W * H, K);
    auto inRect = [](int u, int v, int u0, int u1, int v0, int v1) { return u >= u0 && u < u1 && v >= v0 && v < v1; };
    for (int v = 0; v < H; ++v)
        for (int u = 0; u < W; ++u) {
            const int i = v * W + u;
            if (inRect(u, v, 12, 24, 8, 28)) s.material[i] = MaterialSample{ 1.0, 0.85, 1.0 };   // specular
            if (inRect(u, v, 40, 52, 8, 28)) s.material[i] = MaterialSample{ 0.0, 0.10, 1.0 };   // dark
            if (inRect(u, v, 30, 33, 0, H)) s.framesObserving[i] = 0;                            // occlusion -> belief holes
        }
    return s;
}

bool runComposeGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE COMPOSE] L1 true / L2 belief / L3 live -- shared SCALAR cause + toggles + determinism\n");

    const int W = 64, H = 48, K = 24;
    SensorProfile prof;
    DepthModel model = DepthModel::fromProfile(prof.depth);
    const ReconScene scene = buildComposeScene(W, H, K);

    CompositionConfig cfgAll; cfgAll.seed = prof.seed;
    const Composition comp(scene, model, cfgAll);
    CompositionConfig cfgNoL2 = cfgAll; cfgNoL2.useL2 = false;
    const Composition compNoL2(scene, model, cfgNoL2);

    // (1) CAMERA READS THE BELIEF surface (not truth). Average T live reads per cell; with L2 on the mean
    //     tracks belief mu, with L2 off it tracks truth. (Verifies the toggle re-bases the camera.)
    auto meanCamErr = [&](const Composition& c, bool vsBelief) {
        double e2 = 0; int m = 0;
        for (int i = 0; i < W * H; ++i) {
            if (c.beliefHole(i)) continue;
            std::mt19937_64 r(c.seedL3() ^ (std::uint64_t(i) * 0x9E3779B97F4A7C15ull));
            double acc = 0; int valid = 0;
            for (int t = 0; t < 400; ++t) { const double z = c.cameraRead(i, r); if (z != DEPTH_INVALID) { acc += z; ++valid; } }
            if (!valid) continue;
            const double mc = acc / valid;
            const double ref = vsBelief ? c.beliefMu(i) : c.physicsDepth(i);
            e2 += (mc - ref) * (mc - ref); ++m;
        }
        return std::sqrt(e2 / m);
    };
    const double camVsBelief = meanCamErr(comp, true);    // L2 on: should be small (tracks belief)
    const double camVsTruth = meanCamErr(comp, false);    // L2 on: larger (belief != truth)
    const double offVsTruth = meanCamErr(compNoL2, false);// L2 off: small (tracks truth)
    const bool t_readsBelief = camVsBelief < 0.5 * camVsTruth && offVsTruth < camVsTruth;
    std::printf("  reads-bel: L2on camera vs belief=%.5f vs truth=%.5f ; L2off vs truth=%.5f -> %s\n",
                camVsBelief, camVsTruth, offVsTruth, t_readsBelief ? "ok (camera observes the belief surface)" : "FAIL");

    // (2) SHARED SCALAR CAUSE: 2D material sweep (specular x albedo INDEPENDENT). corr(L2 sigma, L3 dropout)
    //     is tight only because both read the same materialDrive; a different channel (specular-only) scatters.
    std::mt19937_64 swr(prof.seed ^ 0xC0FFEEull);
    auto l2Sigma = [&](const MaterialSample& m) {
        ReconScene s; s.W = 16; s.H = 16; const int n = 256;
        s.trueDepth.assign(n, 1.3); s.material.assign(n, m); s.framesObserving.assign(n, K);
        const ReconField f = reconstruct(s, model, K, 0.5, swr);
        std::vector<double> sg; for (const auto& c : f.cells) if (!c.hole) sg.push_back(c.sigma);
        return mean(sg);
    };
    auto l3Drop = [&](const MaterialSample& m) {
        int holes = 0; const int T = 4000;
        for (int t = 0; t < T; ++t) if (model.sample(1.3, m, swr) == DEPTH_INVALID) ++holes;
        return double(holes) / T;
    };
    const double lamb = double(prof.depth.holeRateLambert), spc = double(prof.depth.holeRateSpecular);
    std::vector<double> swSig, swDropReal, swDropSpecOnly;
    const double specs[3] = { 0.0, 0.5, 1.0 }, albs[3] = { 0.85, 0.30, 0.10 };
    for (double sp : specs)
        for (double al : albs) {
            const MaterialSample m{ sp, al, 1.0 };
            swSig.push_back(l2Sigma(m));
            swDropReal.push_back(l3Drop(m));
            swDropSpecOnly.push_back(lamb + (spc - lamb) * sp);   // impostor: dropout from the specular channel only
        }
    const LinFit fitReal = linearFit(swSig, swDropReal);
    const LinFit fitSpec = linearFit(swSig, swDropSpecOnly);
    const bool t_shared = fitReal.slope > 0 && fitReal.r2 > 0.90 && fitSpec.r2 < 0.75;
    std::printf("  shared   : same-scalar corr r2=%.3f (>0.90) ; different-channel(spec-only) r2=%.3f (<0.75) -> %s\n",
                fitReal.r2, fitSpec.r2, t_shared ? "ok (one shared materialDrive feeds both)" : "FAIL");

    // (3) DIVERGENCE sanity: physics != live camera; belief honest (within 2 sigma).
    std::mt19937_64 rcam(comp.seedL3());
    double dCam = 0; int cov = 0, obs = 0, vc = 0;
    for (int i = 0; i < W * H; ++i) {
        if (!comp.beliefHole(i)) { ++obs; if (std::fabs(comp.physicsDepth(i) - comp.beliefMu(i)) < 2.0 * comp.beliefSigma(i)) ++cov; }
        const double z = comp.cameraRead(i, rcam); if (z != DEPTH_INVALID) { dCam += std::fabs(comp.physicsDepth(i) - z); ++vc; }
    }
    dCam /= vc; const double coverage = double(cov) / obs;
    const bool t_diverge = dCam > 0.002 && coverage >= 0.88;
    std::printf("  diverge  : |L1-camera|=%.5f (>0.002) ; belief coverage=%.3f (>=0.88) -> %s\n",
                dCam, coverage, t_diverge ? "ok" : "FAIL");

    // (4) DETERMINISM + toggle-stable seeds; occlusion exercises the belief-hole path.
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
    int holes = 0; for (int i = 0; i < W * H; ++i) if (comp.beliefHole(i)) ++holes;
    const bool seedStable = comp.seedL3() == compNoL2.seedL3();
    const bool t_determ = identical && anyDiff && std::fabs(m1 - m2) < 0.01 && seedStable && holes > 0;
    std::printf("  determ   : same-seed identical=%s seed+1 differs=%s mean %.4f/%.4f ; L3 seed stable=%s belief-holes=%d -> %s\n",
                identical ? "yes" : "no", anyDiff ? "yes" : "no", m1, m2, seedStable ? "yes" : "no", holes, t_determ ? "ok" : "FAIL");

    const bool pass = t_readsBelief && t_shared && t_diverge && t_determ;
    std::printf("[SENSOR GATE COMPOSE] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
