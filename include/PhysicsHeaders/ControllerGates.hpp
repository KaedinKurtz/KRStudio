#pragma once
// ControllerGates.hpp -- Phase 4 controller-gate declarations.
namespace krs::ctrl {

// C-track (KRS_CTRACK_SELFTEST): peak DYNAMIC tracking error under a moving setpoint -- computed torque
// (after) < bound while the soft PD (before, the ~0.23 rad deferred lag) fails it.
bool runControllerTrackGate();

// C-knob (KRS_CKNOB_SELFTEST): a goal-knob node's dial drives the live joint to the commanded angle,
// FK-verified <1e-4; disconnected knob -> no motion.
bool runControllerKnobGate();

// C-glass (KRS_CGLASS_SELFTEST): the glass robot's link transforms come from the PLANNED joint config
// (FK of planned), not the live ones; feeding live values collapses plan==current (the neg-ctrl).
bool runControllerGlassGate();

} // namespace krs::ctrl
