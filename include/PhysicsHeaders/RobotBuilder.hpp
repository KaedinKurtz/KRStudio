#pragma once
// ===========================================================================
// ROBOT BUILDER -- auto-parse a STEP assembly into an EDITABLE kinematic chain.
//
// Phase 0 recon (runParseReconGate) established EMPIRICALLY against the real
// FANUC-430 STEP that OCCT's STEPCAFControl_Reader recovers the part TREE +
// per-part PLACEMENTS + names + colors, but NO parametric mates (STEP drops
// them). So auto-parse uses the recovered bodies+placements and INFERS joints
// from inter-part geometry (the gated krs::joint::deriveRevoluteFromBores).
//
// This header is the OCCT-FREE data model + gates (joint inference, the editable
// RobotGraph, membership/ownership, tree/subtree-detach). The OCCT assembly read
// lives in RobotBuilder.cpp. The model operates on analytic BRepFace data, so its
// gates run headless on synthetic parts (mirrors SelectionService / RobotModel).
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <Eigen/Dense>
#include <string>
#include <vector>
#include <set>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "components.hpp"        // BRepFace
#include "JointTooling.hpp"      // krs::joint::deriveRevoluteFromBores (gated JOINT-FROM-FEATURE)
#include "RobotModel.hpp"        // krs::robot::Robot / krs::dyn

namespace krs::rbuild {

enum class JType { Revolute = 0, Prismatic = 1, Fixed = 2 };
enum class Prov  { Inferred = 0, Manual = 1 };

// ---- a part recovered from the STEP assembly -------------------------------
// faces are analytic B-Rep params in the part's LOCAL frame; placement maps
// local -> world (assembly). placement * localPoint = worldPoint.
struct ParsedPart {
    std::string name;
    Eigen::Matrix4d placement = Eigen::Matrix4d::Identity();
    std::vector<BRepFace> faces;
    int entity = -1;                 // primary scene entity once spawned (-1 = not spawned)
    // Additional scene entities rigidly belonging to this body's link. A CAD link owns
    // SEVERAL solids/shells (e.g. a FANUC link = a casting + brackets + bolts); they all
    // move together. instantiateFromGraph parents + drives {entity} U extraEntities.
    std::vector<int> extraEntities;
    glm::vec3 visSize{ 0.12f, 0.12f, 0.12f };  // per-axis size of the placeholder box mesh
                                               // (lets the demo build proper-looking links)
};
using RBBody = ParsedPart;

// ---- engineering limits for a joint (consumed by clampDof via toRobot) -------
// enabled=false models a CONTINUOUS revolute (URDF 'continuous': no position limit),
// avoiding a new JType enum value (which would break the int serialization). effort /
// velocity default to 0 == unspecified (URDF treats as +inf).
struct JointLimits {
    double lower   = -3.14159265;   // rad (revolute) / m (prismatic)
    double upper   =  3.14159265;
    double effort  = 0.0;
    double velocity= 0.0;
    bool   enabled = true;          // false => continuous (no position clamp)
};

// ---- a joint connecting two bodies, frame DERIVED from feature geometry -----
// MATE-CONNECTOR MODEL: the joint owns a PERSISTED coordinate FRAME (axisPos origin +
// axisDir=Z + refDir=X), NOT a live reference to a B-Rep face. Bore-picking only AUTHORS
// the frame at definition time; afterwards the frame is an independent, editable datum
// (placeable/flippable -- Phase 5), so re-importing/editing the CAD never silently
// rebinds or breaks the joint (the topological-naming fragility of "2 live faces").
struct RBJoint {
    int parent = -1;
    int child  = -1;
    JType type = JType::Revolute;
    glm::vec3 axisPos{ 0.0f };               // frame origin: a point on the joint axis (world m)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f };   // frame Z: unit joint axis
    glm::vec3 refDir { 1.0f, 0.0f, 0.0f };   // frame X: secondary axis, kept perpendicular to Z
    Prov prov = Prov::Inferred;
    bool ambiguous = false;                  // flagged low-confidence -> NOT a committed joint
    double residual = 0.0;                   // coaxiality residual (diagnostic)
    JointLimits limits;                      // position/effort/velocity bounds (Revolute/Prismatic)

    // ---- IDENTITY (joint-primary model) -----------------------------------------
    // A joint is a first-class, addressable entity. `id` = engine-internal STABLE handle
    // (assigned once by RobotGraph::addJoint, never reused, PRESERVED across split/merge/
    // rebuild) used for persistent selection + by-id lookup. `nodeId` = user-facing CAN-style
    // numeric address (hardware mapping). `name` = user-facing semantic label. Both nodeId and
    // name surface in the joint picker; id stays internal. 0 / -1 / "" mean "unassigned" -> auto.
    std::uint64_t id     = 0;                 // internal stable handle (0 = unassigned)
    int           nodeId = -1;                // CAN-style numeric node id (-1 = unassigned)
    std::string   name;                       // semantic name ("" = unassigned)

