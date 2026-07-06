#pragma once
// ===========================================================================
// KEE -- the .kee END-EFFECTOR DOCUMENT (krs::kee), the data layer of the
// end-effector stack.
//
// An end-effector IS a small robot: its own bodies + joints are a
// krs::rbuild::RobotGraph (a parallel gripper = base + 2 jaws, 1 prismatic
// drive + a mimic follower). What makes it an EFFECTOR rather than a robot is
// the contract layered on top:
//   * interfaceMateId -- the MateConnector (durable, body-LOCAL, stable id --
//     components.hpp) ON THE BASE BODY that mates to the robot flange: the
//     document's "native interfacing mate". Mating a .kee to an arm =
//     resolveMate(flange connector, interface connector), nothing world-anchored.
//   * tcps -- >=1 named TCP frames in BASE-BODY-LOCAL coordinates (tcps[0] is
//     the default); the IK target frames this tool offers.
//   * faceRolesByBody -- per-body durable-faceKey -> FaceRoleEntry designations
//     (GripSurface / KeepOut / Interaction; see FaceRole.hpp).
//   * actuation -- the jaw drive DOF by NAME (drive-by-name, joint-primary
//     model), aperture endpoints in joint space, optional mimic follower.
//
// FILE FORMAT (kee/1): single JSON file, versioned ("format":"kee/1", unknown
// major refused), embedded contentHash (SHA1 over the canonical serialization
// with the hash field removed -- a swapped/hand-edited file is DETECTED at load
// and reported, never silently trusted: the ksave lockfile discipline adapted
// to a self-contained document). The graph codec MIRRORS KSave.cpp's .krobot/
// .kjoint path field-for-field (embedded joints are literal kjoint/1 objects),
// extended with per-body placements because a .kee has no CAD-rebuild source.
//
// VALIDATION (the honest half): validate() checks a loaded/authored doc is
// USABLE and reports what is missing as per-item pass/fail lines for UI
// display. Load parses; validate judges -- a structurally sound file with a
// dangling drive-joint name LOADS fine and FAILS validation.
//
// Gate: runKeeGate() (env hook KRS_KEE_SELFTEST wired in main) -- synthetic
// parallel gripper -> save -> load -> field-exact round-trip; validate all-ok;
// NEG-CTRLs (missing interface mate / drive-name typo / aperture outside
// limits fail VALIDATION; corrupt file + wrong major refused at LOAD; tampered
// content hash detected).
// ===========================================================================
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <QString>
#include <QStringList>

#include "RobotBuilder.hpp"   // krs::rbuild::RobotGraph / RBJoint / RBBody
#include "components.hpp"     // MateConnector (durable body-LOCAL frame, stable id)
#include "FaceRole.hpp"       // FaceRoleEntry / faceRoleName / faceRoleFromName

namespace krs::kee {

// A named tool-center-point frame, BASE-BODY-LOCAL (rides the effector's base
// body; world frame derives through the base placement at mate time).
struct TcpFrame {
    std::string name;
    glm::vec3 pos{ 0.0f };
    glm::vec3 z{ 0.0f, 0.0f, 1.0f };   // approach axis
    glm::vec3 x{ 1.0f, 0.0f, 0.0f };   // roll reference
};

// The drive contract: WHICH graph joint actuates the jaws (by NAME -- names are
// first-class joint identity, RBJoint::name), the aperture endpoints in that
// joint's space, and an optional mimic follower (parallel gripper: ratio -1).
struct Actuation {
    std::string driveJointName;            // the jaw drive DOF ("" = passive tool)
    double openQ = 0.0, closedQ = 0.0;     // aperture endpoints in joint space
    double mimicRatio = -1.0;              // follower jaw q = ratio * drive q
    std::string mimicJointName;            // "" = no mimic
    double maxGripForceN = 40.0;
};

// The complete .kee document.
struct EffectorDoc {
    QString name;
    krs::rbuild::RobotGraph graph;         // the effector's own bodies + joints
    std::uint32_t interfaceMateId = 0;     // MateConnector id on the BASE body (flange mate)
    std::vector<TcpFrame> tcps;            // >=1; tcps[0] = default TCP
    // body index -> durable faceKey -> semantic designation (grip/keepout/interaction).
    std::map<int, std::unordered_map<std::uint64_t, FaceRoleEntry>> faceRolesByBody;
    // authored connectors per body (including the interface one on the base).
    std::map<int, std::vector<MateConnector>> connectorsByBody;
    Actuation actuation;
    QString notes;
};

struct Report {
    bool ok = false;
    QString error;                         // fatal reason when !ok
    QStringList warnings;                  // hash drift etc. -- shown to the user
};

// Write / read the versioned JSON document (see the header block for format).
// saveKee also runs validate() and surfaces failing items as WARNINGS (a save
// never blocks on validation -- authoring an incomplete doc is legitimate).
Report saveKee(const EffectorDoc& doc, const std::string& path);
Report loadKee(const std::string& path, EffectorDoc& out);

// Per-item pass/fail lines for UI display. Checks:
//   graph non-empty + valid base; interface mate resolves to a connector on a
//   base-component body; driveJointName resolves to a non-Fixed graph joint;
//   openQ/closedQ within that joint's limits; >=1 well-formed TCP; at least
//   one GripSurface role (WARNING-ONLY: the item stays ok with a warning text);
//   mimic joint resolves (and differs from the drive) when named.
struct ValidationItem { bool ok; QString what; };
std::vector<ValidationItem> validate(const EffectorDoc& doc);
inline bool validationOk(const std::vector<ValidationItem>& items) {
    for (const auto& it : items) if (!it.ok) return false;
    return true;
}

// Headless gate (KRS_KEE_SELFTEST; hook wired by main). ALL PASS required.
bool runKeeGate();

} // namespace krs::kee
