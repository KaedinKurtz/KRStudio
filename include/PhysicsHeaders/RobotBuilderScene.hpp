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

// GATE BRIDGE-RENDER: every demo body becomes a valid, renderable scene entity.
// NEG-CTRLs: an un-spawned graph (entity==-1) and a fake bridge (entity set to a
// non-created id) both FAIL the rendered-entity check.
bool runRobotBuilderBridgeGate();

} // namespace krs::rbuild
