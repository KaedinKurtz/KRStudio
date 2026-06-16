// SensorGate4.cpp -- Phase 4 GATE L2-UNCERTAINTY. Proves the Layer-2 belief field KNOWS WHERE IT IS UNCERTAIN:
//   (1) CONTRAST: sigma over poorly-observed cells (specular/dark -- high material drive -> holes + match
//       noise) is much larger than over well-observed bright cells;
//   (2) CO-LOCATION: recon holes (n==0) lie exactly in the occluded region, and sigma correlates with the
//       shared material drive (the field is uncertain where the SENSOR is unreliable);
//   (3) CALIBRATION (round-trip): for observed cells |mu - Z_true| < 2*sigma for >=90% -- the uncertainty is
//       honest, not arbitrary.
// NEG-CTRL (uniform sigma == median everywhere): CONTRAST ratio ~1 (FAILS) and calibration collapses
// (over-confident in noisy regions). A real recon knows where it's uncertain; a uniform field does not.
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

bool runL2UncertaintyGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE L2-UNCERTAINTY] recon belief field: sigma contrast + hole co-location + calibration\n");

    const int W = 64, H = 48, K = 12;
    SensorProfile prof;
    DepthModel model = DepthModel::fromProfile(prof.depth);

    ReconScene scene; scene.W = W; scene.H = H;
    scene.trueDepth.assign(W * H, 0.0);
    scene.material.assign(W * H, MaterialSample{ 0.0, 0.85, 1.0 });   // bright Lambertian default
    scene.framesObserving.assign(W * H, K);

    // region membership for the gate's group statistics
    std::vector<int> brightIdx, poorIdx, occludedIdx;
    auto inRect = [](int u, int v, int u0, int u1, int v0, int v1) { return u >= u0 && u < u1 && v >= v0 && v < v1; };
    for (int v = 0; v < H; ++v) {
        for (int u = 0; u < W; ++u) {
            const int i = v * W + u;
            scene.trueDepth[i] = 1.0 + 0.6 * (double(u) / W);          // slanted plane 1.0 -> 1.6 m
            const bool spec = inRect(u, v, 10, 22, 8, 20);
            const bool dark = inRect(u, v, 40, 52, 8, 20);
            const bool occl = inRect(u, v, 28, 34, 0, H);             // occluded strip -> recon holes
            if (spec) scene.material[i] = MaterialSample{ 1.0, 0.85, 1.0 };
            if (dark) scene.material[i] = MaterialSample{ 0.0, 0.10, 1.0 };
            if (occl) { scene.framesObserving[i] = 0; occludedIdx.push_back(i); }
            else if (spec || dark) poorIdx.push_back(i);
            else brightIdx.push_back(i);
        }
    }

    std::mt19937_64 rng(prof.seed);
    const ReconField field = reconstruct(scene, model, K, /*sigmaPrior=*/0.5, rng);

    // (1) CONTRAST: mean sigma poorly-observed vs bright (both observed).
    auto meanSigma = [&](const std::vector<int>& idx) {
        std::vector<double> s; for (int i : idx) if (!field.cells[i].hole) s.push_back(field.cells[i].sigma);
        return mean(s);
    };
    const double sigBright = meanSigma(brightIdx);
    const double sigPoor = meanSigma(poorIdx);
    const bool t_contrast = sigPoor > 2.0 * sigBright;
    std::printf("  contrast : sigma poor=%.5f bright=%.5f ratio=%.2f (exp >2) -> %s\n",
                sigPoor, sigBright, sigPoor / sigBright, t_contrast ? "ok" : "FAIL");

    // (2) CO-LOCATION: recon holes (n==0) == the occluded region (precision + recall); sigma vs material drive.
    int holeInOccl = 0, holeTotal = 0;
    for (int i = 0; i < W * H; ++i) if (field.cells[i].hole) { ++holeTotal; }
    for (int i : occludedIdx) if (field.cells[i].hole) ++holeInOccl;
    const double precision = holeTotal ? double(holeInOccl) / holeTotal : 0.0;       // holes that are occluded
    const double recall = occludedIdx.empty() ? 0.0 : double(holeInOccl) / occludedIdx.size();
    // sigma vs material drive over OBSERVED cells (positive correlation).
    std::vector<double> drive, sig;
    for (int i = 0; i < W * H; ++i) if (!field.cells[i].hole) { drive.push_back(materialDrive(scene.material[i])); sig.push_back(field.cells[i].sigma); }
    const LinFit corr = linearFit(drive, sig);
    const bool t_coloc = precision > 0.95 && recall > 0.95 && corr.slope > 0.0 && corr.r2 > 0.3;
    std::printf("  co-locate: holes precision=%.3f recall=%.3f ; sigma~drive slope=%.4f r2=%.3f -> %s\n",
                precision, recall, corr.slope, corr.r2, t_coloc ? "ok (uncertain where sensor is unreliable)" : "FAIL");

    // (3) CALIBRATION: |mu - Z_true| < 2*sigma for >=90% of observed cells (honest uncertainty).
    int observed = 0, covered = 0;
    for (int i = 0; i < W * H; ++i) {
        const ReconCell& c = field.cells[i];
        if (c.hole || c.n < 2) continue;
        ++observed;
        if (std::fabs(c.mu - scene.trueDepth[i]) < 2.0 * c.sigma) ++covered;
    }
    const double coverage = observed ? double(covered) / observed : 0.0;
    const bool t_calib = coverage >= 0.90;
    std::printf("  calibrate: |mu-truth|<2sigma coverage=%.3f of %d observed (exp >=0.90) -> %s\n",
                coverage, observed, t_calib ? "ok (uncertainty is honest)" : "FAIL");

    // ---- NEG-CTRL: uniform sigma = median of the real field's observed sigmas. ----
    std::vector<double> allSig;
    for (int i = 0; i < W * H; ++i) if (!field.cells[i].hole && field.cells[i].n >= 2) allSig.push_back(field.cells[i].sigma);
    std::sort(allSig.begin(), allSig.end());
    const double uniformSigma = allSig.empty() ? 0.0 : allSig[allSig.size() / 2];
    // contrast under uniform sigma -> exactly 1.
    const double negRatio = 1.0;
    // calibration under uniform sigma: same mu, but sigma replaced by the constant -> noisy regions fail.
    int negObs = 0, negCov = 0;
    for (int i = 0; i < W * H; ++i) {
        const ReconCell& c = field.cells[i];
        if (c.hole || c.n < 2) continue;
        ++negObs;
        if (std::fabs(c.mu - scene.trueDepth[i]) < 2.0 * uniformSigma) ++negCov;
    }
    const double negCoverage = negObs ? double(negCov) / negObs : 0.0;
    // The real field achieves contrast>2 AND calibration>=0.90 SIMULTANEOUSLY; no single uniform sigma can --
    // raise it and you lose contrast (ratio->1), keep the median and the noisy tail is under-covered. So the
    // uniform field FAILS contrast (definitionally ~1) AND is strictly worse calibrated than the real field.
    const bool t_neg = negRatio < 2.0 && negCoverage < coverage - 0.01;
    std::printf("  NEG-CTRL : uniform sigma=%.5f -> contrast ratio=1.0 (FAILS >2); calibration=%.3f < real %.3f -> %s\n",
                uniformSigma, negCoverage, coverage, t_neg ? "ok (no single sigma fits both regimes)" : "FAIL");

    const bool pass = t_contrast && t_coloc && t_calib && t_neg;
    std::printf("[SENSOR GATE L2-UNCERTAINTY] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
