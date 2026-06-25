#pragma once
// ===========================================================================
// ROBOT-BUILDER SCENE BRIDGE
//
// The data ops (krs::rbuild::EditController on a RobotGraph) are proven headless,
// but the running app never held a live RobotGraph and nothing bridged its bodies
// to RENDERED scene entities. This module is that (gated) glue:
//   * buildDemoGraph()    -- a synthetic, renderable robot so every edit op is
//                            exercisable without an OCCT STEP import.
//   * spawnGraphBodies()  -- the body->entity RENDER BRIDGE: each RBBody becomes a
//                            renderable, pickable scene entity. THIS is what makes
//                            the panel-edited graph appear on screen.
//   * runRobotBuilderBridgeGate() -- asserts the demo bodies genuinely become
//                            rendered entities (graph-in-memory is NOT enough).
//
// Kept separate from the OCCT-free RobotBuilder.hpp data model: this file pulls in
// Scene/components (the render side). The edit ops themselves are NOT reimplemented
// here -- the panel invokes krs::rbuild::EditController against the graph this hosts.
// ===========================================================================
#include "RobotBuilder.hpp"

class Scene;

namespace krs::rbuild {

// A synthetic, renderable demo robot: B0-B1-B2 serial chain (DOF 2) plus an
// unjointed B3 that shares a COAXIAL bore with B2 (so define-from-features can add
// J2 -> DOF 3). Deleting J1 detaches the B2(-B3) subtree intact. Exercises
// delete / define / subtree-detach / re-mate end to end.
RobotGraph buildDemoGraph();

// Body->entity RENDER BRIDGE. For each body: spawn a renderable scene entity
// (RenderableMeshComponent + TransformComponent at the body placement + TagComponent
// + RobotSubcomponentComponent{robotId} + BRepFaceComponent for feature picking) and
// record it in RBBody.entity. After this the body is on screen and pickable; before
// it the graph is invisible.
void spawnGraphBodies(Scene& scene, RobotGraph& g, int robotId = 0);

// Body index in g whose RBBody.entity == entity (the raw entt id), or -1. Bridges a
// picked scene entity back to a graph body (for define-from-features / grab).
int bodyIndexForEntity(const RobotGraph& g, int entity);

// Mirror the graph's bodies into a (preview) scene as renderable representation
// entities WITHOUT mutating g (read-only). Returns the count spawned. Used by the
// isolated robot-only viewport to render the live graph in its own scene.
int mirrorGraphIntoScene(Scene& preview, const RobotGraph& g, int robotId = 0);

// Turntable spin: a camera position orbiting `base` at horizontal radius `dist` and
// height `elev`, at azimuth `angleRad`. The real base-axis transform the robot-only
// viewport's spin tick uses (gated headless: motion + constant orbit radius).
glm::vec3 turntableCameraPos(const glm::vec3& base, float dist, float elev, float angleRad);

// Sync each body's entity RobotSubcomponentComponent to its LIVE membership: a member
// body is tagged (kinematic chain owns it -> the viewport drag lock-out blocks it); a
// NON-member (detached / unjointed) body is untagged -> the lock-out frees it for grab.
// (isTagged == isMember, per the data model.) Call after every graph edit. This is the
// "relax the lock-out for detached subtrees" mechanism -- membership-accurate, not a
// blanket override.
void syncRobotTagsToMembership(Scene& scene, const RobotGraph& g);

// GATE BRIDGE-RENDER: every demo body becomes a valid, renderable scene entity.
// NEG-CTRLs: an un-spawned graph (entity==-1) and a fake bridge (entity set to a
// non-created id) both FAIL the rendered-entity check.
bool runRobotBuilderBridgeGate();

// GATE RBUILD-PANEL (Phase 1, defined in src/UI/RobotBuilderPanel.cpp): the panel's
// controls exist, are signal-connected, and invoke the proven ops (delete/define/
// property hot-swap); the data model changes correctly. Needs QApplication (GUI).
bool runRobotBuilderPanelGate();

// GATE VIEWPORT-DATA (Phase 2, defined in src/UI/RobotViewport.cpp): the robot-only
// viewport binds the LIVE graph (not a copy), the spin is a real base-axis transform,
// and the graph's bodies are mirrored into the view scene. Rendering is operator-confirm.
bool runRobotViewportGate();

// GATE SUBTREE-GRAB-INVOKED (Phase 3): after a mid-chain delete the detached subtree
// becomes grabbable (untagged) while still-attached bodies stay locked (tagged); re-mate
// re-locks. NEG-CTRL: a grab on a still-attached/robot-tagged body fails.
bool runRobotSubtreeGrabGate();

} // namespace krs::rbuild
