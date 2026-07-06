#pragma once
// ===========================================================================
// krs::graspq -- the OPEN GRASP-SOLVING CONTRACT + the reference parallel-jaw
// solver ("builtin_antipodal").
//
// This module is the pluggable SEAM between "something wants grasp candidates
// for an entity" and "something knows how to compute them". Everything flows
// through the plain structs below -- deliberately NO dependency on any
// effector document/asset format -- so ANY authoring source (C++, a node
// graph, Python) can implement a solver and register it at runtime via
// registerSolver(). The builtin antipodal solver is the reference
// implementation other solvers must beat, never a privileged path.
//
// Face ROLES: GraspTarget.faceRoles maps a durable BRepFace.faceKey
// (components.hpp, krs::mate::computeFaceKey) to a plain int role. The values
// are fixed by this contract:
//     0 none / 1 GripSurface / 2 KeepOut / 3 Interaction
// (include/UtilityHeaders/FaceRole.hpp did not exist when this module was
// authored -- a parallel task owns it -- so the role input here is a plain
// faceKey->int map with the values pinned by the constants below. If that
// header lands with an enum, its numeric values must match these.)
//
// Builtin role semantics (documented, honest):
//   - KeepOut (2) faces are NEVER contacted, and candidate scoring carries a
//     keepout-PROXIMITY penalty for contacts near them.
//   - When ANY face in the map carries the GripSurface (1) role, contact is
//     restricted to GripSurface faces only ("grip-only mode"). A map with
//     only KeepOut entries does NOT trigger grip-only mode -- the unmarked
//     faces stay grippable (otherwise KeepOut would be redundant).
//   - Interaction (3) faces (buttons/levers -- functional features) are not
//     clamped by the reference solver: excluded from contact, no penalty.
//   - An EMPTY map means every face is grippable.
//
// TCP frame convention (world space): tcpZ = approach direction (the gripper
// travels along +tcpZ toward the part; a "top-down" grasp has tcpZ ~ (0,0,-1)),
// tcpX = jaw-open/closing axis (the contacts lie on the tcpX line), and the
// implied tcpY = cross(tcpZ, tcpX). The builtin assumes JawSpec.approachLocal
// == +Z (the default); a non-default approachLocal is honestly noted in `why`
// and NOT compensated for.
//
// Determinism: solve() results must be reproducible for a fixed (registry,
// query, seed). The builtin uses no sampling at all -- it is deterministic
// regardless of seed; `seed` exists in the contract for solvers that sample.
// Ranking: score descending; scores within 1e-6 of each other tie-break by
// top-down approach preference (tcpZ closest to world -Z), then by faceKey.
//
// Gate: runGraspQueryGate() (env hook KRS_GRASPQ_SELFTEST, wired in the main
// loop) -- headless, pure CPU, synthetic B-Rep entities in a Scene registry,
// real NEG-CTRLs ([graspq] rows, Measure-gate style).
// ===========================================================================
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace krs::graspq {

// Face-role values (contract-fixed; see header comment re FaceRole.hpp).
inline constexpr int kRoleNone        = 0;
inline constexpr int kRoleGripSurface = 1;
inline constexpr int kRoleKeepOut     = 2;
inline constexpr int kRoleInteraction = 3;

// The effector abstraction a solver needs -- NOT any effector document. A
// caller that owns a richer effector description reduces it to this.
struct JawSpec {
    double apertureMinM = 0.0, apertureMaxM = 0.1;   // reachable jaw gap
    double jawDepthM = 0.02, jawWidthM = 0.015;      // pad footprint
    double maxForceN = 40.0;
    glm::vec3 approachLocal{0,0,1};                  // approach direction in TCP frame
};

struct GraspTarget {
    entt::entity entity = entt::null;                 // B-Rep + mesh + roles live on it
    std::unordered_map<std::uint64_t, int> faceRoles; // faceKey -> 0 none / 1 GripSurface / 2 KeepOut / 3 Interaction (empty = all faces grippable)
};

struct GraspQuery { JawSpec jaw; GraspTarget target; int maxCandidates = 32; unsigned seed = 7; };

struct GraspCandidate {
    glm::vec3 tcpPos{0}; glm::vec3 tcpZ{0,0,1}; glm::vec3 tcpX{1,0,0};  // WORLD TCP frame (Z = approach, X = jaw-open axis)
    double apertureM = 0;                             // jaw gap at contact
    std::uint64_t faceKeyA = 0, faceKeyB = 0;         // the contact pair (equal keys = one cylinder face, diameter grip)
    glm::vec3 contactA{0}, contactB{0};               // WORLD contact points
    double score = 0;                                 // higher better
    QStringList why;                                  // honest per-candidate scoring notes
};

// The open seam: any authoring source implements this signature and registers
// under a name. The registry is process-global and thread-safe.
using SolverFn = std::function<std::vector<GraspCandidate>(entt::registry&, const GraspQuery&)>;

// Register (or REPLACE -- last write wins) a solver under `name`. The builtin
// "builtin_antipodal" registers itself at static init; node/Python solvers
// register at runtime.
void registerSolver(const std::string& name, SolverFn fn);

// All registered solver names, sorted (deterministic).
std::vector<std::string> solverNames();

// Run the named solver. An UNKNOWN name returns an honest empty vector --
// never a crash, never a silent fallback to a different solver.
std::vector<GraspCandidate> solve(entt::registry& reg, const GraspQuery& q,
                                  const std::string& solver = "builtin_antipodal");

// Headless self-test (env KRS_GRASPQ_SELFTEST, wired by the main loop):
// 40 mm box -> side-face antipodal grip (aperture exact, TCP centered,
// top-down approach); KeepOut / GripSurface-only role filtering; 20 mm
// cylinder diameter grip; too-small jaw -> honest zero candidates; solver
// registry routing + unknown-name NEG-CTRL; determinism under a fixed seed.
bool runGraspQueryGate();

} // namespace krs::graspq
