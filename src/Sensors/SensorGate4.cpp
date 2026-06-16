// SensorGate4.cpp -- Phase 4 GATE L2-UNCERTAINTY (hardened after adversarial review). Proves the Layer-2
// belief field is an HONEST, Bayesian, shared-cause uncertainty -- not just "bigger numbers in noisy regions":
//   (1) CONTRAST that RESPONDS to the cause: sigma_poor/sigma_bright > 2.5 with the material noise penalty on,
//       and collapses to ~1 with the penalty OFF (so the contrast is the shared material, not a free knob);
//   (2) CALIBRATION by reduced chi-square: mean((mu-Z_true)^2 / sigma^2) ~ 1 (TWO-SIDED -- detects an inflated
//       OR over-confident sigma; the old one-sided <2sigma coverage was tautological);
//   (3) MU ACCURACY standalone: RMS(mu - Z_true) over bright cells below a physical bound, independent of sigma;
//   (4) 1/sqrt(n) BAYESIAN SCALING: at fixed material, sigma ~ n^-0.5 across observation counts -- a per-region
//       CONSTANT impostor (right pattern, wrong rule) FAILS this;
//   (5) SHARED-FIELD CORRELATION (the directive's core): across a material sweep, L2 sigma and L3 dropout rate
//       co-vary (r>0.9) because BOTH read the same materialDrive; a channel materialDrive ignores moves neither.
// NEG-CTRLS: (A) uniform sigma -> no contrast + chi-square off; (B) per-region-constant sigma -> passes
// contrast but FAILS 1/sqrt(n) (it never divides by sqrt(n)). Both are excluded.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include "Layer2Gpis.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace krs::sensor {

// Reduced chi-square over cells with a RELIABLE variance estimate (n >= minN). Tiny-n cells have a
// heavy-tailed normalized residual (Student-t, E[t^2]=(n-1)/(n-3)) that biases the mean upward; with n>=12
// the estimate is stable (E[chi^2] ~ 1.1) so a roughly-2-sided ~1 test cleanly detects sigma mis-scaling.
static double chiSqReduced(const ReconField& f, const ReconScene& s, const std::vector<double>& sigmaOverride, int minN) {
    double acc = 0.0; int m = 0;
    for (int i = 0; i < s.W * s.H; ++i) {
        const ReconCell& c = f.cells[i];
        if (c.hole || c.n < minN) continue;
        const double sg = sigmaOverride.empty() ? c.sigma : sigmaOverride[i];
        const double r = c.mu - s.trueDepth[i];
        acc += (r * r) / (sg * sg); ++m;
    }
    return m ? acc / m : 0.0;
}

