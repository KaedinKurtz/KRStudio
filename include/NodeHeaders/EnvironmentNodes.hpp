#pragma once
// ===========================================================================
// EnvironmentNodes -- node-graph control of the SCENE ENVIRONMENT (krs::envnode).
//
// Two scene-writing node types so a graph can drive lighting the same way it
// drives joints/fields:
//   * "environment_settings" (Environment) -- writes the EnvironmentSettings ctx
//     (sun intensity/color/direction, IBL intensity, exposure EV, skybox on/off)
//     and SceneProperties (fog). It sets EnvironmentSettings::nodeDriven so the app
//     pushes the ctx into the live RenderingSystem each eval pass.
//   * "light_control" (Light) -- writes a target light entity's LightComponent
//     (color/intensity/range/enable) + its emissive material so the bulb glows the
//     commanded colour. The target is a wired entt::entity input, or a light named
//     by the "light" param (matched against TagComponent). The renderer reads
//     LightComponents from the registry every frame, so a Light node is live with
//     no extra push.
//
// Both compute()s are headless (Scene-only, no RenderingSystem/GL), so the gate
// runs without a window.
// ===========================================================================

namespace krs::envnode {

// Headless gate (KRS_ENVNODE_SELFTEST): an Environment node writes the sun/IBL/exposure/skybox ctx +
// fog and flags nodeDriven; a Light node drives a wired light entity AND a by-name light (color/
// intensity/range/enable + emissive glow). NEG-CTRLs: a Light node with no target touches nothing
// (no phantom LightComponent); an Environment node's write is non-vacuous (ctx differs from default).
bool runEnvironmentNodesGate();

} // namespace krs::envnode
