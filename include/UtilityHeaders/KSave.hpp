#pragma once
// ===========================================================================
// KSAVE -- the .kscene / .krobot / .kjoint / .kstate file family (save architecture v1).
//
// COMPOSITION BY REFERENCE: a .kscene lists robot INSTANCES, each referencing a .krobot by
// RELATIVE PATH + id + contentHash; a .krobot is the master dict for one robot, referencing its
// .kjoint files (one per joint, nested in a sibling folder) the same way. Definitions are
// reusable and human-diffable JSON; hashes are the lockfile: a definition swapped on disk is
// DETECTED at load (re-parsed + reported), never silently trusted or silently ignored.
//
// GEOMETRY IS NOT INLINED: a .krobot records its CAD source (STEP path) + which deterministic
// chain builder produced its body list; load re-imports the source and re-runs that builder, then
// OVERLAYS the .kjoint files (the same strict-match-or-refuse policy as krs::persist). Demo/
// primitive robots rebuild from their recipe. This keeps every file small and the parse honest.
//
// .kstate is the SESSION SIDECAR (sim-agnostic, deterministic state only -- per-robot joint poses
// q, camera; explicitly NO PhysX/fluid buffers): best-effort by contract -- stale entries are
// skipped with a report, and a missing/corrupt .kstate never blocks a scene load. Rewritten on
// every save and at app exit, so reopening the last scene brings the program back where it closed.
//
// FAILURE POLICY (uniform): unknown format major -> refuse the FILE; malformed definition ->
// refuse that ROBOT with a report entry (scene keeps loading others); state that no longer binds
// -> skip + report. Nothing silently rebinds.
// ===========================================================================
#include <string>
#include <vector>
#include <QString>
#include <QStringList>
#include "KLut.hpp"   // krs::klut::Lut -- optional characterized-data reference on an ActuatorSpec

class Scene;

namespace krs::ksave {

// Per-robot provenance the graph mirrors would otherwise lose (buildGraphFromLiveRobot cannot know
// where a graph came from): which CAD source + deterministic builder reproduces the body list.
// ctx singleton, keyed by robotId; populated at boot / import / scene load.
struct RobotSource {
    int robotId = -1;
    std::string sourceStep;                 // STEP path ("" = no CAD source)
    int builder = 0;                        // 0 = demo recipe, 1 = named serial chain, 2 = parts spanning tree
};
struct RobotSourceRegistry {
    std::vector<RobotSource> entries;
    const RobotSource* find(int robotId) const {
        for (const auto& e : entries) if (e.robotId == robotId) return &e;
        return nullptr;
    }
    void set(int robotId, const std::string& step, int builder) {
        for (auto& e : entries) if (e.robotId == robotId) { e.sourceStep = step; e.builder = builder; return; }
        entries.push_back({ robotId, step, builder });
    }
};

// The scene document currently open (ctx singleton): Save reuses it, the exit hook rewrites the
// .kstate sidecar against it, and QSettings("ksave/lastScene") points at it for the boot reopen.
struct OpenSceneInfo { std::string kscenePath; };

struct Report {
    bool ok = false;
    int robots = 0, joints = 0;
    int lights = 0, objects = 0;            // v1.1: non-robot scene content round-tripped
    QStringList warnings;                   // hash drift, skipped robots, stale state -- shown to the user
    QString error;                          // fatal reason when !ok
};

// Write <kscenePath> + robots/<name>.krobot + robots/<name>/<joint>.kjoint + the .kstate sidecar.
// Robots without a rebuildable source (e.g. a split-off branch) are skipped with a warning.
Report saveScene(Scene& scene, const std::string& kscenePath);

// Replace the scene's robots with the document's: clears the robot registry + member entities,
// re-imports each robot's source through its recorded builder, overlays its .kjoint files, then
// applies the .kstate sidecar (q, camera) best-effort. Non-robot entities are untouched (v1).
Report loadScene(Scene& scene, const std::string& kscenePath);

// Rewrite ONLY the .kstate sidecar for an already-saved scene (the cheap exit hook).
bool saveSessionState(Scene& scene, const std::string& kscenePath);

// ---- .kactuator / .kmotor: the drive-train characterization chain --------------------------
// A .kmotor is a VENDOR-CHARACTERIZABLE definition (Kt, current limits, speed -- real Maxon/etc.
// datasheet values); a .kactuator composes motor + gearbox (ratio, efficiency) into a reusable
// drive unit. resolveActuator() walks kactuator -> kmotor and DERIVES the joint-side limits:
//   effort   = Kt * maxCurrent * ratio * efficiency        [N*m]
//   velocity = motorMaxSpeed / ratio                        [rad/s]
// Same reference semantics as the rest of the family (relative ref + id + contentHash, versioned,
// refuse unknown majors). Intrinsic motor data lives in .kmotor; contextual data (which gearbox,
// which joint) lives at the reference site -- the boundary that keeps reuse working.
struct ActuatorSpec {
    bool ok = false;
    QString error;
    std::string motorName, actuatorName;
    double kt = 0, maxCurrent = 0, motorMaxSpeed = 0;   // from .kmotor
    double ratio = 1, efficiency = 1;                   // from .kactuator
    double jointEffort = 0, jointVelocity = 0;          // derived joint-side limits
    // Optional CHARACTERIZED data: a .klut of REAL measured behavior (e.g. torque-speed, Kt-vs-temp)
    // referenced from the .kactuator (or .kmotor). When present, a consumer can sample the empirical
    // curve/surface instead of (or alongside) the closed-form Kt*I*ratio derivation above.
    bool hasCharacterized = false;
    std::string characterizedQuantity;                  // what the LUT encodes (e.g. "torqueSpeed")
    krs::klut::Lut characterizedLut;                    // the loaded LUT (query via sample1D/sample2D)
};
ActuatorSpec resolveActuator(const std::string& kactuatorPath);

// Headless gate (KRS_KSAVE_SELFTEST): save -> fresh scene -> load round-trip (files, joints,
// limits, names, connectors, q, DOF); tampered .kjoint is detected + honored; NEG-CTRLs: missing
// .krobot / corrupt .kscene refuse cleanly; stale q (wrong length) is skipped without crashing.
// Plus the actuator chain: derived effort/velocity match the closed form; refusals clean.
bool runKSaveGate();

// Headless gate (KRS_SCENESAVE_SELFTEST): save architecture v1.1 -- a ROBOT-FREE scene's
// environment/skybox settings (EnvironmentSettings ctx), fog/background (SceneProperties), all
// lights (type/color/intensity/cone/size/range/enabled + transform + emissive material), and loose
// primitive objects (transform incl. rotation + full MaterialComponent) survive a save -> fresh
// scene -> load round-trip; NEG-CTRL: a recipe-less mesh-asset object is skipped with a warning,
// never silently fabricated.
bool runSceneObjectsGate();

} // namespace krs::ksave
