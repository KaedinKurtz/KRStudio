#pragma once
// ===========================================================================
// Phase V — GATE V (V.1 / V-assign): validates the 17-solid -> serial-link
// ASSIGNMENT for the visibly-articulating FANUC. The offset-fit transform (V2)
// is circular (the rest offset absorbs any mis-assignment), so correctness is
// proven with an INDEPENDENT geometric witness: each solid's auto-detected
// B-Rep bores (AttachmentComponent), carried by its assigned link's delta-pose
// over the demo motion, must keep every SHARED HINGE coincident (a real joint
// axis is shared by both its links) while the joints actually articulate. A
// deliberately mis-assigned solid MUST break coincidence (negative control,
// same rigor as the GATE-D frozen-robot proof). Reads link poses from the SAME
// canonical articulation graph GATE H/D validated.
//
// Gated by KRS_VASSIGN_SELFTEST (folded into KRS_OVERNIGHT_BENCH). Vacuous pass
// when built without PhysX or without OpenCASCADE (no STEP to assign).
// ===========================================================================

namespace krs::dyn {

// Runs the V.1 / V-assign correctness battery. Prints "[vassign] ... PASS/FAIL"
// with real measured numbers; returns true iff the assignment is validated and
// the negative control is rejected (non-vacuous).
bool runVisibleArticGateV();

// Phase V GATE V.6: exercises the EXACT boot path the app uses (the shared
// krs::fanuc helper + the kinematic demo drive + the per-frame tick loop) and
// confirms the FANUC visibly moves end-to-end, with the assignment fingerprint
// matching the canonical map. Gated by KRS_FANUC_BOOT_SELFTEST. Vacuous pass
// without PhysX/OpenCASCADE.
bool runFanucBootGateV6();

// Diagnostic (KRS_FANUC_SOLID_DUMP): imports the FANUC STEP and dumps every
// enumerated solid's vertex/index counts, world bbox and assigned link, so a
// non-rendering / under-enumerated / empty-mesh solid is visible. Returns true.
bool runFanucSolidDump();

} // namespace krs::dyn
