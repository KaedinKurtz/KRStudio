#pragma once
// ===========================================================================
// Phase A — GATE A simulated-plant track: builds a PhysX
// PxArticulationReducedCoordinate from the SAME spec as the krs::dyn oracle and
// validates the simulated articulation against the analytical oracle:
//   A1  FK: applyCache(ePOSITION) -> getGlobalPose  vs  oracle FK  (100 configs)
//   A2  revolute on a *detected cylinder axis*: 90 deg sweep, point deviation
//       from the exact analytic rotation about the extracted axis  (< 0.1 mm)
//   A3  parallelogram closed loop via PxD6Joint: residual across motion (< 1e-4)
//   A5  commanded joint torque -> joint acceleration via the cache + analytical
//       utilities (M, gravity, Coriolis)  vs  oracle ABA  (< 1%)
//
// Self-contained: creates and releases its own PxFoundation/PxPhysics/PxScene
// so it never entangles SimulationController's lazily-created world. Gated by
// KRS_ARTIC_SELFTEST (folded into KRS_OVERNIGHT_BENCH). Compiles to a vacuous
// pass when built without PhysX.
// ===========================================================================

namespace krs::dyn {

// Runs the GATE-A simulated-plant battery. Prints "[artic gate] ... PASS/FAIL"
// with real measured numbers; returns true iff every sub-gate passes.
bool runArticulationGate();

} // namespace krs::dyn
