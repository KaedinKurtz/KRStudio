#pragma once
// SensorGates.hpp -- declarations for the synthetic-sensor pipeline gates. Each is pure
// distributional/statistical math (no GL/Qt) and runs from the bench + an env trigger. Every gate pairs a
// measured number with the NAIVE model (clean / fixed-Gaussian / independent-draw / stateless) as the
// FAILING negative control -- which is what proves physical faithfulness.
namespace krs::sensor {

bool runStatsHarnessGate();   // GATE 0  : stats harness self-tests + wrong-statistic neg-ctrl + profile round-trip

} // namespace krs::sensor