    // Re-derive refDir as a UNIT vector perpendicular to axisDir (a stable frame basis).
    // Call after setting axisDir from inference/snap; picks any perpendicular if refDir
    // is parallel to the axis.
    void orthonormalizeFrame() {
        glm::vec3 z = (glm::length(axisDir) > 1e-9f) ? glm::normalize(axisDir) : glm::vec3(0, 0, 1);
        glm::vec3 x = refDir - glm::dot(refDir, z) * z;
        if (glm::length(x) < 1e-6f) {                    // refDir parallel to z -> any perpendicular
            const glm::vec3 seed = (std::abs(z.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            x = seed - glm::dot(seed, z) * z;
        }
        axisDir = z; refDir = glm::normalize(x);
    }
    // Reverse the joint axis, keeping the frame right-handed (Y = Z x X unchanged).
    void flipAxis() { axisDir = -axisDir; refDir = -refDir; }
    // The full orthonormal mate frame basis as columns {X, Y, Z}.
    glm::mat3 frameBasis() const {
        const glm::vec3 z = glm::normalize(axisDir);
        glm::vec3 x = refDir - glm::dot(refDir, z) * z;
        x = (glm::length(x) > 1e-6f) ? glm::normalize(x)
                                     : glm::normalize((std::abs(z.x) < 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0))
                                                      - glm::dot((std::abs(z.x) < 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0)), z) * z);
        const glm::vec3 y = glm::cross(z, x);
        return glm::mat3(x, y, z);
    }
};

// ---- transform an analytic BRepFace from local to world by a placement ------
inline BRepFace faceToWorld(const BRepFace& f, const Eigen::Matrix4d& M)
{
    auto xp = [&](const glm::vec3& p) {
        Eigen::Vector4d v(p.x, p.y, p.z, 1.0); Eigen::Vector4d r = M * v;
        return glm::vec3(float(r.x()), float(r.y()), float(r.z()));
    };
    auto xd = [&](const glm::vec3& d) {
        Eigen::Vector3d v(d.x, d.y, d.z); Eigen::Vector3d r = M.block<3,3>(0,0) * v;
        glm::vec3 g(float(r.x()), float(r.y()), float(r.z()));
        const float L = glm::length(g); return (L > 1e-12f) ? g / L : d;
    };
    BRepFace w = f;
    w.axisPos = xp(f.axisPos);
    w.axisDir = xd(f.axisDir);
    w.normal  = xd(f.normal);
    return w;
}

// ===========================================================================
// JOINT INFERENCE -- honest best-guess from inter-part geometry.
//
// A revolute candidate exists between two parts iff they share a COAXIAL
// cylindrical interface (matching radius, common axis line) -- derived by the
// gated krs::joint::deriveRevoluteFromBores (which REJECTS non-parallel / offset
// pairs). A part pair with no coaxial cylinder is left UNJOINTED (not faked).
// Ambiguity (a near-tol or radius-mismatched best match) is FLAGGED, not
// silently committed. Returns one best candidate per pair that has one.
// ===========================================================================
inline bool inferRevolute(const RBBody& A, const RBBody& B, RBJoint& out,
                          double coaxTol = 1e-4, double radTol = 1e-3)
{
    double best = 1e30; bool found = false; RBJoint cand;
    for (const auto& fa : A.faces) {
        if (fa.type != 1) continue;                       // cylinder only
        const BRepFace wa = faceToWorld(fa, A.placement);
        for (const auto& fb : B.faces) {
            if (fb.type != 1) continue;
            if (std::abs(double(fa.radius) - double(fb.radius)) > radTol) continue;  // radii must match
            const BRepFace wb = faceToWorld(fb, B.placement);
            krs::joint::JointFrame jf; double ang = 0, off = 0;
            if (!krs::joint::deriveRevoluteFromBores(wa, wb, jf, coaxTol, &ang, &off)) continue;
            const double res = std::max(ang, off);
            if (res < best) {
                best = res; found = true;
                cand.parent = -1; cand.child = -1; cand.type = JType::Revolute;
                cand.axisPos = jf.axisPos; cand.axisDir = jf.axisDir;
                cand.orthonormalizeFrame();                     // persist a full mate frame
                cand.prov = Prov::Inferred; cand.residual = res;
            }
        }
    }
    if (!found) return false;
    out = cand;
    return true;
}

// ===========================================================================
// THE EDITABLE ROBOT GRAPH -- a TREE rooted at `base`. Membership (robot-
// subcomponent ownership) = transitive closure from base through COMMITTED
// (non-ambiguous) joints. Deleting a joint detaches the downstream subtree
// INTACT (its internal joints survive); re-mating re-derives membership.
// ===========================================================================
struct RobotGraph {
    std::vector<RBBody> bodies;
    std::vector<RBJoint> joints;
    int base = 0;
    // Which first-class robot this authoring graph represents. The boot FANUC is 0;
    // the Builder's "Load Demo" graph is 1, etc. syncRobotTagsToMembership() stamps
    // member bodies with THIS id so two graphs coexist without clobbering each other.
    int robotId = 0;

    // Joint-identity allocators (see RBJoint::id/nodeId/name). addJoint() issues a fresh
    // id/nodeId/name to any joint that arrives unassigned, and PRESERVES (never regenerates)
    // the identity of a joint that already carries one -- so split/merge/rebuild keep stable
    // ids. Counters are kept ahead of any preserved id to guarantee uniqueness within a graph.
    std::uint64_t nextJointId = 1;           // 0 is the "unassigned" sentinel on RBJoint::id
    int           nextNodeId  = 0;

    // bodies reachable from an ARBITRARY root through committed joints (BFS over the undirected
    // graph). Base-independent -- the building block for re-rooting and connected-components.
    std::set<int> membersFrom(int root) const {
        std::set<int> seen;
        if (root < 0 || root >= int(bodies.size())) return seen;
        std::vector<int> stack{ root }; seen.insert(root);
        while (!stack.empty()) {
            const int b = stack.back(); stack.pop_back();
            for (const auto& j : joints) {
                if (j.ambiguous) continue;
                int o = -1;
                if (j.parent == b) o = j.child; else if (j.child == b) o = j.parent;
                if (o >= 0 && o < int(bodies.size()) && !seen.count(o)) { seen.insert(o); stack.push_back(o); }
            }
        }
        return seen;
    }
    // bodies reachable from base (the kinematic-tree membership).
    std::set<int> members() const { return membersFrom(base); }
    bool isMember(int body) const { const auto m = members(); return m.count(body) != 0; }

