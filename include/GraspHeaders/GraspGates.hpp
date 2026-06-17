#pragma once
// GraspGates.hpp -- declarations for the grasp-pipeline headless gates (namespace krs::grasp). Each pairs a
// measured number with the FAILING negative control that makes it an anti-cheat, and prints the locked-physics
// configHash so any silent softening of the criterion is visible in the logs.
namespace krs::grasp {

bool runGraspImportGate();   // GATE IMPORT: YCB load + real-meter scale + mass/inertia + NaN; x1000 neg-ctrl
bool runGraspCoacdGate();    // GATE COACD: concavity survives (ball rests inside bowl); convex-hull filler FAILS
bool runGraspSuccessGate();  // GATE SUCCESS-CRITERION: good grasp passes / bad fails under LOCKED physics; softened-world anti-cheat neg-ctrl
bool runGraspPlannerGate();  // GATE PLANNER: antipodal heuristic raises the success rate baseline->tuned; random neg-ctrl
bool runGraspFailureCatalogGate(); // GATE FAILURE-CATALOG: classify 100% of tuned-planner failures; incomplete-taxonomy neg-ctrl

} // namespace krs::grasp
