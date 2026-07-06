#pragma once
// ===========================================================================
// Phase V — SINGLE SOURCE OF TRUTH for the FANUC-430 visible articulation.
//
// The 17-solid -> serial-link assignment, the canonical 4-link spec, and the
// scene setup (import STEP -> build live articulation -> viz mapping) live HERE
// and are consumed by BOTH the V-assign gate AND the app boot scene. Neither
// hand-rolls its own assignment: if they diverged, V-assign would stay green
// while the rendered arm was subtly wrong. assignmentFingerprint() + the gate's
// equality assert make any future divergence trip a gate.
// ===========================================================================

#include <vector>
#include <string>
#include <entt/entt.hpp>
#include "ArticulationSpec.hpp"

class Scene;
class SimulationController;

namespace krs::fanuc {

constexpr int kSolidCount = 27;   // FANUC-430 renderable bodies: 17 SOLIDs + 10 free SHELLs

// THE assignment. inspectIndex 0..16 (STEP solid (k+1)) -> serial link:
// 0 fixed base, 1 J1 yaw (carousel), 2 J2 shoulder (upper arm), 3 J3 elbow
// (forearm + counterbalance strut + bolts + wrist held rigid; J4 frozen).
int solidLink(int inspectIndex);

// Stable fingerprint of solidLink(0..16) — the gate asserts the app's loaded
// assignment equals this, so editing the map without updating the expected
// fingerprint trips the gate.
std::string assignmentFingerprint();

// The canonical serial spec GATE H/D validated (J1 Y@origin, J2 X@(0,0.74,0.305),
// J3 X@(+0,1.075,0), J4 Z@(+0,0.25,0) present but frozen).
krs::dyn::RobotArticSpec canonicalSpec();

// First existing FANUC STEP path among the deployed/working-dir candidates.
std::string findStepAsset();

// GATE FANUC-6DOF (E2.1): the REAL STEP through the DEFAULT boot pipeline
// (parseAssembly -> buildNamedSerialChain) must yield a TRUE 6-DoF chain --
// dof==6, zero ambiguous joints, quality axes (J0 vertical, no last-resort
// guesses), FK(q=0)==parsed placements -- and articSpecFromGraph must convert it
// into a spec PhysX builds with articDofCount()==6 and 6 INDEPENDENT dofs.
// NEG-CTRLs: the legacy canonicalSpec is still 4 joints with J4 frozen (the cap
// this gate retires), and an ambiguous joint truncates the spec honestly instead
// of fabricating an axis. SKIPs (tri-state) when OCCT or the STEP asset is absent.
bool runFanuc6Gate();

struct Setup {
    bool ok = false;
    int  solids = 0;
    std::string message;
    std::vector<entt::entity> solidEntity;                      // [inspectIndex] -> entity (null if absent)
    std::vector<std::vector<entt::entity>> movingLinkEntities;  // [movingLink 0..3] -> entities
    std::string fingerprint;                                    // == assignmentFingerprint() at setup time
};

// Import the FANUC STEP into `scene`, build the canonical articulation on `sim`
// (play(), zero gravity), pose it at rest (q=0), and register the solid->link viz
// mapping. The SAME call is used by the gate and the app boot. Caller drives the
// joints (e.g. SimulationController demo-drive) and renders. Vacuous {ok=false}
// without OpenCASCADE/PhysX.
Setup setupFanucScene(Scene& scene, SimulationController& sim, const std::string& stepPath);

} // namespace krs::fanuc