bool runL2UncertaintyGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE L2-UNCERTAINTY] honest Bayesian shared-cause recon uncertainty\n");

    const int W = 64, H = 48, K = 24;
    SensorProfile prof;
    DepthModel model = DepthModel::fromProfile(prof.depth);

    ReconScene scene; scene.W = W; scene.H = H;
    scene.trueDepth.assign(W * H, 0.0);
    scene.material.assign(W * H, MaterialSample{ 0.0, 0.85, 1.0 });
    scene.framesObserving.assign(W * H, K);

    std::vector<int> brightFull, poorIdx, occludedIdx;
    std::vector<int> nBand[4]; const int nObs[4] = { 3, 6, 12, 24 };
    auto inRect = [](int u, int v, int u0, int u1, int v0, int v1) { return u >= u0 && u < u1 && v >= v0 && v < v1; };
    for (int v = 0; v < H; ++v) {
        for (int u = 0; u < W; ++u) {
            const int i = v * W + u;
            scene.trueDepth[i] = 1.0 + 0.6 * (double(u) / W);
            const bool strip = inRect(u, v, 2, 8, 0, H);              // bright, varied observation count
            const bool spec = inRect(u, v, 12, 24, 8, 20);
            const bool dark = inRect(u, v, 40, 52, 8, 20);
            const bool occl = inRect(u, v, 28, 34, 0, H);
            if (spec) scene.material[i] = MaterialSample{ 1.0, 0.85, 1.0 };
            if (dark) scene.material[i] = MaterialSample{ 0.0, 0.10, 1.0 };
            if (occl) { scene.framesObserving[i] = 0; occludedIdx.push_back(i); continue; }
            if (strip) {                                              // 4 v-bands with different n
                const int band = std::min(3, v / (H / 4));
                scene.framesObserving[i] = nObs[band];
                nBand[band].push_back(i);
                continue;
            }
            if (spec || dark) poorIdx.push_back(i);
            else brightFull.push_back(i);
        }
    }

    auto reconWith = [&](double penalty) {
        DepthModel m = model; m.matchNoisePenalty = penalty;
        std::mt19937_64 r(prof.seed);
        return reconstruct(scene, m, K, 0.5, r);
    };
    const ReconField field = reconWith(3.0);

    auto meanSig = [&](const std::vector<int>& idx, const std::vector<double>& ov) {
        std::vector<double> s;
        for (int i : idx) if (!field.cells[i].hole) s.push_back(ov.empty() ? field.cells[i].sigma : ov[i]);
        return mean(s);
    };
    const std::vector<double> none;

    // (1) CONTRAST that responds to the shared material noise penalty.
    const double ratioReal = meanSig(poorIdx, none) / meanSig(brightFull, none);
    const ReconField field0 = reconWith(0.0);
    auto meanSig0 = [&](const std::vector<int>& idx) {
        std::vector<double> s; for (int i : idx) if (!field0.cells[i].hole) s.push_back(field0.cells[i].sigma);
        return mean(s);
    };
    const double ratio0 = meanSig0(poorIdx) / meanSig0(brightFull);
    const bool t_contrast = ratioReal > 2.5 && ratio0 < 1.5;
    std::printf("  contrast : ratio=%.2f (penalty on) vs %.2f (penalty off) -> %s\n",
                ratioReal, ratio0, t_contrast ? "ok (driven by shared material, not a free knob)" : "FAIL");

    // (2) CALIBRATION: reduced chi-square ~ 1 over reliably-observed cells (two-sided -- detects an inflated
    //     OR over-confident sigma). E[chi^2] sits slightly above 1 from the Student factor at finite n.
    const double chi2 = chiSqReduced(field, scene, none, 12);
    const bool t_calib = chi2 > 0.8 && chi2 < 1.4;
    std::printf("  calibrate: reduced chi^2=%.3f (exp ~1.0-1.2, two-sided) -> %s\n", chi2, t_calib ? "ok (honest magnitude)" : "FAIL");

    // (3) MU ACCURACY standalone (independent of sigma): RMS over bright cells below a physical bound.
    double muErr2 = 0; int muN = 0;
    for (int i : brightFull) { const double e = field.cells[i].mu - scene.trueDepth[i]; muErr2 += e * e; ++muN; }
    const double rmsMu = std::sqrt(muErr2 / muN);
    const bool t_mu = rmsMu < 0.0025;       // ~ rangeSigma(Z~1.3)/sqrt(K) + speckle/sqrt(K) ~ 1mm; bound 2.5mm
    std::printf("  mu-accur : RMS(mu-truth)=%.5f m over bright (bound 0.0025) -> %s\n", rmsMu, t_mu ? "ok" : "FAIL");

    // (4) 1/sqrt(n) Bayesian scaling at fixed material (bright strip, n in {3,6,12,24}).
    std::vector<double> ln, lsig;
    for (int b = 0; b < 4; ++b) { ln.push_back(nObs[b]); lsig.push_back(meanSig(nBand[b], none)); }
    const double nSlope = powerFit(ln, lsig).exponent;
    const bool t_nscale = std::fabs(nSlope + 0.5) < 0.12;
    std::printf("  n-scaling: sigma ~ n^%.3f (exp -0.5; Bayesian fusion) -> %s\n", nSlope, t_nscale ? "ok" : "FAIL");

    // (5) SHARED-FIELD CORRELATION: L2 sigma and L3 dropout co-vary across a material sweep (same cause).
    std::mt19937_64 swr(prof.seed ^ 0x5EED5u);
    auto l3Drop = [&](const MaterialSample& m) {
        int holes = 0; const int T = 6000;
        for (int t = 0; t < T; ++t) if (model.sample(1.3, m, swr) == DEPTH_INVALID) ++holes;
        return double(holes) / T;
    };
    auto l2Sigma = [&](const MaterialSample& m) {
        ReconScene s; s.W = 16; s.H = 16; const int n = 256;
        s.trueDepth.assign(n, 1.3); s.material.assign(n, m); s.framesObserving.assign(n, K);
        const ReconField f = reconstruct(s, model, K, 0.5, swr);
        std::vector<double> sg; for (const auto& c : f.cells) if (!c.hole) sg.push_back(c.sigma);
        return mean(sg);
    };
    std::vector<double> sweepDrop, sweepSig;
    for (int s = 0; s <= 8; ++s) {
        const double alb = 0.50 - 0.05 * s;                          // drive 0 -> 0.8 (graded)
        const MaterialSample m{ 0.0, alb, 1.0 };
        sweepDrop.push_back(l3Drop(m)); sweepSig.push_back(l2Sigma(m));
    }
    const LinFit sweepFit = linearFit(sweepDrop, sweepSig);
    // control: vary a channel materialDrive IGNORES (incidenceCos) -> neither L3 drop nor L2 sigma should move.
    std::vector<double> ctlDrop, ctlSig;
    for (int s = 0; s <= 4; ++s) { const MaterialSample m{ 0.0, 0.85, 1.0 - 0.15 * s }; ctlDrop.push_back(l3Drop(m)); ctlSig.push_back(l2Sigma(m)); }
    const double ctlDropSpread = *std::max_element(ctlDrop.begin(), ctlDrop.end()) - *std::min_element(ctlDrop.begin(), ctlDrop.end());
    const double ctlSigSpread = (*std::max_element(ctlSig.begin(), ctlSig.end()) - *std::min_element(ctlSig.begin(), ctlSig.end())) / mean(ctlSig);
    const bool t_shared = sweepFit.slope > 0 && sweepFit.r2 > 0.81 && ctlDropSpread < 0.02 && ctlSigSpread < 0.1;
    std::printf("  shared   : corr(L3drop,L2sigma) slope>0 r2=%.3f (exp >0.81); ignored-channel spread drop=%.3f sig=%.2f -> %s\n",
                sweepFit.r2, ctlDropSpread, ctlSigSpread, t_shared ? "ok (one shared material cause)" : "FAIL");

    // (6) co-location sanity: recon holes (n==0) are the occluded region (the geometric recon-hole source).
    int holeInOccl = 0, holeTotal = 0;
    for (int i = 0; i < W * H; ++i) if (field.cells[i].hole) ++holeTotal;
    for (int i : occludedIdx) if (field.cells[i].hole) ++holeInOccl;
    const double precision = holeTotal ? double(holeInOccl) / holeTotal : 0.0;
    const double recall = occludedIdx.empty() ? 0.0 : double(holeInOccl) / occludedIdx.size();
    const bool t_coloc = precision > 0.95 && recall > 0.95;
    std::printf("  holes    : precision=%.3f recall=%.3f vs occlusion -> %s\n", precision, recall, t_coloc ? "ok" : "FAIL");

    // ---- NEG-A: uniform sigma = median -> no contrast + chi-square off. ----
    std::vector<double> obsSig;
    for (int i = 0; i < W * H; ++i) if (!field.cells[i].hole && field.cells[i].n >= 2) obsSig.push_back(field.cells[i].sigma);
    std::sort(obsSig.begin(), obsSig.end());
    const double uni = obsSig[obsSig.size() / 2];
    std::vector<double> sigUniform(W * H, uni);
    const double chi2Uni = chiSqReduced(field, scene, sigUniform, 12);
    const double ratioUni = 1.0;
    const bool t_negA = ratioUni < 2.5 && (chi2Uni < 0.8 || chi2Uni > 1.4);
    std::printf("  NEG-A    : uniform sigma -> contrast=1.0 (FAILS) chi^2=%.3f (FAILS ~1) -> %s\n",
                chi2Uni, t_negA ? "ok" : "FAIL");

    // ---- NEG-B: per-region constant sigma (right pattern, wrong rule -- no 1/sqrt(n)). Passes contrast but
    //      FAILS the n-scaling test, because a constant ignores observation count. ----
    const double sigBrightConst = meanSig(brightFull, none);
    const double sigPoorConst = meanSig(poorIdx, none);
    std::vector<double> sigRegion(W * H, sigBrightConst);
    for (int i : poorIdx) sigRegion[i] = sigPoorConst;
    std::vector<double> bn, bs;
    for (int b = 0; b < 4; ++b) { bn.push_back(nObs[b]); bs.push_back(sigBrightConst); }   // constant across n-bands
    const double negBslope = powerFit(bn, bs).exponent;
    const double ratioRegion = sigPoorConst / sigBrightConst;        // passes contrast (by construction)
    const bool t_negB = ratioRegion > 2.5 && std::fabs(negBslope + 0.5) > 0.12;   // passes contrast, FAILS 1/sqrt(n)
    std::printf("  NEG-B    : per-region const -> contrast=%.2f (PASSES) but n-scaling slope=%.3f (FAILS -0.5) -> %s\n",
                ratioRegion, negBslope, t_negB ? "ok (1/sqrt(n) excludes it)" : "FAIL");

    const bool pass = t_contrast && t_calib && t_mu && t_nscale && t_shared && t_coloc && t_negA && t_negB;
    std::printf("[SENSOR GATE L2-UNCERTAINTY] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