    // ---- CONNECTED COMPONENTS (the joint-primary model: a "robot" is a DERIVED component) -------
    // Partition the bodies into connected components over all committed joints (base-independent).
    // Each returned list is sorted ascending (std::set order). Cutting a joint that disconnects a
    // subtree simply yields TWO components here -- nothing is deleted; both stay drivable.
    std::vector<std::vector<int>> connectedComponents() const {
        std::vector<std::vector<int>> comps;
        std::vector<char> visited(bodies.size(), 0);
        for (int b = 0; b < int(bodies.size()); ++b) {
            if (visited[b]) continue;
            const std::set<int> m = membersFrom(b);
            std::vector<int> comp(m.begin(), m.end());
            for (int x : comp) if (x >= 0 && x < int(visited.size())) visited[x] = 1;
            comps.push_back(std::move(comp));
        }
        return comps;
    }
    // Deterministically pick a root body for a component: keep the existing base if it belongs to
    // the component, else the lowest body index (stable). (Phase 4 re-root passes an explicit body.)
    int chooseBase(const std::vector<int>& comp) const {
        if (comp.empty()) return -1;
        for (int b : comp) if (b == base) return base;
        int best = comp.front();
        for (int b : comp) best = std::min(best, b);
        return best;
    }

    // robot DOF = committed, member, non-Fixed joints (both endpoints reachable from base).
    int dof() const {
        const auto m = members();
        int d = 0;
        for (const auto& j : joints) {
            if (j.ambiguous || j.type == JType::Fixed) continue;
            if (m.count(j.parent) && m.count(j.child)) ++d;
        }
        return d;
    }

    // the robot-subcomponent tag tracks LIVE membership; a tagged body is owned by
    // the kinematic chain and is NOT free-movable (the single-owner lock-out).
    bool isTagged(int body) const { return isMember(body); }
    bool freeMoveAllowed(int body) const { return !isMember(body); }

    // Append a joint, minting identity for any unassigned field and preserving an existing one.
    // Rejects a self-joint (parent==child) -- a joint must connect two DISTINCT bodies. Returns -1.
    int addJoint(RBJoint j) {
        if (j.parent == j.child) return -1;   // no self-joint (e.g. two coaxial bores on one collapsed link)
        if (j.id == 0) j.id = nextJointId++;
        else           nextJointId = std::max(nextJointId, j.id + 1);
        if (j.nodeId < 0) j.nodeId = nextNodeId++;
        else              nextNodeId = std::max(nextNodeId, j.nodeId + 1);
        if (j.name.empty()) j.name = "J" + std::to_string(j.nodeId);
        joints.push_back(std::move(j));
        return int(joints.size()) - 1;
    }

    // delete a joint by index. The downstream subtree's INTERNAL joints stay in the
    // list (intact, still articulated) -- membership is recomputed lazily by members().
    void deleteJoint(int idx) {
        if (idx >= 0 && idx < int(joints.size())) joints.erase(joints.begin() + idx);
    }

    // find a committed joint connecting two bodies (either direction), or -1.
    int jointBetween(int a, int b) const {
        for (int i = 0; i < int(joints.size()); ++i) {
            const auto& j = joints[i];
            if (j.ambiguous) continue;
            if ((j.parent == a && j.child == b) || (j.parent == b && j.child == a)) return i;
        }
        return -1;
    }

    // Chain ordering: DFS from base over committed (non-ambiguous) MEMBER joints.
    // order[0] == base; for k>=1, order[k] becomes chain body (k-1). parentJoint[b]/
    // parentBody[b] record how body b attaches. toRobot() AND the live-instance
    // factory (instantiateFromGraph) both consume this, so the chain DOF order, the
    // krs::robot::Robot joints, and the link->entity map are GUARANTEED to agree.
    struct ChainOrder { std::vector<int> order; std::vector<int> parentJoint; std::vector<int> parentBody; };
    // Spanning tree by DFS from an ARBITRARY root (base-independent) -- the re-rootable derivation.
    ChainOrder chainOrderFrom(int root) const {
        ChainOrder co;
        co.parentJoint.assign(bodies.size(), -1);
        co.parentBody.assign(bodies.size(), -1);
        if (root < 0 || root >= int(bodies.size())) return co;
        const auto m = membersFrom(root);
        std::set<int> seen{ root }; std::vector<int> stack{ root };
        co.order.push_back(root);
        while (!stack.empty()) {
            const int b = stack.back(); stack.pop_back();
            for (int ji = 0; ji < int(joints.size()); ++ji) {
                const auto& j = joints[ji];
                if (j.ambiguous) continue;
                int o = -1; if (j.parent == b) o = j.child; else if (j.child == b) o = j.parent;
                if (o >= 0 && m.count(o) && !seen.count(o)) {
                    seen.insert(o); stack.push_back(o); co.order.push_back(o);
                    co.parentJoint[o] = ji; co.parentBody[o] = b;
                }
            }
        }
        return co;
    }
    ChainOrder chainOrder() const { return chainOrderFrom(base); }
    // Body indices in chain order (order[0]=base; order[k>=1] = chain body k-1).
    std::vector<int> chainBodyOrder() const { return chainOrder().order; }

    // ---- MATE-SNAP (CAD concentric mate) ----------------------------------------------
    // Body B and every transitive descendant (so a whole sub-assembly moves as one rigid
    // unit). Requires the A-B joint to already exist so chainOrder reaches B. order is
    // base-rooted with parent before child, so one forward pass marks the closure.
    std::vector<int> subtreeOf(int B) const {
        const ChainOrder co = chainOrder();
        std::vector<char> in(bodies.size(), 0);
        if (B >= 0 && B < int(bodies.size())) in[B] = 1;
        for (int b : co.order) {
            if (b == base) continue;
            const int pb = co.parentBody[b];
            if (pb >= 0 && pb < int(in.size()) && in[pb]) in[b] = 1;   // child of an in-set body
        }
        std::vector<int> out;
        for (int b = 0; b < int(in.size()); ++b) if (in[b]) out.push_back(b);
        return out;
    }

