// GltfAnim.cpp -- library-agnostic TRS-keyframe core + a thin Assimp glTF/GLB glue,
// producing the existing krs::anim::SkeletonClip (see GltfAnim.hpp for the contract).
//
// The CORE (sampleTrsLocal / buildClipFromChannels) is pure Eigen + std and is what
// runGltfAnimGate() exercises with synthetic channels -- no file, no Assimp. The glue
// (importGltfAnimation) uses Assimp only to DECODE the container; all the animation
// math routes back through the core so it is covered by the same gate.
#include "UtilityHeaders/GltfAnim.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace krs::anim {

// ------------------------------------------------------------------ small helpers
namespace {

// Set the error string (if provided) and return false, so a caller can
// `return fail(err, "...")` in one line without ever entering a throwing path.
bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

// Homogeneous compose T * R * S from a translation, a (unit) rotation, and a
// per-axis scale. R and S are 3x3; T is the translation column. Matches the
// SkeletonClip convention: block(0,0)=rotation*scale, block(0,3)=translation.
Eigen::Matrix4d composeTRS(const Eigen::Vector3d& t, const Eigen::Quaterniond& r,
                           const Eigen::Vector3d& s) {
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    const Eigen::Matrix3d R = r.normalized().toRotationMatrix();
    // R * diag(s): scale the columns (S applied on the RIGHT, i.e. in child space).
    Eigen::Matrix3d RS;
    RS.col(0) = R.col(0) * s.x();
    RS.col(1) = R.col(1) * s.y();
    RS.col(2) = R.col(2) * s.z();
    M.block<3,3>(0,0) = RS;
    M.block<3,1>(0,3) = t;
    return M;
}

// Find the bracketing key indices [lo,hi] and the in-segment fraction u in [0,1]
// for absolute time `t` over an ASCENDING key-time vector. Clamps outside the
// range (before first key => lo=hi=0,u=0; after last => lo=hi=last,u=0). Assumes
// !keys.empty(). Uses a binary search so long key streams stay cheap.
void bracket(const std::vector<double>& keys, double t, size_t& lo, size_t& hi, double& u) {
    const size_t n = keys.size();
    if (t <= keys.front())     { lo = hi = 0;     u = 0.0; return; }
    if (t >= keys.back())      { lo = hi = n - 1; u = 0.0; return; }
    // first key strictly greater than t -> that is `hi`, its predecessor is `lo`.
    const auto it = std::upper_bound(keys.begin(), keys.end(), t);
    hi = size_t(it - keys.begin());
    lo = hi - 1;
    const double span = keys[hi] - keys[lo];
    u = (span > 0.0) ? (t - keys[lo]) / span : 0.0;   // guard duplicate/zero-span keys
}

} // namespace

// ------------------------------------------------------------------ sampleTrsLocal

Eigen::Matrix4d sampleTrsLocal(const TrsChannel& c, double t) {
    // Rest defaults, used whenever a stream is empty (or malformed / mis-sized).
    Eigen::Vector3d    T = c.staticOffset;
    Eigen::Quaterniond R = Eigen::Quaterniond::Identity();
    Eigen::Vector3d    S = Eigen::Vector3d::Ones();

    const size_t nk = c.tKeys.size();
    if (nk == 0) return composeTRS(T, R, S);   // static bind pose

    // Position (LINEAR). Only sample if the stream is present and aligned to tKeys.
    if (c.pos.size() == nk) {
        size_t lo, hi; double u;
        bracket(c.tKeys, t, lo, hi, u);
        T = c.pos[lo] + u * (c.pos[hi] - c.pos[lo]);
    }
    // Rotation (SLERP). Eigen's Quaterniond::slerp does the shortest-arc blend.
    if (c.rot.size() == nk) {
        size_t lo, hi; double u;
        bracket(c.tKeys, t, lo, hi, u);
        R = c.rot[lo].normalized().slerp(u, c.rot[hi].normalized());
    }
    // Scale (LINEAR).
    if (c.scl.size() == nk) {
        size_t lo, hi; double u;
        bracket(c.tKeys, t, lo, hi, u);
        S = c.scl[lo] + u * (c.scl[hi] - c.scl[lo]);
    }
    return composeTRS(T, R, S);
}

// ------------------------------------------------------------------ buildClipFromChannels

