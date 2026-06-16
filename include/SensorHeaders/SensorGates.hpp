#pragma once
// SensorGates.hpp -- declarations for the synthetic-sensor pipeline gates. Each is pure
// distributional/statistical math (no GL/Qt) and runs from the bench + an env trigger. Every gate pairs a
// measured number with the NAIVE model (clean / fixed-Gaussian / independent-draw / stateless) as the
// FAILING negative control -- which is what proves physical faithfulness.
namespace krs::sensor {

bool runStatsHarnessGate();    // GATE 0         : stats harness self-tests + wrong-statistic neg-ctrl + profile round-trip
bool runRgbIntrinsicsGate();   // GATE INTRINSICS : distort/undistort round-trip <0.5px; pinhole neg-ctrl fails at edges
bool runRgbNoiseStatsGate();   // GATE NOISE-STATS: shot variance scales with signal (Poisson slope); fixed-Gaussian neg-ctrl flat
bool runDepthStructGate();     // GATE DEPTH-STRUCT: quadratic Z^2 range + material holes + flying pixels + min-Z; clean+Gaussian neg-ctrl
bool runImuAllanGate();        // GATE IMU-ALLAN: Allan white slope + bias-instability floor + integrated drift; per-sample-Gaussian neg-ctrl
bool runL2UncertaintyGate();   // GATE L2-UNCERTAINTY: sigma contrast + hole co-location + calibration; uniform-sigma neg-ctrl
bool runComposeGate();         // GATE COMPOSE: L1 true vs L2 belief + L3 noise; shared correlation; toggles + determinism; independent-draw neg-ctrl

} // namespace krs::sensor
