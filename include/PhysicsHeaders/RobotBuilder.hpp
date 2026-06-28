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
#include <algorithm>
#include <cmath>

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
    int entity = -1;                 // scene entity once spawned (-1 = not spawned)
    glm::vec3 visSize{ 0.12f, 0.12f, 0.12f };  // per-axis size of the placeholder box mesh
                                               // (lets the demo build proper-looking links)
};
using RBBody = ParsedPart;

// ---- a joint connecting two bodies, frame DERIVED from feature geometry -----
struct RBJoint {
    int parent = -1;
    int child  = -1;
    JType type = JType::Revolute;
    glm::vec3 axisPos{ 0.0f };               // world-frame point on the joint axis
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f };   // world-frame unit axis
    Prov prov = Prov::Inferred;
    bool ambiguous = false;                  // flagged low-confidence -> NOT a committed joint
    double residual = 0.0;                   // coaxiality residual (diagnostic)
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

    // bodies reachable from base through committed joints (BFS over the undirected graph).
    std::set<int> members() const {
        std::set<int> seen;
        if (base < 0 || base >= int(bodies.size())) return seen;
        std::vector<int> stack{ base }; seen.insert(base);
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
    bool isMember(int body) const { const auto m = members(); return m.count(body) != 0; }

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

    int addJoint(const RBJoint& j) { joints.push_back(j); return int(joints.size()) - 1; }

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
    ChainOrder chainOrder() const {
        ChainOrder co;
        co.parentJoint.assign(bodies.size(), -1);
        co.parentBody.assign(bodies.size(), -1);
        if (base < 0 || base >= int(bodies.size())) return co;
        const auto m = members();
        std::set<int> seen{ base }; std::vector<int> stack{ base };
        co.order.push_back(base);
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
    // Body indices in chain order (order[0]=base; order[k>=1] = chain body k-1).
    std::vector<int> chainBodyOrder() const { return chainOrder().order; }

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
        for (size_t k = 1; k < order.size(); ++k) {           // skip base (k=0)
            const int body = order[k];
            const int pj = co.parentJoint[body];
            const int pb = co.parentBody[body];
            const Eigen::Matrix4d rel = bodies[pb].placement.inverse() * bodies[body].placement;
            krs::robot::Joint rj;
            rj.type = (joints[pj].type == JType::Fixed) ? krs::dyn::JType::Fixed : krs::dyn::JType::Revolute;
            rj.member = true;
            rj.Rtree = rel.block<3,3>(0,0);
            rj.ptree = rel.block<3,1>(0,3);
            // joint axis expressed in the PARENT link frame (world axis rotated by parent^-1).
            const Eigen::Matrix3d Rp = bodies[pb].placement.block<3,3>(0,0);
            const Eigen::Vector3d aw(joints[pj].axisDir.x, joints[pj].axisDir.y, joints[pj].axisDir.z);
            rj.axis = (Rp.transpose() * aw).normalized();
            rj.frameProv = (joints[pj].prov == Prov::Manual) ? krs::robot::Provenance::UserSupplied
                                                             : krs::robot::Provenance::GeometryDerived;
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
                                        double coaxTol = 1e-4)
{
    krs::joint::JointFrame jf; double ang = 0, off = 0;
    if (!krs::joint::deriveRevoluteFromBores(worldFaceA, worldFaceB, jf, coaxTol, &ang, &off)) return false;
    out.parent = bodyA; out.child = bodyB; out.type = JType::Revolute;
    out.axisPos = jf.axisPos; out.axisDir = jf.axisDir;
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

    // DEFINE-FROM-FEATURES control: a revolute from two selected world bore features.
    // Returns false (no joint) on a degenerate pair (rejected for the right reason).
    bool defineFromFeatures(const BRepFace& worldFaceA, int bodyA,
                            const BRepFace& worldFaceB, int bodyB, RBJoint* created = nullptr) {
        if (!graph) return false;
        RBJoint j;
        if (!defineRevoluteFromSelection(worldFaceA, worldFaceB, bodyA, bodyB, j)) return false;
        graph->addJoint(j);
        if (created) *created = j;
        return true;
    }
};

// ---- PHASE 0 recon + PHASES 1-4 gates (defined in RobotBuilder.cpp /
//      RobotBuilderGate.cpp) -------------------------------------------------
bool runParseReconGate();        // PHASE 0 (OCCT, real FANUC STEP)
bool runAutoParseReport();       // PHASE 1 real-assembly demo: FANUC parse -> inference -> report (OCCT)
bool runAutoParseChainGate();    // PHASE 1 (synthetic: inferred axes match geometry; FK==placements; ambiguous not faked)
bool runJointEditGate();         // PHASE 2 (manual joint from selected features matches; degenerate rejected; chain re-derives)
bool runTagOwnershipGate();      // PHASE 3 (single-owner lock-out; membership-tracked)
bool runSubtreeDetachGate();     // PHASE 4 (downstream subtree detaches intact; tag tracks membership)
bool runEditOpInvokedGate();     // CONFIG Phase 3 (panel controls invoke the proven ops; no-op/wrong-op neg-ctrls)

} // namespace krs::rbuild