    // Rigid world transform that MATES child bore frame jB onto parent bore frame jA: rotate jB's
    // axis parallel to jA's (shortest arc) and translate so jB's mate POINT (axisPos -- pass the
    // selected bore RIM) COINCIDES with jA's. Result: the two selected faces meet at jA's point (the
    // joint interface / origin) with their axes concentric. Left-multiplied onto the child subtree's
    // world placements.
    static Eigen::Matrix4d mateTransformConcentric(const RBJoint& jA, const RBJoint& jB) {
        RBJoint a = jA, b = jB; a.orthonormalizeFrame(); b.orthonormalizeFrame();
        const Eigen::Vector3d dA(a.axisDir.x, a.axisDir.y, a.axisDir.z);
        const Eigen::Vector3d dB(b.axisDir.x, b.axisDir.y, b.axisDir.z);
        const Eigen::Vector3d pA(a.axisPos.x, a.axisPos.y, a.axisPos.z);
        const Eigen::Vector3d pB(b.axisPos.x, b.axisPos.y, b.axisPos.z);
        Eigen::Vector3d v = dB.cross(dA); const double s = v.norm(), c = dB.dot(dA);
        Eigen::Matrix3d R;
        if (s < 1e-9) {
            R = (c > 0.0) ? Eigen::Matrix3d::Identity()
                          : Eigen::Matrix3d(Eigen::AngleAxisd(3.14159265358979323846, dB.unitOrthogonal()));
        } else {
            v /= s; Eigen::Matrix3d K;
            K << 0, -v.z(), v.y(),  v.z(), 0, -v.x(),  -v.y(), v.x(), 0;
            R = Eigen::Matrix3d::Identity() + s * K + (1.0 - c) * K * K;   // Rodrigues
        }
        const Eigen::Vector3d RpB = R * pB;
        Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
        T.block<3, 3>(0, 0) = R; T.block<3, 1>(0, 3) = pA - RpB;           // coincide the mate points
        return T;
    }

    // ---- SPLIT / MERGE (the multi-robot model: delete a joint -> two robots; mate -> one) ----------
    // Cut joint `jointIdx`: the joint's CHILD subtree becomes `outBranch` (re-rooted at that child);
    // the rest stays in `outBase`. Bodies keep their entities; joints are re-indexed to each side. The
    // cut joint is dropped (re-mating restores it). Internal joints of each side survive. (Out-params,
    // not a by-value struct, since RobotGraph is incomplete inside its own definition.) Returns false
    // on a bad index or an attempt to split off the base itself.
    bool splitAtJoint(int jointIdx, RobotGraph& outBase, RobotGraph& outBranch,
                      int* branchRootOldIdx = nullptr) const {
        outBase = RobotGraph{}; outBranch = RobotGraph{};
        if (jointIdx < 0 || jointIdx >= int(joints.size())) return false;
        const int childB = joints[jointIdx].child;
        if (childB < 0 || childB >= int(bodies.size())) return false;
        const std::vector<int> sub = subtreeOf(childB);          // body indices in the branch
        std::vector<char> inBranch(bodies.size(), 0);
        for (int b : sub) if (b >= 0 && b < int(bodies.size())) inBranch[b] = 1;
        if (base >= 0 && base < int(inBranch.size()) && inBranch[base]) return false;  // never split off the base
        std::vector<int> toBase(bodies.size(), -1), toBranch(bodies.size(), -1);
        for (int b = 0; b < int(bodies.size()); ++b) {
            if (inBranch[b]) { toBranch[b] = int(outBranch.bodies.size()); outBranch.bodies.push_back(bodies[b]); }
            else             { toBase[b]   = int(outBase.bodies.size());   outBase.bodies.push_back(bodies[b]); }
        }
        outBase.base   = (base >= 0 && toBase[base] >= 0) ? toBase[base] : 0;
        outBranch.base = (toBranch[childB] >= 0) ? toBranch[childB] : 0;
        outBase.robotId = robotId;
        for (int j = 0; j < int(joints.size()); ++j) {
            if (j == jointIdx) continue;
            const RBJoint& J = joints[j];
            if (J.parent < 0 || J.child < 0 || J.parent >= int(bodies.size()) || J.child >= int(bodies.size())) continue;
            const bool pIn = inBranch[J.parent], cIn = inBranch[J.child];
            if (pIn && cIn)        { RBJoint nj = J; nj.parent = toBranch[J.parent]; nj.child = toBranch[J.child]; outBranch.addJoint(nj); }
            else if (!pIn && !cIn) { RBJoint nj = J; nj.parent = toBase[J.parent];   nj.child = toBase[J.child];   outBase.addJoint(nj); }
            // a tree has exactly one parent edge per body, so the only cross edge is jointIdx (dropped).
        }
        if (branchRootOldIdx) *branchRootOldIdx = childB;
        return true;
    }

    // Merge `branch` INTO this graph: append its bodies (re-indexed) + internal joints, then connect
    // branch.base (as the child) to `parentBodyInThis` via `crossJoint`. The inverse of splitAtJoint.
    // Returns the index of the new cross joint, or -1.
    int mergeFrom(const RobotGraph& branch, int parentBodyInThis, RBJoint crossJoint) {
        if (parentBodyInThis < 0 || parentBodyInThis >= int(bodies.size())) return -1;
        const int offset = int(bodies.size());
        for (const auto& b : branch.bodies) bodies.push_back(b);
        for (const auto& j : branch.joints) { RBJoint nj = j; nj.parent += offset; nj.child += offset; addJoint(nj); }
        crossJoint.parent = parentBodyInThis;
        crossJoint.child  = offset + (branch.base >= 0 ? branch.base : 0);
        addJoint(crossJoint);
        return int(joints.size()) - 1;
    }