bool buildClipFromChannels(const std::vector<TrsChannel>& channels, double sampleRate,
                           SkeletonClip& out, std::string* err) {
    out = SkeletonClip{};      // defined starting state on every entry
    if (err) err->clear();
    if (channels.empty()) return fail(err, "no channels");

    const size_t N = channels.size();

    // Name -> original-index map, rejecting duplicate names (they make the parent
    // links ambiguous). This is also the lookup used to resolve parent references.
    std::unordered_map<std::string, int> nameToIdx;
    nameToIdx.reserve(N * 2);
    for (size_t i = 0; i < N; ++i) {
        if (channels[i].name.empty())
            return fail(err, "channel " + std::to_string(i) + " has an empty name");
        if (!nameToIdx.emplace(channels[i].name, int(i)).second)
            return fail(err, "duplicate channel name '" + channels[i].name + "'");
    }

    // Resolve each channel's parent to an ORIGINAL index (-1 for a root). A named
    // parent that does not exist is a hard error (a silent orphan would corrupt FK).
    std::vector<int> parentOf(N, -1);
    for (size_t i = 0; i < N; ++i) {
        const std::string& p = channels[i].parent;
        if (p.empty()) { parentOf[i] = -1; continue; }
        const auto it = nameToIdx.find(p);
        if (it == nameToIdx.end())
            return fail(err, "channel '" + channels[i].name + "' names a missing parent '" + p + "'");
        if (it->second == int(i))
            return fail(err, "channel '" + channels[i].name + "' is its own parent");
        parentOf[i] = it->second;
    }

    // ---- Topological order: parent BEFORE child. Kahn-style over depth ----
    // depth[i] = number of ancestors; a node with all ancestors resolvable gets a
    // finite depth. If any node never resolves, there is a CYCLE (or a chain into a
    // cycle) -> reject. We compute depth by memoised walk with an on-stack guard.
    std::vector<int> depth(N, -1);             // -1 = unresolved
    std::vector<char> onStack(N, 0);           // cycle detection during a walk
    // Resolve depth of `i`, following parent links; returns false on a cycle.
    // Iterative to avoid deep recursion on long chains.
    auto resolveDepth = [&](int start) -> bool {
        std::vector<int> stack;
        int cur = start;
        // Walk up until we hit a node with known depth (or a root), pushing the path.
        while (cur != -1 && depth[cur] < 0) {
            if (onStack[cur]) return false;    // revisited a node still on THIS walk -> cycle
            onStack[cur] = 1;
            stack.push_back(cur);
            cur = parentOf[cur];
        }
        // `cur` is either -1 (root parent) or a node with a known depth. Unwind,
        // assigning increasing depths down the recorded path.
        int base = (cur == -1) ? -1 : depth[cur];
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            depth[*it] = ++base;
            onStack[*it] = 0;
        }
        return true;
    };
    for (size_t i = 0; i < N; ++i) {
        if (depth[i] < 0 && !resolveDepth(int(i)))
            return fail(err, "parent cycle detected (channel '" + channels[i].name + "')");
    }

    // Stable topological order: sort original indices by (depth, original index).
    // Equal-depth ties keep input order so the layout is deterministic.
    std::vector<int> order(N);
    for (size_t i = 0; i < N; ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return depth[a] < depth[b]; });

    // remap[originalIdx] = position in the topologically-ordered joints vector.
    std::vector<int> remap(N, -1);
    for (size_t newIdx = 0; newIdx < N; ++newIdx) remap[order[newIdx]] = int(newIdx);

    // Build the joint list in topological order; every parent index is now < own.
    out.joints.resize(N);
    for (size_t newIdx = 0; newIdx < N; ++newIdx) {
        const int orig = order[newIdx];
        SkeletonJoint& jn = out.joints[newIdx];
        jn.name   = channels[orig].name;
        jn.parent = (parentOf[orig] < 0) ? -1 : remap[parentOf[orig]];
        jn.offset = channels[orig].staticOffset;
        jn.hasPosition = !channels[orig].pos.empty();
        jn.nChannels   = 0;    // glTF has no BVH-style channel layout; left at 0.
        jn.channelStart = 0;
        jn.isEndSite   = false;
        // Defensive: topological ordering must have produced parent < own index.
        if (jn.parent >= int(newIdx))
            return fail(err, "internal ordering error at '" + jn.name + "'");
    }

    // ---- Sample rate + duration -> frame grid ----
    const double rate = (sampleRate > 0.0) ? sampleRate : 30.0;   // default 30 fps
    double duration = 0.0;
    for (const auto& c : channels)
        if (!c.tKeys.empty()) duration = std::max(duration, c.tKeys.back());
    // frameCount = round(duration*rate)+1 (always at least 1 frame at t=0).
    const int frameCount = int(std::lround(duration * rate)) + 1;
    out.frameCount = frameCount;
    out.frameTime  = 1.0 / rate;

    // ---- Sample every channel at every frame time into localXforms[f][j] ----
    out.localXforms.assign(size_t(frameCount),
                           std::vector<Eigen::Matrix4d>(N, Eigen::Matrix4d::Identity()));
    for (int f = 0; f < frameCount; ++f) {
        const double t = double(f) / rate;               // absolute time of this frame
        for (size_t newIdx = 0; newIdx < N; ++newIdx) {
            const int orig = order[newIdx];
            out.localXforms[size_t(f)][newIdx] = sampleTrsLocal(channels[orig], t);
        }
    }
    return true;
}

