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

// Phase G — GATE H: builds the FANUC articulation through the LIVE
// SimulationController path (buildPhysicsWorld) and validates that live tree
// against the oracle. H1 (live FK <1e-4, >=50 cfg) here; H2/H3 added in G.2/G.3.
// Gated by KRS_ARTIC_LIVE_SELFTEST. Vacuous pass without PhysX.
bool runArticulationLiveGate();

// Phase A — GATE D: the default FANUC sandbox demo (repeated pick-and-place on the
// live parallelogram articulation) measured for stability (D1, loop residual <1e-4
// throughout >=1000 cycles), tracking (D2), no resource growth (D3), determinism (D4).
// Gated by KRS_DEMO_SELFTEST. Vacuous pass without PhysX.
bool runDemoGateD();

} // namespace krs::dyn