    // serial-order the member joints by DFS from base (parent = the already-visited
    // endpoint) -> a krs::robot::Robot whose FK at q=0 reproduces the parsed placements.
    krs::robot::Robot toRobot() const {
        krs::robot::Robot r;
        r.name = "parsed_robot";
        const ChainOrder co = chainOrder();
        const std::vector<int>& order = co.order;
        if (order.empty()) return r;
        r.basePlacement = bodies[base].placement;
        r.nLinks = int(order.size());
        // position of each body in chain order; chain-DOF index of order[k] (k>=1) is k-1.
        // Used to set Joint.treeParent so a BRANCHED graph lowers with the correct parent
        // (base -> treeParent -1 == serial fallback).
        std::vector<int> orderPos(bodies.size(), -1);
        for (size_t k = 0; k < order.size(); ++k)
            if (order[k] >= 0 && order[k] < int(bodies.size())) orderPos[order[k]] = int(k);
        for (size_t k = 1; k < order.size(); ++k) {           // skip base (k=0)
            const int body = order[k];
            const int pj = co.parentJoint[body];
            const int pb = co.parentBody[body];
            const Eigen::Matrix4d rel = bodies[pb].placement.inverse() * bodies[body].placement;
            krs::robot::Joint rj;
            // Lower ALL three authoring types (was a Fixed?Fixed:Revolute ternary that
            // silently coerced Prismatic -> Revolute).
            rj.type = (joints[pj].type == JType::Fixed)     ? krs::dyn::JType::Fixed
                    : (joints[pj].type == JType::Prismatic) ? krs::dyn::JType::Prismatic
                                                            : krs::dyn::JType::Revolute;
            rj.member = true;   // in the chain (a Fixed joint positions its child rigidly; 0-DOF)
            rj.Rtree = rel.block<3,3>(0,0);
            // BORE ANCHOR: place the joint frame ORIGIN on the BORE AXIS LINE so the revolute rotates
            // about the bore, not the child link's CAD origin. Use the point on the bore axis CLOSEST to
            // the child link origin -- this removes only the LATERAL offset (the actual bug, e.g. FANUC J0)
            // while keeping the along-axis position, so a well-modeled joint whose link origin already lies
            // on the bore axis (every synthetic graph + FANUC J1-J5) is an EXACT no-op (bodyPose(0)
            // unchanged). axisPos unset (0,0,0) also falls back to the link origin.
            const glm::vec3 bore = joints[pj].axisPos;
            const Eigen::Vector3d linkOrigin = rel.block<3,1>(0,3);
            if (glm::length(bore) > 1e-9f) {
                const Eigen::Matrix4d invP = bodies[pb].placement.inverse();
                const Eigen::Vector4d bw(double(bore.x), double(bore.y), double(bore.z), 1.0);
                const Eigen::Vector3d borePar = (invP * bw).head<3>();                 // bore point in parent frame
                Eigen::Vector3d axPar = invP.block<3,3>(0,0) *
                                        Eigen::Vector3d(joints[pj].axisDir.x, joints[pj].axisDir.y, joints[pj].axisDir.z);
                const double L = axPar.norm();
                if (L > 1e-9) {
                    axPar /= L;
                    const Eigen::Vector3d proj = borePar + (linkOrigin - borePar).dot(axPar) * axPar;  // closest axis point
                    // Move ONLY when the link origin is genuinely off the bore axis (the bug). Otherwise use
                    // the link origin EXACTLY so a well-modeled joint is bit-exact (no ~1e-8 projection drift).
                    rj.ptree = ((linkOrigin - proj).norm() > 1e-6) ? proj : linkOrigin;
                } else {
                    rj.ptree = linkOrigin;
                }
            } else {
                rj.ptree = linkOrigin;
            }
            // joint axis expressed in the PARENT link frame (world axis rotated by parent^-1).
            const Eigen::Matrix3d Rp = bodies[pb].placement.block<3,3>(0,0);
            const Eigen::Vector3d aw(joints[pj].axisDir.x, joints[pj].axisDir.y, joints[pj].axisDir.z);
            rj.axis = (Rp.transpose() * aw).normalized();
            rj.frameProv = (joints[pj].prov == Prov::Manual) ? krs::robot::Provenance::UserSupplied
                                                             : krs::robot::Provenance::GeometryDerived;
            // Carry engineering limits so clampDof enforces them. Continuous (enabled=false)
            // => effectively unlimited. effort/velocity 0 == unspecified -> keep Joint defaults.
            const JointLimits& L = joints[pj].limits;
            if (L.enabled) { rj.qLower = L.lower; rj.qUpper = L.upper; }
            else           { rj.qLower = -1e9;    rj.qUpper = 1e9; }
            if (L.velocity > 0.0) rj.vMax = L.velocity;
            if (L.effort   > 0.0) rj.effortMax = L.effort;
            rj.engProv = (joints[pj].prov == Prov::Manual) ? krs::robot::Provenance::UserSupplied
                                                           : krs::robot::Provenance::GeometryDerived;
            // carry joint identity into the derived model so it survives the live<->graph round-trip.
            rj.id     = joints[pj].id;
            rj.nodeId = joints[pj].nodeId;
            rj.name   = joints[pj].name;
            // tree edge: chain-DOF index of this body's parent. orderPos[base]=0 -> -1 (base/root,
            // read by toChain as the serial-root); a deeper parent gives its chain-body index. The
            // -2 fallback (malformed) defers to the serial previous-body rule.
            rj.treeParent = (pb >= 0 && orderPos[pb] >= 0) ? (orderPos[pb] - 1) : -2;
            r.joints.push_back(rj);
        }
        return r;
    }
};

// ---- manual joint definition from two SELECTED features (Phase 2) ----------
// Reuses the gated derivation: two selected cylindrical bores (their analytic
// BRepFace, in WORLD frame) -> a revolute frame. Returns false (no joint) for a
// degenerate pair (non-coaxial / non-cylinder), rejected for the right reason.
inline bool defineRevoluteFromSelection(const BRepFace& worldFaceA, const BRepFace& worldFaceB,
                                        int bodyA, int bodyB, RBJoint& out,
                                        double coaxTol = 5e-3,   // match the auto-parser; tight enough to
                                                                 // reject non-coaxial bores, loose enough
                                                                 // for real machined CAD bore pairs
                                        bool requireCollinear = true)  // manual define passes false: the mate
                                                                       // snaps an offset (but parallel) pair coaxial
{
    if (bodyA == bodyB) return false;   // a joint must connect two DISTINCT bodies (no fiducial self-joint)
    krs::joint::JointFrame jf; double ang = 0, off = 0;
    if (!krs::joint::deriveRevoluteFromBores(worldFaceA, worldFaceB, jf, coaxTol, &ang, &off, requireCollinear)) return false;
    out.parent = bodyA; out.child = bodyB; out.type = JType::Revolute;
    out.axisPos = jf.axisPos; out.axisDir = jf.axisDir;
    out.orthonormalizeFrame();                              // persist a full mate frame (decoupled from the faces)
    out.prov = Prov::Manual; out.residual = std::max(ang, off); out.ambiguous = false;
    return true;
}

// ---- build a chain from parsed parts: greedily connect coaxial-cylinder
//      interfaces (lowest residual first) into a SPANNING TREE rooted at `base`.
//      Cycles are avoided (union-find); pairs with no coaxial interface are left
//      unjointed (honest -- not every touching pair becomes a joint). -----------
inline RobotGraph buildGraphFromParts(const std::vector<ParsedPart>& parts, int base = 0)
{
    RobotGraph g; g.bodies = parts; g.base = base;
    struct Edge { RBJoint j; double res; int a, b; };
    std::vector<Edge> edges;
    for (int i = 0; i < int(parts.size()); ++i)
        for (int j = i + 1; j < int(parts.size()); ++j) {
            RBJoint cand;
            if (inferRevolute(parts[i], parts[j], cand)) {
                cand.parent = i; cand.child = j;
                edges.push_back({ cand, cand.residual, i, j });
            }
        }
    std::sort(edges.begin(), edges.end(), [](const Edge& x, const Edge& y) { return x.res < y.res; });
    std::vector<int> uf(parts.size());
    for (int i = 0; i < int(uf.size()); ++i) uf[i] = i;
    auto find = [&uf](int x) { while (uf[x] != x) x = uf[x]; return x; };
    for (const auto& e : edges) {
        const int ra = find(e.a), rb = find(e.b);
        if (ra != rb) { uf[ra] = rb; g.addJoint(e.j); }     // tree edge -> commit the joint
    }
    return g;
}

// ---- NAME-DRIVEN serial-chain parse (the robust first guess for a named arm) -----------
// Pure geometric inference is AMBIGUOUS for a compact wrist (j4/j5/j6 share a near-common
// axis line, so lowest-residual coaxial pairing mis-orders them). When the CAD parts are
// NAMED by joint (FANUC: 430-base, 430-j1..j6, the 430_j3-* forearm cluster), the NAMES
// establish the serial ORDER; geometry only supplies each joint's axis. This:
//   * groups parts by joint number (the first 'j<1..6>' token in the name; else base),
//   * COLLAPSES each multi-part link into ONE RBBody (the 430_j3-* cluster + 430-j1-hardstop
//     ride their link as extraEntities, driven together by instantiateFromGraph),
//   * adds a REVOLUTE between consecutive link bodies, axis from the best coaxial bore
//     between the two link groups (ambiguous=true if none -> user defines it),
//   * orders base -> j1 -> j2 -> j3 -> j4 -> j5 -> j6.
// Returns a RobotGraph whose committed revolute joints are in the NAMED chain order.
inline int jointNumberFromName(const std::string& nm) {
    for (size_t i = 0; i + 1 < nm.size(); ++i)
        if ((nm[i] == 'j' || nm[i] == 'J') && nm[i + 1] >= '1' && nm[i + 1] <= '6')
            return nm[i + 1] - '0';
    return 0;   // base / static accessory
}
inline RobotGraph buildNamedSerialChain(const std::vector<ParsedPart>& parts)
{
    RobotGraph g; g.base = 0;
    std::array<std::vector<int>, 7> grp;            // part indices per joint number [0]=base/static, [1..6]=j1..j6
    for (int i = 0; i < int(parts.size()); ++i) grp[jointNumberFromName(parts[i].name)].push_back(i);

    // COLLAPSE each link group into ONE RBBody: the representative part carries the link's
    // frame/faces; the OTHER parts of the link ride it as extraEntities (multi-solid link).
    // instantiateFromGraph drives {entity} U extraEntities together, so a whole link (e.g. the
    // 430_j3-* forearm cluster) moves as one. Result: bodies = base, j1, j2, j3, j4, j5, j6.
    std::vector<std::vector<int>> linkParts;        // original part indices per collapsed body
    for (int L = 0; L <= 6; ++L) {
        if (grp[L].empty()) continue;
        const int rep = grp[L].front();
        RBBody b = parts[rep];                       // name + placement + faces + entity (rep)
        for (size_t k = 1; k < grp[L].size(); ++k)
            if (parts[grp[L][k]].entity >= 0) b.extraEntities.push_back(parts[grp[L][k]].entity);
        g.bodies.push_back(b);
        linkParts.push_back(grp[L]);
    }
    g.base = 0;                                      // bodies[0] is the base link

    // Serial REVOLUTE between consecutive link bodies, axis = the best coaxial bore across the
    // two link GROUPS (searching all part-pairs, each in its own placement). The name prior
    // already GUARANTEES these links connect, so the axis search is RELAXED vs the strict
    // pure-geometric spanning tree (which must avoid false positives). If no coaxial bore is
    // found, the joint is flagged ambiguous (axis to be defined manually).
    // Largest world-space cylinder face across a link group (the dominant bore/bearing) --
    // the fallback axis source when no clean coaxial PAIR exists.
    auto biggestCyl = [&](const std::vector<int>& gids, BRepFace& out)->bool {
        float best = -1.f; bool any = false;
        for (int pi : gids) {
            const ParsedPart& P = parts[pi];
            for (const auto& f : P.faces) {
                if (f.type != 1 || f.radius <= best) continue;
                out = faceToWorld(f, P.placement); best = f.radius; any = true;
            }
        }
        return any;
    };
    for (int bi = 1; bi < int(g.bodies.size()); ++bi) {
        RBJoint best; double bestRes = 1e30; bool found = false;

        // (0) BASE-JOINT VERTICALITY PRIOR (J0 only, bi==1): the first joint is the base turntable,
        //     whose axis is the base MOUNTING NORMAL -- the base's own part-Z -- not a horizontal
        //     flange/bolt bore. The generic radius/coaxiality search below latches onto the largest
        //     horizontal bore (a bolt circle / flange face), giving J0 a wrong horizontal axis. So
        //     for the base joint, prefer the cylinder whose axis is most parallel to the base part-Z;
        //     if none is convincingly vertical, fall back to the base part-Z through the base origin.
        //     (Keyed off the base's OWN Z so it follows a tilted base mount, not hard world-up.)
        if (bi == 1) {
            const Eigen::Vector3d upE = g.bodies[g.base].placement.block<3, 1>(0, 2);
            const glm::vec3 up = glm::normalize(glm::vec3(float(upE.x()), float(upE.y()), float(upE.z())));
            BRepFace bestF; float bestVert = -1.f; bool anyCyl = false;
            for (int side = 0; side < 2; ++side)
                for (int pi : linkParts[bi - 1 + side])
                    for (const auto& f : parts[pi].faces) {
                        if (f.type != 1) continue;
                        const BRepFace w = faceToWorld(f, parts[pi].placement);
                        const float vert = std::abs(glm::dot(glm::normalize(w.axisDir), up));
                        if (vert > bestVert) { bestVert = vert; bestF = w; anyCyl = true; }
                    }
            if (anyCyl && bestVert > 0.7f) {           // a convincingly-vertical bore exists -> use it
                best.axisDir = glm::normalize(bestF.axisDir);
                best.axisPos = bestF.axisPos;
            } else {                                    // else the base mounting normal through the base origin
                best.axisDir = up;
                best.axisPos = glm::vec3(float(g.bodies[g.base].placement(0, 3)),
                                         float(g.bodies[g.base].placement(1, 3)),
                                         float(g.bodies[g.base].placement(2, 3)));
            }
            if (glm::dot(best.axisDir, up) < 0.0f) best.axisDir = -best.axisDir;  // consistent +up sign
            best.residual = 0.0; found = true;
        }

        // (1) best coaxial bore PAIR across the two groups (highest confidence).
        if (!found) for (int a : linkParts[bi - 1]) for (int b : linkParts[bi]) {
            RBJoint cand;
            if (inferRevolute(parts[a], parts[b], cand, /*coaxTol*/ 5e-3, /*radTol*/ 5e-3)
                && cand.residual < bestRes) {
                bestRes = cand.residual; best = cand; found = true;
            }
        }
        // (2) FALLBACK: the largest cylinder on each link, radius-agnostic, relaxed coaxiality
        //     (the structural pivot often shares a hinge LINE even when bore/shaft radii differ).
        if (!found) {
            BRepFace wa, wb;
            if (biggestCyl(linkParts[bi - 1], wa) && biggestCyl(linkParts[bi], wb)) {
                krs::joint::JointFrame jf; double ang = 0, off = 0;
                if (krs::joint::deriveRevoluteFromBores(wa, wb, jf, /*coaxTol*/ 1.5e-2, &ang, &off)) {
                    best.axisPos = jf.axisPos; best.axisDir = jf.axisDir;
                    best.residual = std::max(ang, off); found = true;
                }
            }
        }
        // (3) LAST RESORT: the child link's largest cylinder axis (a drivable guess so the
        //     chain stays connected/6-DoF; flagged low-confidence for the user to refine).
        bool lowConf = false;
        if (!found) {
            BRepFace wb;
            if (biggestCyl(linkParts[bi], wb)) {
                best.axisPos = wb.axisPos; best.axisDir = wb.axisDir; best.residual = 1.0; found = true; lowConf = true;
            }
        }
        best.parent = bi - 1; best.child = bi; best.type = JType::Revolute; best.prov = Prov::Inferred;
        // Commit every joint so the chain stays connected (a drivable 6-DoF first guess); only a
        // joint with NO cylinder at all on either link is ambiguous (degenerate). Low-confidence
        // fallback axes are committed + refinable via the editable graph.
        best.ambiguous = !found;
        if (!found) { best.axisDir = glm::vec3(0, 0, 1); best.axisPos = glm::vec3(0); }
        (void)lowConf;
        best.orthonormalizeFrame();
        g.addJoint(best);
    }
    return g;
}

// PHASE 1 -- parse a STEP assembly into bodies (name + world placement + analytic
// faces in part-local frame) via STEPCAFControl_Reader. KR_WITH_OCCT only.
std::vector<ParsedPart> parseAssembly(const std::string& path);

// ---- EDITING-PANEL CONTROLLER (the data-op behind each on-screen control) ---
// The editing panel's buttons call these; each INVOKES the proven RobotGraph op and
// the chain re-derives. The gate drives these directly (the Qt panel that wires
// clicks to them is OPERATOR-VISUAL-CONFIRM). A control that reports success without
// invoking its op (or invokes the wrong one) is the failing model the gate rejects.
struct EditController {
    RobotGraph* graph = nullptr;

