#pragma once
// ===========================================================================
// GltfAnim -- glTF/GLB skeletal-animation import into the existing SkeletonClip
// (krs::anim). Companion to the self-contained BVH importer in SkeletonClip.*;
// this fills the SAME clip representation (per-frame, per-joint LOCAL parent->
// child homogeneous transforms) from a glTF animation.
//
// WHY the split (library-agnostic core + thin Assimp glue):
//   BVH is a trivial ASCII grammar so it is self-parsed. glTF/GLB is a binary
//   container (accessors, buffer views, sparse data, cubic-spline samplers) and
//   is genuinely worth a real importer -- Assimp is already a linked dependency
//   (see MeshUtils.hpp), so we let it decode the container. But Assimp's aiScene
//   is awkward to gate headlessly (needs a real file / a decode). So the actual
//   MATH -- topological ordering, TRS keyframe sampling (LERP position/scale,
//   SLERP rotation, clamp outside the key range), and composition into a
//   SkeletonClip -- lives in a pure, file-free, Assimp-free core that the gate
//   exercises directly with SYNTHETIC channels. The Assimp glue is a thin
//   adapter: walk the node hierarchy for parent names + rest transforms, convert
//   aiNodeAnim key arrays into TrsChannel, then call the core.
//
// FRAME / UNIT CONVENTIONS:
//   * We do NOT re-axis (no Y-up<->Z-up swap): a consumer that needs robot axes
//     remaps, exactly as the BVH path does. glTF is authored Y-up/right-handed.
//   * Key TIMES are seconds. glTF stores seconds natively; Assimp stores ticks,
//     so the glue divides by ticksPerSecond (defaulting 25 when the file says 0).
//   * Rotations are unit QUATERNIONS (glTF-native), SLERP-interpolated. Positions
//     and scales are LINEAR-interpolated. The local matrix is composed T*R*S.
//   * A channel with no position keys uses its bind-pose translation (staticOffset,
//     the parent->child rest offset). No rotation keys => identity; no scale => ones.
//
// Everything in the core is headless + pure-CPU (Eigen only, std strings) so
// runGltfAnimGate() gates without a window, without Assimp, and without a file.
// ===========================================================================
#include "SkeletonClip.hpp"   // the target type (krs::anim::SkeletonClip); do NOT redefine it.

#include <Eigen/Dense>
#include <Eigen/Geometry>     // Quaterniond / slerp (rotation keyframe interpolation)
#include <string>
#include <vector>

namespace krs::anim {

// ---------------------------------------------------------------------------
// TrsChannel -- one animated node's sampled keyframes, glTF/mocap-agnostic.
//
// This is the LIBRARY-AGNOSTIC input to the clip builder: it carries a node's
// name, its parent's name, a rest (bind) translation, and three OPTIONAL key
// streams (position / rotation / scale) all indexed by the SAME `tKeys` time
// vector. An empty key stream means "use the rest value" (identity rotation,
// unit scale, or the staticOffset translation). Times are seconds, ascending.
// ---------------------------------------------------------------------------
struct TrsChannel {
    std::string name;                        // node/joint name (unique within a clip)
    std::string parent;                      // parent node name; "" (empty) marks a root
    Eigen::Vector3d staticOffset =           // rest translation, used when there are no position keys
        Eigen::Vector3d::Zero();
    std::vector<double> tKeys;               // key times (seconds), ascending; may be empty
    std::vector<Eigen::Vector3d>  pos;       // position keys aligned to tKeys (empty => staticOffset)
    std::vector<Eigen::Quaterniond> rot;     // rotation keys aligned to tKeys (empty => identity)
    std::vector<Eigen::Vector3d>  scl;       // scale keys aligned to tKeys (empty => ones)
};

// Sample one channel's TRS at absolute time `t` and compose the local parent->
// child homogeneous matrix M = T * R * S:
//   * position/scale: piecewise-LINEAR between the bracketing keys,
//   * rotation: SLERP between the bracketing rotation keys,
//   * CLAMP: `t` before the first key yields the first key; after the last, the last.
// An empty stream contributes its rest value (staticOffset / identity / ones).
// Never throws: a channel with mismatched key-array lengths falls back to rest.
Eigen::Matrix4d sampleTrsLocal(const TrsChannel& c, double t);

// Build a SkeletonClip from a set of channels:
//   (a) TOPOLOGICALLY order the channels parent-before-child by walking the parent
//       links (stable). A missing-parent reference or a parent CYCLE is rejected
//       (returns false, sets *err) rather than producing a silent orphan or hanging.
//   (b) Choose a fixed sample rate: `sampleRate` if > 0, else a default 30 fps.
//       Duration = the max last key time across all channels (0 if no keys).
//       frameCount = round(duration * rate) + 1, frameTime = 1 / rate.
//   (c) Sample every channel at every frame time into clip.localXforms[f][j].
// On success returns true and fully populates `out`. On failure returns false,
// leaves `out` defined-but-unspecified, and (if err) writes a human-readable reason.
bool buildClipFromChannels(const std::vector<TrsChannel>& channels, double sampleRate,
                           SkeletonClip& out, std::string* err = nullptr);

// ---------------------------------------------------------------------------
// Assimp glue (implemented in GltfAnim.cpp; declared here). Loads `path` via
// Assimp, converts animation `animIndex` into TrsChannels, then calls the core.
//   * sampleRate <= 0 => the core's default (30 fps).
//   * Defensive: never throws; returns false + *err on any missing/malformed input
//     (no file, no animations, bad index, empty animation, unresolved parents...).
// aiQuaternion is (w,x,y,z); we convert to Eigen::Quaterniond(w,x,y,z) accordingly.
// ---------------------------------------------------------------------------
bool importGltfAnimation(const std::string& path, SkeletonClip& out, int animIndex = 0,
                         double sampleRate = 0.0, std::string* err = nullptr);

// GATE: exercise the library-agnostic core with synthetic channels (root translate +
// child "elbow" rotating 0->90deg about Z over 1s). Verifies topological ordering,
// frame metadata, TRS sampling endpoints, mid-frame SLERP (~45deg), clamping, and
// world composition; plus NEG-CTRLs for a missing parent and a 2-node cycle.
// Requires NO file and NO Assimp call. Prints "ALL PASS"/"FAILURES PRESENT".
bool runGltfAnimGate();

} // namespace krs::anim