// ------------------------------------------------------------------ Assimp glue

namespace {

// Depth-first walk of the Assimp node hierarchy, recording each node's parent name
// and its rest (bind) translation (from decomposing aiNode::mTransformation). Used
// by the glue to give every animated channel a parent + a staticOffset.
struct NodeInfo {
    std::string parent;              // parent node name ("" for the scene root)
    Eigen::Vector3d restTranslation; // rest local translation (bind pose)
};

void collectNodes(const aiNode* node, const std::string& parentName,
                  std::unordered_map<std::string, NodeInfo>& out) {
    if (!node) return;
    aiVector3D t, s; aiQuaternion r;
    node->mTransformation.Decompose(s, r, t);   // never throws; fills defaults on identity
    NodeInfo info;
    info.parent = parentName;
    info.restTranslation = Eigen::Vector3d(double(t.x), double(t.y), double(t.z));
    out[std::string(node->mName.C_Str())] = info;
    for (unsigned c = 0; c < node->mNumChildren; ++c)
        collectNodes(node->mChildren[c], std::string(node->mName.C_Str()), out);
}

} // namespace

bool importGltfAnimation(const std::string& path, SkeletonClip& out, int animIndex,
                         double sampleRate, std::string* err) {
    out = SkeletonClip{};
    if (err) err->clear();

    Assimp::Importer imp;
    const aiScene* s = imp.ReadFile(path, aiProcess_Triangulate);
    if (!s)                       return fail(err, std::string("Assimp failed to read '") + path + "': " + imp.GetErrorString());
    if (s->mNumAnimations == 0)   return fail(err, "no animations in '" + path + "'");
    if (animIndex < 0 || animIndex >= int(s->mNumAnimations))
        return fail(err, "animation index " + std::to_string(animIndex) + " out of range (have " +
                         std::to_string(s->mNumAnimations) + ")");

    const aiAnimation* anim = s->mAnimations[size_t(animIndex)];
    if (!anim || anim->mNumChannels == 0) return fail(err, "animation has no node channels");

    // Ticks -> seconds. glTF via Assimp usually reports ticksPerSecond=1000 (ms) or
    // the real fps; default to 25 when unset (Assimp's documented fallback).
    double tps = anim->mTicksPerSecond;
    if (tps <= 0.0) tps = 25.0;

    // Rest transforms + parent links from the node hierarchy.
    std::unordered_map<std::string, NodeInfo> nodes;
    if (s->mRootNode) collectNodes(s->mRootNode, std::string(), nodes);

    // Convert each aiNodeAnim into a TrsChannel. A glTF channel animates a NODE, so
    // its name identifies the node; we pull the parent + rest translation from `nodes`.
    std::vector<TrsChannel> channels;
    channels.reserve(anim->mNumChannels);
    for (unsigned ci = 0; ci < anim->mNumChannels; ++ci) {
        const aiNodeAnim* na = anim->mChannels[ci];
        if (!na) continue;
        TrsChannel ch;
        ch.name = std::string(na->mNodeName.C_Str());
        const auto it = nodes.find(ch.name);
        if (it != nodes.end()) {
            ch.parent        = it->second.parent;
            ch.staticOffset  = it->second.restTranslation;
        }
        // Position keys (aiVectorKey: mTime in ticks, mValue xyz).
        ch.pos.reserve(na->mNumPositionKeys);
        for (unsigned k = 0; k < na->mNumPositionKeys; ++k) {
            const aiVectorKey& vk = na->mPositionKeys[k];
            ch.pos.emplace_back(double(vk.mValue.x), double(vk.mValue.y), double(vk.mValue.z));
        }
        // Rotation keys (aiQuatKey: aiQuaternion is (w,x,y,z)).
        ch.rot.reserve(na->mNumRotationKeys);
        for (unsigned k = 0; k < na->mNumRotationKeys; ++k) {
            const aiQuatKey& qk = na->mRotationKeys[k];
            ch.rot.emplace_back(double(qk.mValue.w), double(qk.mValue.x),
                                double(qk.mValue.y), double(qk.mValue.z));
        }
        // Scale keys.
        ch.scl.reserve(na->mNumScalingKeys);
        for (unsigned k = 0; k < na->mNumScalingKeys; ++k) {
            const aiVectorKey& vk = na->mScalingKeys[k];
            ch.scl.emplace_back(double(vk.mValue.x), double(vk.mValue.y), double(vk.mValue.z));
        }
        // Build the unified time base. glTF stores independent time samplers per
        // TRS channel, but Assimp already keys them together in the common case; we
        // take the LONGEST key-time array as the channel's tKeys and (defensively)
        // only keep TRS streams whose length matches it -- others fall back to rest.
        auto pickTimes = [&](unsigned n, const auto* keys) {
            std::vector<double> ts; ts.reserve(n);
            for (unsigned k = 0; k < n; ++k) ts.push_back(double(keys[k].mTime) / tps);
            return ts;
        };
        std::vector<double> tp = pickTimes(na->mNumPositionKeys, na->mPositionKeys);
        std::vector<double> tr = pickTimes(na->mNumRotationKeys, na->mRotationKeys);
        std::vector<double> tsc = pickTimes(na->mNumScalingKeys, na->mScalingKeys);
        // Choose the longest as the master timeline.
        const std::vector<double>* master = &tp;
        if (tr.size()  > master->size()) master = &tr;
        if (tsc.size() > master->size()) master = &tsc;
        ch.tKeys = *master;
        // Drop any stream that does not align with the master timeline so the core's
        // "size == tKeys.size()" alignment check keeps it (or falls back to rest).
        if (ch.pos.size() != ch.tKeys.size()) ch.pos.clear();
        if (ch.rot.size() != ch.tKeys.size()) ch.rot.clear();
        if (ch.scl.size() != ch.tKeys.size()) ch.scl.clear();
        channels.push_back(std::move(ch));
    }

    if (channels.empty()) return fail(err, "no usable node channels after conversion");

    // Assimp channels reference nodes by name but do NOT guarantee that every named
    // parent is itself an animated channel. buildClipFromChannels rejects a parent
    // that is not among the channels, so re-root any channel whose parent is not in
    // the set to a root ("") -- this keeps a valid, if flatter, hierarchy rather than
    // failing on a perfectly good animation that only keys a subset of the skeleton.
    std::unordered_map<std::string, int> present;
    for (size_t i = 0; i < channels.size(); ++i) present.emplace(channels[i].name, int(i));
    for (auto& ch : channels)
        if (!ch.parent.empty() && present.find(ch.parent) == present.end())
            ch.parent.clear();

    return buildClipFromChannels(channels, sampleRate, out, err);
}