    int dof() const { return graph ? graph->dof() : -1; }

    // DELETE-JOINT control: removes the joint; the chain re-derives (DOF updates).
    bool deleteJoint(int jointIdx) {
        if (!graph || jointIdx < 0 || jointIdx >= int(graph->joints.size())) return false;
        graph->deleteJoint(jointIdx);
        return true;
    }

    // DEFINE-FROM-FEATURES control: a revolute from two selected world bore features. Normalizes
    // parent = the body NEARER the base in chain order (so the parent stays put and the CHILD subtree
    // is what snaps), and REPLACES an existing joint between the same two bodies instead of appending
    // a duplicate (defining on the already-complete FANUC is a re-mate, not a new edge -- a duplicate
    // would be ignored by chainOrder and the user would "see no joint"). Returns false (no joint) on a
    // degenerate pair (rejected for the right reason). outParent/outChild report the normalized roles.
    bool defineFromFeatures(const BRepFace& worldFaceA, int bodyA,
                            const BRepFace& worldFaceB, int bodyB, RBJoint* created = nullptr,
                            int* outParent = nullptr, int* outChild = nullptr,
                            bool requireCollinear = true) {   // manual panel define passes false (mate snaps coaxial)
        if (!graph) return false;
        if (bodyA == bodyB) return false;   // distinct-body invariant (no self-joint at the data layer)
        RBJoint j;
        if (!defineRevoluteFromSelection(worldFaceA, worldFaceB, bodyA, bodyB, j, 5e-3, requireCollinear)) return false;
        // parent = lower chain index (base side); fall back to lower body index if unordered.
        const RobotGraph::ChainOrder co = graph->chainOrder();
        auto idxOf = [&](int b) {
            for (size_t i = 0; i < co.order.size(); ++i) if (co.order[i] == b) return int(i);
            return 1000000 + b;   // unreached -> sort after ordered, stable by body index
        };
        int parent = bodyA, child = bodyB;
        if (idxOf(bodyB) < idxOf(bodyA)) { parent = bodyB; child = bodyA; }
        j.parent = parent; j.child = child;
        // RE-ROOT (one parent per body): if `child` already attaches to the tree via a DIFFERENT parent
        // joint, drop that edge so the new mate is the child's SOLE parent. Without this, subtreeOf(child)
        // walks the STALE parent and the snap moves the wrong (often whole-arm) closure -- the "define
        // shoves all the other bodies down" bug when mating an already-attached sub-assembly to a new body.
        const int oldPj = (child >= 0 && child < int(co.parentJoint.size())) ? co.parentJoint[child] : -1;
        if (oldPj >= 0 && oldPj < int(graph->joints.size())) {
            const RBJoint& oj = graph->joints[oldPj];
            const bool sameEdge = (oj.parent == parent && oj.child == child) || (oj.parent == child && oj.child == parent);
            if (!sameEdge) graph->deleteJoint(oldPj);   // re-root: remove the child's prior parent edge
        }
        const int ex = graph->jointBetween(bodyA, bodyB);
        if (ex >= 0) { j.limits = graph->joints[ex].limits; graph->joints[ex] = j; }  // REPLACE (no dup)
        else         graph->addJoint(j);
        if (created)   *created   = j;
        if (outParent) *outParent = parent;
        if (outChild)  *outChild  = child;
        return true;
    }

