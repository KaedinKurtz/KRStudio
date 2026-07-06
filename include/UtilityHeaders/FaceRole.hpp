#pragma once
// ===========================================================================
// FACE SEMANTIC ROLES -- what a B-Rep face IS FOR, keyed by the durable faceKey.
//
// The per-faceKey map follows the FaceMaterialComponent precedent (components.hpp:
// a map on the body entity, render/consumers derive), but keys by BRepFace.faceKey
// -- the geometry-INVARIANT topological id (see computeFaceKey) -- NOT the volatile
// face INDEX, so a designation survives re-import / re-tessellation / array
// reordering exactly like a MateConnector's sourceFaceKey anchor.
//
// Built-in roles cover the end-effector data layer (.kee):
//   GripSurface -- a face that is allowed/intended to contact the workpiece
//                  (carries friction + a max normal-force budget for grasp math),
//   KeepOut     -- a face nothing may approach/contact (cameras, cables, seals),
//   Interaction -- a functional face that touches the world but is not a grip pad
//                  (probe tip, suction rim, tool-changer seat).
// The `tag` string is the USER-EXTENSIBLE refinement channel ("vacuum-pad",
// "camera-window", ...) -- a role stays machine-readable while tags stay open.
//
// Header-only, no Qt/entt deps; serialized by the .kee codec (krs::kee, KEE.cpp).
// ===========================================================================
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

enum class FaceRole { None = 0, GripSurface = 1, KeepOut = 2, Interaction = 3 };

struct FaceRoleEntry {
    FaceRole    role = FaceRole::None;
    std::string tag;                    // user-extensible refinement ("" = none)
    float       friction        = 0.8f;   // Coulomb mu for grasp math (GripSurface)
    float       maxNormalForceN = 50.0f;  // per-face normal-force budget [N]
};

// ECS component (per body entity): durable faceKey -> semantic designation.
struct FaceRoleComponent {
    std::unordered_map<std::uint64_t, FaceRoleEntry> byFaceKey;
};

// Canonical names (the .kee serialization tokens). Unknown/None round-trip as "None".
inline const char* faceRoleName(FaceRole r) {
    switch (r) {
        case FaceRole::GripSurface: return "GripSurface";
        case FaceRole::KeepOut:     return "KeepOut";
        case FaceRole::Interaction: return "Interaction";
        default:                    return "None";
    }
}
inline FaceRole faceRoleFromName(const char* s) {
    if (!s) return FaceRole::None;
    if (std::strcmp(s, "GripSurface") == 0) return FaceRole::GripSurface;
    if (std::strcmp(s, "KeepOut")     == 0) return FaceRole::KeepOut;
    if (std::strcmp(s, "Interaction") == 0) return FaceRole::Interaction;
    return FaceRole::None;
}
