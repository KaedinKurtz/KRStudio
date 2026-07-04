#pragma once
// ===========================================================================
// SkeletonClip -- an internal skeletal-animation clip + a self-contained BVH
// importer (krs::anim).
//
// WHY this exists / WHY BVH text is self-parsed (no library):
//   Retargeting robot posture onto captured human/creature motion needs a
//   hierarchy of local transforms per frame. BVH is the lingua franca of mocap
//   and is a trivial ASCII format (HIERARCHY + MOTION), so pulling in a parser
//   dependency (and its build/licence weight) is unjustified -- the whole grammar
//   fits in a few hundred lines. glTF/GLB skinned-animation is the intended
//   FUTURE path (skinning weights, cubic-spline interpolation, morph targets),
//   but that genuinely needs cgltf/tinygltf for the binary container + accessor
//   decode, so it is deliberately NOT implemented here.
//
// FRAME / UNIT CONVENTIONS (BVH's own, preserved verbatim so a clip round-trips):
//   * Right-handed; whatever axis convention the file was authored in. We do NOT
//     re-axis (no Y-up<->Z-up swap) -- a consumer that needs robot axes remaps.
//   * OFFSET / root position channels are in the file's linear units (usually cm
//     for classic BVH); we keep them raw. Rotations are DEGREES in the file and
//     converted to radians internally.
//   * Euler rotations are intrinsic and composed in the ORDER THE CHANNELS ARE
//     DECLARED for that joint (e.g. "Zrotation Yrotation Xrotation" => R = Rz*Ry*Rx,
//     applied to a body-frame vector as v_parent = Rz*Ry*Rx * v_child). Per-joint
//     order is honoured; mixed orders across joints are supported.
//
// Everything is headless + pure-CPU (Eigen only, std strings) so runSkeletonClipGate()
// gates without a window.
// ===========================================================================
#include <Eigen/Dense>
#include <string>
#include <vector>

namespace krs::anim {

// Rotation-channel order for a joint, as declared by its CHANNELS line. Stored so
// the Euler triple is composed in exactly the authored order (order matters:
// R(z,y,x) != R(x,y,z)). Values index which of the 3 rotation angles is applied
// first/second/third; the enum names the AXIS at each composition slot.
enum class Axis { X, Y, Z };

struct SkeletonJoint {
    std::string name;                 // ROOT/JOINT name; End Sites get "<parent>_End".
    int parent = -1;                  // index into SkeletonClip::joints; -1 for the ROOT.
    Eigen::Vector3d offset = Eigen::Vector3d::Zero();  // fixed translation from parent (OFFSET), file units.

    // Channel layout parsed from CHANNELS. hasPosition is true only for a root that
    // declares X/Y/Zposition channels (its origin animates); End Sites have none.
    bool hasPosition = false;         // 3 position channels present (root translation)
    int  nChannels = 0;               // total channels for this joint (0,3, or 6); End Site = 0.
    // Composition order of the 3 rotation channels (rot[0] applied first as the
    // OUTERMOST factor: R = axisRot(rotOrder[0]) * axisRot(rotOrder[1]) * axisRot(rotOrder[2])).
    Axis rotOrder[3] = { Axis::Z, Axis::Y, Axis::X };
    // Where this joint's channel values start within a MOTION frame row (column index).
    int  channelStart = 0;
    bool isEndSite = false;           // End Site leaf: no channels, offset only (a tip marker).
};

struct SkeletonClip {
    std::vector<SkeletonJoint> joints;   // topologically ordered: a joint's parent index is always < its own.
    double frameTime = 0.0;              // seconds per frame (from "Frame Time:").
    int    frameCount = 0;               // number of MOTION frames (from "Frames:").

    // Per-frame, per-joint LOCAL homogeneous transform (parent->child at that frame):
    // localXforms[frame][joint] = [ R offset(+rootPos) ; 0 0 0 1 ]. Fixed joints (End
    // Sites, or joints with no channels) carry a pure OFFSET translation, identity R.
    std::vector<std::vector<Eigen::Matrix4d>> localXforms;   // [frameCount][joints.size()]

    // Compose the hierarchy for one frame: world[j] = world[parent[j]] * local[j].
    // Because joints are topologically ordered, a single forward pass suffices.
    // Returns an empty vector if `frame` is out of range.
    std::vector<Eigen::Matrix4d> worldXforms(int frame) const;

    // World-space position of joint `jointIdx` at `frame` (translation column of its
    // world transform). Returns Vector3d::Zero() on out-of-range indices.
    Eigen::Vector3d jointWorldPos(int frame, int jointIdx) const;
};

// Parse a full BVH document (HIERARCHY + MOTION) from `text` into `out`.
// Returns true on success. On failure returns false, leaves `out` in a defined-but-
// unspecified state, and (if `err != nullptr`) writes a human-readable reason -- the
// parser NEVER throws or crashes on malformed/truncated input (it is fed untrusted files).
bool parseBVH(const std::string& text, SkeletonClip& out, std::string* err = nullptr);

// GATE: parse an embedded BVH, verify hierarchy/frame metadata, a hand-computed world
// position for a known rotation, hierarchy composition, and a malformed-input neg-ctrl.
bool runSkeletonClipGate();

} // namespace krs::anim