    // SET-JOINT-TYPE control: re-type an existing joint (Revolute/Prismatic/Fixed).
    // Switching to/from Fixed changes dof() (Fixed is excluded), so the caller must
    // rebuild the LiveRobot + re-init the articulation. Marks the joint Manual.
    bool setJointType(int jointIdx, JType newType) {
        if (!graph || jointIdx < 0 || jointIdx >= int(graph->joints.size())) return false;
        graph->joints[jointIdx].type = newType;
        graph->joints[jointIdx].prov = Prov::Manual;
        return true;
    }

    // SET-LIMITS control: edit a joint's position/effort/velocity bounds (or make it
    // continuous via enabled=false). Marks the joint Manual.
    bool setJointLimits(int jointIdx, const JointLimits& lim) {
        if (!graph || jointIdx < 0 || jointIdx >= int(graph->joints.size())) return false;
        graph->joints[jointIdx].limits = lim;
        graph->joints[jointIdx].prov   = Prov::Manual;
        return true;
    }

    // SET-JOINT-AXIS control: edit a joint's rotation/translation axis DIRECTION (world frame).
    // Normalizes, re-orthonormalizes the persisted mate frame (refDir kept perpendicular to the
    // new axis), and marks the joint Manual. The world axis is rotated into the parent-link frame
    // by toRobot() (rj.axis = parent.R^-1 * axisDir), so this is the single source of the FK axis.
    // Returns false on a zero-length direction (rejected for the right reason).
    bool setJointAxis(int jointIdx, const glm::vec3& worldDir) {
        if (!graph || jointIdx < 0 || jointIdx >= int(graph->joints.size())) return false;
        if (glm::length(worldDir) < 1e-6f) return false;
        graph->joints[jointIdx].axisDir = glm::normalize(worldDir);
        graph->joints[jointIdx].orthonormalizeFrame();
        graph->joints[jointIdx].prov = Prov::Manual;
        return true;
    }
};

// ---- PHASE 0 recon + PHASES 1-4 gates (defined in RobotBuilder.cpp /
//      RobotBuilderGate.cpp) -------------------------------------------------
bool runParseReconGate();        // PHASE 0 (OCCT, real FANUC STEP)
bool runAutoParseReport();       // PHASE 1 real-assembly demo: FANUC parse -> inference -> report (OCCT)
bool runAutoParseChainGate();    // PHASE 1 (synthetic: inferred axes match geometry; FK==placements; ambiguous not faked)
bool runBaseAxisVerticalGate();  // J0 base-turntable axis = vertical part-Z, not a horizontal flange bore (verticality prior)
bool runMateSnapGate();          // concentric mate transform aligns child bore to parent axis; subtreeOf collects the sub-assembly
bool runSplitMergeGate();        // cut a joint -> base+branch graphs; re-mate merges; FK round-trips; bad input rejected
bool runConnectedComponentsGate();// a "robot" is a DERIVED component: serial->1 comp, disjoint->2, chooseBase deterministic, re-root spans
bool runBoreAnchorGate();        // the lowered revolute rotates about the BORE axis (axisPos), not the child link CAD origin
bool runUrdfExportGate();        // export a component to URDF (base-pick + tree-search): links/joints/types/limits round-trip
bool runJointEditGate();         // PHASE 2 (manual joint from selected features matches; degenerate rejected; chain re-derives)
bool runTagOwnershipGate();      // PHASE 3 (single-owner lock-out; membership-tracked)
bool runSubtreeDetachGate();     // PHASE 4 (downstream subtree detaches intact; tag tracks membership)
bool runEditOpInvokedGate();     // CONFIG Phase 3 (panel controls invoke the proven ops; no-op/wrong-op neg-ctrls)

// Export the connected component containing `baseBody` (spanning tree rooted there) as a URDF string --
// the joint-primary "pick a base link, derive the chain from the joint tree" export. Defined in
// src/Utility/UrdfExport.cpp.
std::string exportGraphToUrdf(const RobotGraph& g, int baseBody = 0, const std::string& robotName = "exported");

} // namespace krs::rbuild
