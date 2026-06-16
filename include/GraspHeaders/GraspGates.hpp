#pragma once
// GraspGates.hpp -- declarations for the grasp-pipeline headless gates (namespace krs::grasp). Each pairs a
// measured number with the FAILING negative control that makes it an anti-cheat, and prints the locked-physics
// configHash so any silent softening of the criterion is visible in the logs.
namespace krs::grasp {

bool runGraspImportGate();   // GATE IMPORT: YCB load + real-meter scale + mass/inertia + NaN; x1000 neg-ctrl

} // namespace krs::grasp
