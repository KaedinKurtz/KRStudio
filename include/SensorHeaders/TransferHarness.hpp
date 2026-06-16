#pragma once
// TransferHarness.hpp -- Phase 6: the sim-to-real transfer-VALIDATION harness. It computes a statistical
// FINGERPRINT of a sensor instance (profile + seed) and compares two fingerprints.
//
// THE HONEST LIMIT (read this): a real sim-to-real TRANSFER claim needs a real D456 capture, which is NOT
// available. So this harness is gated ONLY against a SECOND SYNTHETIC instance. A match proves the synthetic
// pipeline is SELF-CONSISTENT across independent draws of the same profile -- it does NOT prove the synthetic
// sensor matches real hardware. Real-transfer is UNVALIDATED, awaiting an operator capture. The gate prints
// this label every run; nothing here may be read as evidence of sim-to-real transfer.
#include "SensorProfile.hpp"
#include <vector>
#include <cstdint>

namespace krs::sensor {

struct SensorFingerprint {
    double rgbShotSlope{0};     // variance-vs-signal slope == 1/photonsPerDN
    double rgbReadFloor{0};     // PTC intercept == readNoiseDN^2
    double depthQuadExp{0};     // sigma-vs-Z power-fit exponent (~2)
    double depthHoleSpec{0};    // measured specular hole rate
    double imuWhiteStd{0};      // white-noise discrete std at tau=dt
    double imuAllanFloor{0};    // min Allan deviation (the bias-instability floor) -- stable across seeds
    std::vector<int> rgbHist;   // histogram of noisy DN at a fixed mid signal (distribution shape)
};

// Compute a fingerprint for `prof` using seed (prof.seed XOR seedSalt) -> an independent deterministic instance.
SensorFingerprint computeFingerprint(const SensorProfile& prof, std::uint64_t seedSalt);

struct TransferComparison {
    double maxRelDiff{0};   // worst relative difference across the scalar fingerprint dimensions
    double histL1{0};       // L1 distance between the normalized RGB histograms
    bool   match{false};    // within tolerance on BOTH
};

// Compare two fingerprints. match == (maxRelDiff < relTol) && (histL1 < histTol).
TransferComparison compareFingerprints(const SensorFingerprint& a, const SensorFingerprint& b,
                                       double relTol, double histTol);

// The mandatory honesty label printed by the transfer gate.
const char* transferHonestyLabel();

} // namespace krs::sensor
