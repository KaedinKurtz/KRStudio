#pragma once
// ===========================================================================
// DATA NODES -- wire the .klut / .krec data ecosystem into the node graph.
//
// Four nodes bridge recorded/characterized data into (and out of) the live
// node graph, all pure-CPU and headlessly gateable:
//
//   * data_log        -- accumulates (t, Value) rows into an in-node
//                        krs::rec::DataTable while Enable is high (every Nth
//                        eval), and flush()es them to a CSV/JSON file under a
//                        log folder auto-created on first flush.
//   * data_import     -- lazily loads a CSV/JSON DataTable and samples it
//                        (axisCol,valueCol,interp) at the Axis input -> Value,
//                        letting real experimental data drive the scene.
//   * lut_sample      -- lazily loads a .klut Lut and samples it by axis count
//                        (sample1D(X) or sample2D(X,Y)) -> Value.
//   * data_characterize -- bakes a CSV column pair into a reusable .klut via
//                        krs::klut::characterizeFromSamples + saveKLut.
//
// The gate (KRS_DATANODES_SELFTEST) exercises all four end-to-end against the
// real krs::rec / krs::klut round-trips, with a NEG-CTRL for a missing import
// file. Declares only the gate; the node classes are file-local to DataNodes.cpp
// and register themselves with the NodeFactory at load time.
// ===========================================================================

namespace krs::datanodes {

// Headless gate (KRS_DATANODES_SELFTEST). Returns true iff all four nodes pass:
//   (1) data_log with Enable=true logs a ramp for N evals then flush() writes a
//       file under an auto-created logs/ folder with ~N rows; Enable=false logs
//       nothing (neg-ctrl).
//   (2) data_import points at a known CSV and, fed an Axis between samples,
//       outputs the interpolated truth.
//   (3) lut_sample points at an authored .klut and reproduces sample1D.
//   (4) data_characterize reads a y=2x+1 CSV, bakes a .klut; reload+sample is
//       ~2x+1 within tol.
//   NEG-CTRL: data_import on a MISSING file outputs a defined default (0) and
//   does not crash.
bool runDataNodesGate();

} // namespace krs::datanodes