// ------------------------------------------------------------------ GATE

bool runGltfAnimGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[gltfanim] GATE GLTF-ANIM -- TRS keyframe core: topo order + LERP/SLERP sample + clamp + FK compose\n");
    bool pass = true;

    const double d2r = 3.14159265358979323846 / 180.0;

    // ---- Synthetic channels ----------------------------------------------------
    // ROOT "hips": TRANSLATES from origin to (0,2,0) over 1s (rotation stays identity,
    //   so world FK reduces to a clean translation we can hand-check).
    // CHILD "elbow" (parent hips): ROTATES 0 -> 90deg about Z over 1s, at a fixed
    //   rest offset (1,0,0) from the root. No position keys => uses staticOffset.
    // We deliberately declare CHILD BEFORE PARENT in the input vector to force the
    // topological ordering to actually reorder them (a no-op sort would fail below).
    TrsChannel elbow;
    elbow.name = "elbow";
    elbow.parent = "hips";
    elbow.staticOffset = Eigen::Vector3d(1.0, 0.0, 0.0);
    elbow.tKeys = { 0.0, 1.0 };
    elbow.rot = {
        Eigen::Quaterniond::Identity(),                                   // 0deg
        Eigen::Quaterniond(Eigen::AngleAxisd(90.0 * d2r, Eigen::Vector3d::UnitZ())) // 90deg about Z
    };

    TrsChannel hips;
    hips.name = "hips";
    hips.parent = "";                          // root
    hips.staticOffset = Eigen::Vector3d::Zero();
    hips.tKeys = { 0.0, 1.0 };
    hips.pos = { Eigen::Vector3d(0.0, 0.0, 0.0), Eigen::Vector3d(0.0, 2.0, 0.0) };
    // rotation left empty => identity at every frame (clean translation-only root).

    std::vector<TrsChannel> chans = { elbow, hips };   // child first on purpose

    const double rate = 30.0;
    SkeletonClip clip; std::string err;
    const bool built = buildClipFromChannels(chans, rate, clip, &err);
    printf("[gltfanim]   build ok=%s (%s)  %s\n", built ? "yes" : "NO",
           built ? "no error" : err.c_str(), built ? "PASS" : "FAIL");
    pass &= built;
    if (!built) { printf("[gltfanim] FAILURES PRESENT\n"); std::fflush(stdout); return false; }

    // (1) Topological order: hips (root) must come BEFORE elbow, and every parent
    //     index is strictly less than its own index.
    int hipsIdx = -1, elbowIdx = -1;
    for (size_t j = 0; j < clip.joints.size(); ++j) {
        if (clip.joints[j].name == "hips")  hipsIdx = int(j);
        if (clip.joints[j].name == "elbow") elbowIdx = int(j);
    }
    bool topoOk = (clip.joints.size() == 2) && (hipsIdx == 0) && (elbowIdx == 1)
                  && clip.joints[0].parent == -1 && clip.joints[1].parent == 0;
    for (size_t j = 0; j < clip.joints.size(); ++j)
        if (clip.joints[j].parent >= int(j)) topoOk = false;   // parent-before-child invariant
    printf("[gltfanim]   topo hips@%d(parent -1) elbow@%d(parent 0)=%s  %s\n",
           hipsIdx, elbowIdx, topoOk ? "ok" : "NO", topoOk ? "PASS" : "FAIL");
    pass &= topoOk;

    // (2) Frame metadata: duration=1s, rate=30 => frameCount=round(30)+1=31, frameTime=1/30.
    const bool metaOk = (clip.frameCount == 31)
                        && (std::fabs(clip.frameTime - 1.0 / 30.0) < 1e-12);
    printf("[gltfanim]   meta frames=%d (exp 31) frameTime=%.6f (exp %.6f)  %s\n",
           clip.frameCount, clip.frameTime, 1.0 / 30.0, metaOk ? "PASS" : "FAIL");
    pass &= metaOk;

    // (3) sampleTrsLocal endpoints for the CHILD elbow:
    //     t=0   -> identity rotation: Rx = (1,0,0) stays (1,0,0).
    //     t=1.0 -> 90deg about Z: R*(1,0,0) = (0,1,0)  [unit-X -> unit-Y].
    const Eigen::Vector3d ux(1.0, 0.0, 0.0);
    auto rotPart = [](const Eigen::Matrix4d& M) { return M.block<3,3>(0,0); };
    const Eigen::Matrix4d e0 = sampleTrsLocal(elbow, 0.0);
    const Eigen::Matrix4d e1 = sampleTrsLocal(elbow, 1.0);
    const Eigen::Vector3d r0 = rotPart(e0) * ux;   // expect (1,0,0)
    const Eigen::Vector3d r1 = rotPart(e1) * ux;   // expect (0,1,0)
    const bool endpt0 = (r0 - Eigen::Vector3d(1.0, 0.0, 0.0)).norm() < 1e-9;
    const bool endpt1 = (r1 - Eigen::Vector3d(0.0, 1.0, 0.0)).norm() < 1e-9;
    printf("[gltfanim]   elbow t=0 R*ux=(%.4f,%.4f,%.4f) exp(1,0,0)  %s\n", r0.x(), r0.y(), r0.z(), endpt0 ? "PASS" : "FAIL");
    printf("[gltfanim]   elbow t=1 R*ux=(%.4f,%.4f,%.4f) exp(0,1,0)  %s\n", r1.x(), r1.y(), r1.z(), endpt1 ? "PASS" : "FAIL");
    pass &= endpt0 && endpt1;

    // (4) SLERP mid-point proves INTERPOLATION (not nearest-key snap): at t=0.5 the
    //     rotation is 45deg about Z, so R*ux = (cos45, sin45, 0) ~ (0.7071,0.7071,0)
    //     -- distinctly NOT 0deg (1,0,0) and NOT 90deg (0,1,0).
    const Eigen::Matrix4d eMid = sampleTrsLocal(elbow, 0.5);
    const Eigen::Vector3d rMid = rotPart(eMid) * ux;
    const double s45 = std::sin(45.0 * d2r);       // = cos45 = 0.70710678...
    const bool midOk = (rMid - Eigen::Vector3d(s45, s45, 0.0)).norm() < 1e-6
                       && (rMid - Eigen::Vector3d(1.0, 0.0, 0.0)).norm() > 0.2   // not the 0deg key
                       && (rMid - Eigen::Vector3d(0.0, 1.0, 0.0)).norm() > 0.2;  // not the 90deg key
    printf("[gltfanim]   elbow t=0.5 SLERP R*ux=(%.4f,%.4f,%.4f) exp(%.4f,%.4f,0) & !=endpoints  %s\n",
           rMid.x(), rMid.y(), rMid.z(), s45, s45, midOk ? "PASS" : "FAIL");
    pass &= midOk;

    // (5) CLAMP: sampling BEFORE the first key equals the first key; AFTER the last
    //     equals the last. Check both position (root) and rotation (elbow).
    const Eigen::Matrix4d before = sampleTrsLocal(elbow, -5.0);  // before first -> 0deg
    const Eigen::Matrix4d after  = sampleTrsLocal(elbow,  9.0);  // after last   -> 90deg
    const bool clampRot = (rotPart(before) * ux - Eigen::Vector3d(1.0, 0.0, 0.0)).norm() < 1e-9
                          && (rotPart(after) * ux - Eigen::Vector3d(0.0, 1.0, 0.0)).norm() < 1e-9;
    const Eigen::Vector3d hipBefore = sampleTrsLocal(hips, -3.0).block<3,1>(0,3); // -> (0,0,0)
    const Eigen::Vector3d hipAfter  = sampleTrsLocal(hips,  7.0).block<3,1>(0,3); // -> (0,2,0)
    const bool clampPos = (hipBefore - Eigen::Vector3d(0.0, 0.0, 0.0)).norm() < 1e-9
                          && (hipAfter - Eigen::Vector3d(0.0, 2.0, 0.0)).norm() < 1e-9;
    const bool clampOk = clampRot && clampPos;
    printf("[gltfanim]   clamp rot(before=0deg,after=90deg)=%s pos(before=(0,0,0),after=(0,2,0))=%s  %s\n",
           clampRot ? "ok" : "NO", clampPos ? "ok" : "NO", clampOk ? "PASS" : "FAIL");
    pass &= clampOk;

    // (6) WORLD composition via SkeletonClip::worldXforms. Root rotation is identity,
    //     so world[elbow].pos = rootPos(frame) + staticOffset(1,0,0).
    //     Frame 0  (t=0):   rootPos=(0,0,0)   -> elbow world = (1,0,0).
    //     Last frame(t=1):  rootPos=(0,2,0)   -> elbow world = (1,2,0).
    const int fLast = clip.frameCount - 1;
    const Eigen::Vector3d elbowW0    = clip.jointWorldPos(0, elbowIdx);
    const Eigen::Vector3d elbowWLast = clip.jointWorldPos(fLast, elbowIdx);
    const bool worldOk = (elbowW0 - Eigen::Vector3d(1.0, 0.0, 0.0)).norm() < 1e-9
                         && (elbowWLast - Eigen::Vector3d(1.0, 2.0, 0.0)).norm() < 1e-9;
    printf("[gltfanim]   world elbow f0=(%.4f,%.4f,%.4f) exp(1,0,0)  fLast=(%.4f,%.4f,%.4f) exp(1,2,0)  %s\n",
           elbowW0.x(), elbowW0.y(), elbowW0.z(),
           elbowWLast.x(), elbowWLast.y(), elbowWLast.z(), worldOk ? "PASS" : "FAIL");
    pass &= worldOk;

    // (7) POSITIVE-CONTROL that the mid frame world elbow really moved from f0 (not a
    //     stub returning the rest pose every frame). Mid frame index 15 = t=0.5:
    //     rootPos=(0,1,0), elbow local rot=45deg does NOT change its own translation
    //     (offset lives in the root's frame, root rot=identity) so elbow world=(1,1,0).
    const Eigen::Vector3d elbowWMid = clip.jointWorldPos(15, elbowIdx);
    const bool midWorldOk = (elbowWMid - Eigen::Vector3d(1.0, 1.0, 0.0)).norm() < 1e-6
                            && (elbowWMid - elbowW0).norm() > 0.5;   // genuinely different from f0
    printf("[gltfanim]   world elbow fMid(15)=(%.4f,%.4f,%.4f) exp(1,1,0) & != f0  %s\n",
           elbowWMid.x(), elbowWMid.y(), elbowWMid.z(), midWorldOk ? "PASS" : "FAIL");
    pass &= midWorldOk;

    // (8) NEG-CTRL A: a channel whose parent name does NOT exist must be REJECTED
    //     (returned false), NOT silently orphaned. If this vacuously "passed" (built
    //     a clip anyway), the missing-parent guard would be dead code -> VACUOUS!
    {
        TrsChannel orphan;
        orphan.name = "hand";
        orphan.parent = "no_such_bone";                 // dangling parent reference
        orphan.tKeys = { 0.0 };
        orphan.rot = { Eigen::Quaterniond::Identity() };
        std::vector<TrsChannel> bad = { hips, orphan };
        SkeletonClip badClip; std::string be;
        const bool r = buildClipFromChannels(bad, rate, badClip, &be);
        const bool neg = (!r && !be.empty());
        printf("[gltfanim]   NEG-CTRL missing-parent REJECTS build=%s (%s)  %s\n",
               (!r) ? "yes" : "NO", be.empty() ? "(no err!)" : be.c_str(),
               neg ? "PASS" : (r ? "VACUOUS!" : "FAIL"));
        pass &= neg;
    }

    // (9) NEG-CTRL B: a 2-node CYCLE (A parent B, B parent A) must be REJECTED and
    //     must NOT hang (the depth resolver has an on-stack cycle guard).
    {
        TrsChannel A; A.name = "A"; A.parent = "B"; A.tKeys = { 0.0 }; A.rot = { Eigen::Quaterniond::Identity() };
        TrsChannel B; B.name = "B"; B.parent = "A"; B.tKeys = { 0.0 }; B.rot = { Eigen::Quaterniond::Identity() };
        std::vector<TrsChannel> cyc = { A, B };
        SkeletonClip cycClip; std::string ce;
        const bool r = buildClipFromChannels(cyc, rate, cycClip, &ce);
        const bool neg = (!r && !ce.empty());
        printf("[gltfanim]   NEG-CTRL 2-cycle REJECTS build=%s (%s)  %s\n",
               (!r) ? "yes" : "NO", ce.empty() ? "(no err!)" : ce.c_str(),
               neg ? "PASS" : (r ? "VACUOUS!" : "FAIL"));
        pass &= neg;
    }

    // (10) NEG-CTRL C (positive-side vacuity guard): the VALID synthetic build above
    //      must have produced a NON-trivial, MOVING clip. If elbow's f0 and fLast
    //      world positions were identical the whole gate could pass on a stub that
    //      ignores animation -- assert they differ so the sampling is load-bearing.
    const bool moved = (elbowW0 - elbowWLast).norm() > 0.5;
    printf("[gltfanim]   NEG-CTRL (anti-stub) clip actually animates (f0 != fLast)=%s  %s\n",
           moved ? "yes" : "NO", moved ? "PASS" : "VACUOUS!");
    pass &= moved;

    printf("[gltfanim] %s\n", pass ? "ALL PASS (topo order + LERP/SLERP TRS sampling + clamp + FK compose verified)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::anim
