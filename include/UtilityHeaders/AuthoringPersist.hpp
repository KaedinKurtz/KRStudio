#pragma once
// ===========================================================================
// AUTHORING PERSISTENCE -- the robot-authoring state survives an app restart.
//
// Before this module NOTHING the user authored survived exit: every manual joint
// definition, corrected axis, limit edit, rename, re-type, and every minted mate
// connector was re-inferred from the STEP at next boot (RobotBuilderPanel::
// shutdownAndSave was literally "/* nothing persisted */").
//
// V1 SCOPE (honest): persists ONE robot's authoring document -- the boot robot
// (robotId 0) -- as versioned JSON: the edited RobotGraph joint list (identity,
// type, limits, frames, provenance) + the per-body MateConnectors + the mate
// graph. Bodies are addressed by (graph body index, solid slot) which is stable
// because the boot re-imports the SAME STEP through the SAME deterministic
// chain builder; the document carries the source name + body count and the
// overlay REFUSES to apply on any mismatch (falls back to fresh inference --
// never a silent mis-bind). Full multi-robot / BodyId-keyed persistence is the
// planned follow-up once a durable BodyId exists.
// ===========================================================================
#include <string>
#include <entt/entt.hpp>

namespace krs::rbuild { struct RobotGraph; }

namespace krs::persist {

// Where the boot robot's authoring document lives + which CAD source it belongs to.
// Emplaced in registry ctx at boot so the save trigger (graphChanged) knows both.
struct AuthoringDocInfo {
    std::string source;     // CAD asset identity (file BASENAME -- machine-agnostic)
    std::string filePath;   // full path of the JSON document
};

// Machine-local default document path: <application dir>/robot_authoring.json.
std::string defaultAuthoringPath();

// Save graph `g` + the mate connectors/constraints its bodies' entities carry to `filePath`
// as a versioned JSON document tagged with `source`. Returns false on I/O failure.
bool saveAuthoring(entt::registry& reg, const krs::rbuild::RobotGraph& g,
                   const std::string& source, const std::string& filePath);

// Overlay a previously saved document onto a FRESHLY BUILT graph: replaces the joint list with
// the persisted (user-edited) joints, restores MateConnectorComponents onto the graph bodies'
// entities, and rebuilds the ctx MateGraphComponent. Returns false -- with `g` untouched -- on
// missing/corrupt file, version mismatch, source mismatch, or body-count mismatch.
bool loadAuthoringOverlay(entt::registry& reg, krs::rbuild::RobotGraph& g,
                          const std::string& source, const std::string& filePath);

// Headless self-test (KRS_PERSIST_SELFTEST): edit -> save -> fresh scene -> overlay -> everything
// round-trips; NEG-CTRLs: wrong source / wrong body count / truncated file refuse cleanly.
bool runPersistGate();

} // namespace krs::persist
