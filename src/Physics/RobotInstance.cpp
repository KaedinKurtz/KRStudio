// ===========================================================================
// ROBOT-FIRST-CLASS FOUNDATION -- the runtime side of the krs::robot::LiveRobot
// owner (declared in RobotModel.hpp): the instantiate-from-schema factory, the
// Robot-FK -> viz writeback, and the headless KRS_ROBOTOWNER_SELFTEST gate.
//
// Ownership model (user-decided): the LiveRobot owns the canonical q (commanded)
// -- the SINGLE SOURCE OF TRUTH for joint angles. The node graph WRITES q, PhysX
// FOLLOWS q (kinematic targets) + reports qActual back as influence, and the
// RobotGraph is the SCHEMA the Robot is instantiated from. This file grows across
// foundation steps 1-5; each step extends runRobotOwnerGate with a measured check.
// ===========================================================================
#include "RobotModel.hpp"
#include "RobotBuilder.hpp"        // krs::rbuild::RobotGraph (the authoring schema)
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies (gate)
#include "ArticulationSpec.hpp"    // krs::dyn::RobotArticSpec (FANUC canonical spec)
#include "FanucArticulation.hpp"   // krs::fanuc::canonicalSpec
#include "GizmoSystem.hpp"         // GizmoHandleComponent (filtered from the outliner)
#include "Scene.hpp"
#include "components.hpp"

#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <string>
#include <set>

namespace krs::robot {

namespace {
constexpr double kPi = 3.14159265358979323846;

// A 3-DOF demo Robot (yaw Z, pitch Y, pitch Y) for the pure-CPU owner checks.
Robot demoRobot3() {
    Robot r; r.name = "owner_demo"; r.nLinks = 4;
    Joint j1; j1.member = true; j1.axis = Eigen::Vector3d(0, 0, 1); j1.ptree = Eigen::Vector3d(0, 0, 0.0); j1.qLower = -kPi; j1.qUpper = kPi;
    Joint j2; j2.member = true; j2.axis = Eigen::Vector3d(0, 1, 0); j2.ptree = Eigen::Vector3d(0, 0, 0.3); j2.qLower = -1.0; j2.qUpper = 1.0;
    Joint j3; j3.member = true; j3.axis = Eigen::Vector3d(0, 1, 0); j3.ptree = Eigen::Vector3d(0.5, 0, 0); j3.qLower = -2.0; j3.qUpper = 2.0;
    r.joints = { j1, j2, j3 };
    return r;
}

// --- Eigen <-> TransformComponent helpers (rigid; scale assumed 1 for links) ---
Eigen::Matrix4d poseToEig(const krs::dyn::Pose& p) {
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    M.block<3, 3>(0, 0) = p.R; M.block<3, 1>(0, 3) = p.p; return M;
}
Eigen::Matrix4d eigFromTransform(const TransformComponent& tc) {
    const glm::mat4 g = tc.getTransform();      // column-major (glm[col][row])
    Eigen::Matrix4d M;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) M(r, c) = double(g[c][r]);
    return M;
}
void setTransformFromEig(TransformComponent& tc, const Eigen::Matrix4d& M) {
    tc.translation = glm::vec3(float(M(0, 3)), float(M(1, 3)), float(M(2, 3)));
    glm::mat3 gR;
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) gR[c][r] = float(M(r, c));
    tc.rotation = glm::normalize(glm::quat_cast(gR));
    // scale left untouched (links are rigid, scale stays 1)
}

// Convert a POD articulation spec (FANUC canonicalSpec) into the krs::robot schema.
// All joints are MEMBERs so nq() == the PhysX DOF count and the member-joint order
// matches the PhysX DOF order 1:1 (no permutation needed for the FANUC).
Robot robotFromArticSpec(const krs::dyn::RobotArticSpec& spec) {
    Robot r; r.name = "articulated";
    r.nLinks = int(spec.joints.size()) + 1;     // fixed base + one link per joint
    for (const auto& j : spec.joints) {
        Joint rj;
        rj.type   = j.revolute ? krs::dyn::JType::Revolute : krs::dyn::JType::Prismatic;
        rj.member = true;
        Eigen::Matrix3d R;                       // spec.Rtree is ROW-major 3x3
        for (int rr = 0; rr < 3; ++rr) for (int cc = 0; cc < 3; ++cc) R(rr, cc) = double(j.Rtree[rr * 3 + cc]);
        rj.Rtree = R;
        rj.ptree = Eigen::Vector3d(j.ptree[0], j.ptree[1], j.ptree[2]);
        rj.axis  = Eigen::Vector3d(j.axis[0], j.axis[1], j.axis[2]).normalized();
        // Carry the real engineering limits so clampDof() enforces them (the FINAL clamp).
        // Without this, qLower/qUpper kept their +/-pi defaults and the drive never clamped.
        rj.qLower = double(j.qLower); rj.qUpper = double(j.qUpper);
        rj.vMax = double(j.vMax); rj.effortMax = double(j.effortMax);
        rj.engProv = Provenance::UserSupplied;
        r.joints.push_back(rj);
    }
    return r;
}
} // namespace

// Capture each link's rest world pose + each driven entity's rest world transform
// at the CURRENT q (call right after instantiation while q==0). Enables delta-from-
// rest viz that works when one link drives several solids (the FANUC case).
void captureRobotRest(Scene& scene, LiveRobot& lr) {
    auto& reg = scene.getRegistry();
    const std::vector<krs::dyn::Pose> poses = lr.fkLinks();
    lr.restLinkWorld.assign(lr.ndof(), Eigen::Matrix4d::Identity());
    lr.linkEntityRestWorld.assign(lr.ndof(), {});
    for (int k = 0; k < lr.ndof(); ++k) {
        if (k < int(poses.size())) lr.restLinkWorld[k] = lr.model.basePlacement * poseToEig(poses[k]);
        for (entt::entity e : lr.linkEntities[k]) {
            const auto* tc = reg.try_get<TransformComponent>(e);
            lr.linkEntityRestWorld[k].push_back(tc ? eigFromTransform(*tc) : Eigen::Matrix4d::Identity());
        }
    }
}

// Drive link entity transforms from Robot FK(q): per link, delta = linkWorld(q) *
// linkWorld(0)^-1, applied to each driven entity's rest world transform. Robot FK
// (not PhysX) is the viz source. No-op unless useRobotFkViz is set.
void writeBackRobotViz(Scene& scene, LiveRobot& lr) {
    if (!lr.useRobotFkViz) return;
    auto& reg = scene.getRegistry();
    const std::vector<krs::dyn::Pose> poses = lr.fkLinks();
    for (int k = 0; k < lr.ndof() && k < int(poses.size()); ++k) {
        if (k >= int(lr.restLinkWorld.size())) continue;
        const Eigen::Matrix4d linkNow = lr.model.basePlacement * poseToEig(poses[k]);
        const Eigen::Matrix4d delta   = linkNow * lr.restLinkWorld[k].inverse();
        for (size_t ei = 0; ei < lr.linkEntities[k].size(); ++ei) {
            const entt::entity e = lr.linkEntities[k][ei];
            if (!reg.valid(e)) continue;
            auto* tc = reg.try_get<TransformComponent>(e);
            if (!tc) continue;
            const Eigen::Matrix4d rest =
                (ei < lr.linkEntityRestWorld[k].size()) ? lr.linkEntityRestWorld[k][ei]
                                                        : Eigen::Matrix4d::Identity();
            setTransformFromEig(*tc, delta * rest);
        }
    }
}

// ---------------------------------------------------------------------------
// Factory: a RobotGraph (authoring schema) -> a live, first-class Robot.
// ---------------------------------------------------------------------------
LiveRobot* instantiateFromGraph(Scene& scene, const krs::rbuild::RobotGraph& g, int robotId)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rrp = reg.ctx().find<RobotRegistry>();
    if (!rrp) rrp = &reg.ctx().emplace<RobotRegistry>();
    RobotRegistry& registry = *rrp;

    LiveRobot& lr = registry.create(robotId);
    lr.model   = g.toRobot();
    lr.robotId = robotId;
    lr.name    = (lr.model.name.empty() || lr.model.name == "parsed_robot")
                     ? ("Robot " + std::to_string(robotId)) : lr.model.name;
    lr.model.name = lr.name;
    lr.rebuild();

    // Named root entity. REUSE an existing root for this robotId if present, so
    // re-applying an EDITED graph to a live robot is idempotent (no duplicate roots in
    // the outliner). Identity TransformComponent so parenting is a transform NO-OP
    // (propagateTransforms composes world = parent * local; identity parent leaves the
    // existing absolute-world body transforms correct -- no double-apply).
    entt::entity root = entt::null;
    for (auto e : reg.view<RobotRootComponent>())
        if (reg.get<RobotRootComponent>(e).robotId == robotId) { root = e; break; }
    if (root == entt::null || !reg.valid(root)) {
        root = reg.create();
        reg.emplace<TransformComponent>(root, glm::vec3(0.0f),
                                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    }
    reg.emplace_or_replace<RobotRootComponent>(root, RobotRootComponent{ lr.name, robotId });
    reg.emplace_or_replace<TagComponent>(root, lr.name);
    lr.root = root;

    // Map chain bodies -> graph body entities using the SAME ordering toRobot() used,
    // and stamp the real robotId + parent each member body under the root.
    const std::vector<int> order = g.chainBodyOrder();   // order[0]=base; order[k>=1]=chain body k-1
    lr.linkEntities.assign(lr.ndof(), {});
    for (size_t k = 0; k < order.size(); ++k) {
        const int bodyIdx = order[k];
        if (bodyIdx < 0 || bodyIdx >= int(g.bodies.size())) continue;
        // Every solid in this body's link: the primary entity plus any extraEntities
        // (multi-solid links, e.g. the FANUC). All are parented + driven together.
        std::vector<int> ids = g.bodies[bodyIdx].extraEntities;
        ids.insert(ids.begin(), g.bodies[bodyIdx].entity);
        for (const int eid : ids) {
            if (eid < 0) continue;
            const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
            if (!reg.valid(e)) continue;
            reg.emplace_or_replace<ParentComponent>(e, root);
            reg.emplace_or_replace<RobotSubcomponentComponent>(e, robotId);
            if (k >= 1 && int(k - 1) < lr.ndof()) lr.linkEntities[k - 1].push_back(e);
        }
    }
    captureRobotRest(scene, lr);   // q is still 0 here -> rest = the authored pose
    return &lr;
}

// Build an EDITABLE authoring RobotGraph that MIRRORS a live robot exactly: one RBBody
// per link (placement = the link's rest world pose; entity group = that link's solids),
// one RBJoint per member joint (world frame from jointAxesWorld(); type + limits from the
// model). Built from the LIVE data (not re-derived from CAD), so toRobot() of the result
// reproduces the SAME FK -- the basis for routing the FANUC through one editable-graph
// path. The base body carries no entity (the base link is fixed).
krs::rbuild::RobotGraph buildGraphFromLiveRobot(const LiveRobot& lr)
{
    using krs::rbuild::RBBody; using krs::rbuild::RBJoint; using krs::rbuild::JType;
    krs::rbuild::RobotGraph g;
    g.robotId = lr.robotId;
    g.base    = 0;
    const int n = lr.ndof();

    RBBody base; base.name = lr.name + "_base"; base.placement = lr.model.basePlacement;
    g.bodies.push_back(base);

    const auto axes = lr.jointAxesWorld();   // world (pos,dir) of each member joint at q=0
    for (int k = 0; k < n; ++k) {
        RBBody b; b.name = lr.name + "_link" + std::to_string(k + 1);
        if (k < int(lr.restLinkWorld.size())) b.placement = lr.restLinkWorld[k];
        if (k < int(lr.linkEntities.size())) {
            for (size_t i = 0; i < lr.linkEntities[k].size(); ++i) {
                const int eid = int(static_cast<std::uint32_t>(lr.linkEntities[k][i]));
                if (b.entity < 0) b.entity = eid; else b.extraEntities.push_back(eid);
            }
        }
        g.bodies.push_back(b);
    }
    for (int k = 0; k < n; ++k) {
        RBJoint j; j.parent = k; j.child = k + 1;     // link k -> link k+1
        const krs::robot::Joint& mj = lr.model.joints[lr.memberJoint[k]];
        j.type = (mj.type == krs::dyn::JType::Fixed)     ? JType::Fixed
               : (mj.type == krs::dyn::JType::Prismatic) ? JType::Prismatic
                                                         : JType::Revolute;
        if (k < int(axes.size())) {
            j.axisPos = glm::vec3(float(axes[k].first.x()),  float(axes[k].first.y()),  float(axes[k].first.z()));
            j.axisDir = glm::vec3(float(axes[k].second.x()), float(axes[k].second.y()), float(axes[k].second.z()));
        }
        j.orthonormalizeFrame();
        j.limits.lower = mj.qLower; j.limits.upper = mj.qUpper; j.limits.enabled = true;
        j.prov = krs::rbuild::Prov::Inferred;
        g.joints.push_back(j);
    }
    return g;
}

// Re-apply an EDITED authoring graph to its live robot (the schema<->graph round-trip):
// snap the robot HOME first (so captureRobotRest re-captures the true rest from the
// solids' home transforms), then re-instantiate (idempotent root reuse + entity re-map).
// instantiateFromGraph reuses the existing LiveRobot, so ownsDrive/useRobotFkViz persist.
LiveRobot* reapplyGraphToRobot(Scene& scene, const krs::rbuild::RobotGraph& g, int robotId)
{
    auto& reg = scene.getRegistry();
    if (RobotRegistry* rr = reg.ctx().find<RobotRegistry>()) {
        if (LiveRobot* lr = rr->get(robotId)) {
            if (lr->useRobotFkViz && lr->ndof() > 0) {   // snap solids to home before re-capture
                lr->q.setZero();
                writeBackRobotViz(scene, *lr);
            }
        }
    }
    return instantiateFromGraph(scene, g, robotId);
}

// MATE-SNAP: move the child subtree so its bore frame is concentric with the parent's. Mutates the
// graph body placements AND the live solids' TransformComponents together, so the subsequent
// reapplyGraphToRobot (toRobot from placements + captureRobotRest from TransformComponents at q=0)
// sees a consistent, already-mated rest pose -> the live robot renders the snap.
void snapMateSubtree(Scene& scene, krs::rbuild::RobotGraph& g, int /*parent*/, int child,
                     const krs::rbuild::RBJoint& frameParent, const krs::rbuild::RBJoint& frameChild)
{
    const Eigen::Matrix4d T = krs::rbuild::RobotGraph::mateTransformConcentric(frameParent, frameChild);
    if ((T - Eigen::Matrix4d::Identity()).cwiseAbs().maxCoeff() < 1e-9) return;   // already mated -> no-op
    const std::vector<int> sub = g.subtreeOf(child);
    auto& reg = scene.getRegistry();
    for (int bdy : sub) {
        if (bdy < 0 || bdy >= int(g.bodies.size())) continue;
        g.bodies[bdy].placement = T * g.bodies[bdy].placement;     // graph (FK source for toRobot)
        std::vector<int> ids = g.bodies[bdy].extraEntities;        // all solids of this link
        ids.insert(ids.begin(), g.bodies[bdy].entity);
        for (int eid : ids) {
            if (eid < 0) continue;
            const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
            if (!reg.valid(e)) continue;
            if (auto* tc = reg.try_get<TransformComponent>(e))
                setTransformFromEig(*tc, T * eigFromTransform(*tc));  // live solid (rest source)
        }
    }
}

// RIGID DRAG: translate the WHOLE robot by a world delta -- move its base placement and re-drive the
// link viz, so every link follows as one rigid unit (the "grab the root/parent -> subtree follows"
// behavior). Only the base placement changes; q (the pose) is untouched, so the articulation is kept.
void transformRobot(Scene& scene, int robotId, const Eigen::Matrix4d& Tworld)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return;
    LiveRobot* lr = rr->get(robotId); if (!lr) return;
    lr->model.basePlacement = Tworld * lr->model.basePlacement;   // world-left-multiply; restLinkWorld stays
    if (lr->useRobotFkViz) writeBackRobotViz(scene, *lr);         // delta = base_new*base_old^-1 -> moves links
    if (reg.valid(lr->root)) if (auto* tc = reg.try_get<TransformComponent>(lr->root))
        setTransformFromEig(*tc, Tworld * eigFromTransform(*tc));
}

void translateRobot(Scene& scene, int robotId, const Eigen::Vector3d& deltaWorld)
{
    Eigen::Matrix4d T = Eigen::Matrix4d::Identity(); T.block<3, 1>(0, 3) = deltaWorld;
    transformRobot(scene, robotId, T);
}

// IK DRAG: drag link `body` (a chain-body index) toward a world target point -- solve the DoF ABOVE it
// (DLS IK) so the chain bends to reach the goal, then re-drive the viz. Orientation is held at the
// link's current value (a positional drag). Returns true if the solver converged. q is clamped to the
// joint limits afterwards. This is the "grab a child -> IK drags the chain" behavior.
bool ikDragLink(Scene& scene, int robotId, int body, const Eigen::Vector3d& targetWorld)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr || lr->ndof() <= 0) return false;
    if (body < 0 || body >= lr->chain.nbody()) return false;
    const Eigen::Matrix4d invBase = lr->model.basePlacement.inverse();
    krs::dyn::Pose target;
    target.R = lr->chain.bodyPose(lr->q, body).R;             // hold orientation; drag is positional
    const Eigen::Vector4d tw(targetWorld.x(), targetWorld.y(), targetWorld.z(), 1.0);
    target.p = (invBase * tw).head<3>();                     // world -> chain base frame (IK frame)
    Eigen::VectorXd q = lr->q;
    const krs::dyn::SerialChain::IKResult res = lr->chain.ik(target, body, q);
    for (int i = 0; i < int(q.size()); ++i) q[i] = lr->clampDof(i, q[i]);
    lr->q = q;
    if (lr->useRobotFkViz) writeBackRobotViz(scene, *lr);
    return res.ok;
}

// SPLIT: cut joint `graphJointIdx` of robot `robotId`; the detached subtree becomes a NEW first-class
// robot (its own root + LiveRobot, rooted at the subtree's current world pose) so it moves as a unit,
// while the base side keeps `robotId`. Both stay articulated (their internal joints survive).
bool splitRobotAtJoint(Scene& scene, int robotId, int graphJointIdx, int* outNewRobotId)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr) return false;

    krs::rbuild::RobotGraph g = buildGraphFromLiveRobot(*lr);
    krs::rbuild::RobotGraph base, branch;
    if (!g.splitAtJoint(graphJointIdx, base, branch)) return false;
    if (branch.bodies.empty() || base.bodies.empty()) return false;

    int newId = 1; for (auto& rp : rr->robots) if (rp) newId = std::max(newId, rp->robotId + 1);
    base.robotId = robotId; branch.robotId = newId;

    // Re-instantiate the base (fewer bodies; reuses robot `robotId`'s root), then the branch as a new
    // robot -- which re-parents + re-tags the subtree's entities onto the new root, moving them off the
    // base robot. The branch is FK-viz + drivable so its base placement (drag) moves the whole subtree.
    instantiateFromGraph(scene, base, robotId);
    LiveRobot* br = instantiateFromGraph(scene, branch, newId);
    if (!br) return false;
    br->name = br->model.name = lr->name + " branch " + std::to_string(newId);
    br->useRobotFkViz = lr->useRobotFkViz; br->ownsDrive = false;   // moved by hand, not the node bus
    if (reg.valid(br->root)) {
        reg.emplace_or_replace<RobotRootComponent>(br->root, RobotRootComponent{ br->name, newId });
        reg.emplace_or_replace<TagComponent>(br->root, br->name);
    }
    if (outNewRobotId) *outNewRobotId = newId;
    return true;
}

// MERGE: fold `childRobotId` into `parentRobotId` -- append the child's graph, connect its base to
// `parentBodyIdx` via `crossJoint`, destroy the child robot, and re-instantiate the parent (which
// re-parents the former child's entities). The inverse of splitRobotAtJoint.
bool mergeRobots(Scene& scene, int parentRobotId, int childRobotId,
                 int parentBodyIdx, const krs::rbuild::RBJoint& crossJoint)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lrP = rr->get(parentRobotId); LiveRobot* lrC = rr->get(childRobotId);
    if (!lrP || !lrC || parentRobotId == childRobotId) return false;

    krs::rbuild::RobotGraph gP = buildGraphFromLiveRobot(*lrP);
    krs::rbuild::RobotGraph gC = buildGraphFromLiveRobot(*lrC);
    if (gP.mergeFrom(gC, parentBodyIdx, crossJoint) < 0) return false;
    gP.robotId = parentRobotId;

    // Drop the child robot (root + registry entry) BEFORE re-instantiating the parent, which then
    // re-parents ALL bodies (including the former child's) under the parent root.
    const entt::entity childRoot = lrC->root;
    for (auto it = rr->robots.begin(); it != rr->robots.end(); ++it)
        if (*it && (*it)->robotId == childRobotId) { rr->robots.erase(it); break; }   // invalidates lrC
    if (reg.valid(childRoot)) reg.destroy(childRoot);
    instantiateFromGraph(scene, gP, parentRobotId);
    return true;
}

LiveRobot* instantiateFanucRobot(Scene& scene,
                                 const std::vector<std::vector<entt::entity>>& movingLinkEntities,
                                 const std::vector<entt::entity>& allBodies,
                                 int robotId, const std::string& name)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rrp = reg.ctx().find<RobotRegistry>();
    if (!rrp) rrp = &reg.ctx().emplace<RobotRegistry>();
    LiveRobot& lr = rrp->create(robotId);

    lr.model      = robotFromArticSpec(krs::fanuc::canonicalSpec());
    lr.model.name = name;
    lr.name       = name;
    lr.robotId    = robotId;
    lr.rebuild();

    // Named root (identity transform -> parenting is a transform no-op).
    const entt::entity root = reg.create();
    reg.emplace<RobotRootComponent>(root, RobotRootComponent{ name, robotId });
    reg.emplace<TagComponent>(root, name);
    reg.emplace<TransformComponent>(root, glm::vec3(0.0f),
                                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    lr.root = root;

    // Chain body k == moving link k (J1..J4 -> the 4 moving link entity groups).
    lr.linkEntities.assign(lr.ndof(), {});
    for (int k = 0; k < lr.ndof() && k < int(movingLinkEntities.size()); ++k)
        lr.linkEntities[k] = movingLinkEntities[k];

    // Parent + stamp robotId on ALL the FANUC body entities (base + moving + shells).
    for (entt::entity e : allBodies) {
        if (e == entt::null || !reg.valid(e)) continue;
        reg.emplace_or_replace<ParentComponent>(e, root);
        reg.emplace_or_replace<RobotSubcomponentComponent>(e, robotId);
    }

    captureRobotRest(scene, lr);   // rest at q=0 (basePlacement identity; canonicalSpec is world-aligned)
    // Step 6b: the Robot now OWNS the FANUC -- its q drives the PhysX articulation as a
    // kinematic follower, and Robot FK (delta-from-rest) drives the link viz. The node
    // graph still writes the bus, but it is drained INTO LiveRobot::q first (single source
    // of truth). At q=0 the FK delta is identity, so there is no jump from the rest pose.
    lr.ownsDrive     = true;
    lr.useRobotFkViz = true;
    return &lr;
}

SceneGrouping groupByRobot(entt::registry& reg)
{
    SceneGrouping out;
    std::unordered_map<int, size_t> idToIdx;
    for (auto e : reg.view<RobotRootComponent>()) {
        const auto& rr = reg.get<RobotRootComponent>(e);
        RobotGroup g;
        g.root = e; g.robotId = rr.robotId;
        g.name = rr.name.empty() ? ("Robot " + std::to_string(rr.robotId)) : rr.name;
        idToIdx[rr.robotId] = out.robots.size();
        out.robots.push_back(std::move(g));
    }
    for (auto e : reg.view<TagComponent>()) {
        if (reg.any_of<RobotRootComponent>(e)) continue;          // the root itself
        if (reg.any_of<GizmoHandleComponent>(e)) continue;        // internal gizmo handle
        const auto* sc = reg.try_get<RobotSubcomponentComponent>(e);
        const auto it = (sc ? idToIdx.find(sc->robotId) : idToIdx.end());
        if (sc && it != idToIdx.end()) out.robots[it->second].bodies.push_back(e);
        else                           out.loose.push_back(e);
    }
    return out;
}

int mirrorLiveRobotIntoScene(Scene& viewScene, Scene& mainScene, int robotId,
                             std::vector<std::pair<entt::entity, entt::entity>>& outMap)
{
    auto& mreg = mainScene.getRegistry();
    auto& vreg = viewScene.getRegistry();
    outMap.clear();
    int n = 0;
    for (auto me : mreg.view<RobotSubcomponentComponent, RenderableMeshComponent, TransformComponent>()) {
        if (mreg.get<RobotSubcomponentComponent>(me).robotId != robotId) continue;
        const entt::entity ve = vreg.create();
        vreg.emplace<RenderableMeshComponent>(ve, mreg.get<RenderableMeshComponent>(me));
        vreg.emplace<TransformComponent>(ve, mreg.get<TransformComponent>(me));  // root is identity -> local == world
        if (auto* mat = mreg.try_get<MaterialComponent>(me)) vreg.emplace<MaterialComponent>(ve, *mat);
        if (auto* tag = mreg.try_get<TagComponent>(me))      vreg.emplace<TagComponent>(ve, *tag);
        // carry the material-type tag so the view picks the same shader path
        if (mreg.all_of<TriPlanarMaterialTag>(me))   vreg.emplace<TriPlanarMaterialTag>(ve);
        if (mreg.all_of<UVTexturedMaterialTag>(me))  vreg.emplace<UVTexturedMaterialTag>(ve);
        if (mreg.all_of<TessellatedMaterialTag>(me)) vreg.emplace<TessellatedMaterialTag>(ve);
        if (mreg.all_of<ParallaxMaterialTag>(me))    vreg.emplace<ParallaxMaterialTag>(ve);
        outMap.emplace_back(me, ve);
        ++n;
    }
    return n;
}

int drainCommandBusIntoRobots(entt::registry& reg)
{
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
    const ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
    if (!rr || !cmd) return 0;
    int n = 0;
    for (auto& rp : rr->robots) {
        if (!rp) continue;
        rp->applyCommand(cmd->target, cmd->driven);   // bus -> q (clamped, driven-only)
        ++n;
    }
    return n;
}

bool runRobotOwnerGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[robotowner] GATE ROBOT-OWNER -- LiveRobot owns q (single source of truth); FK==chain.fk; clamp; driven-only\n");

    bool pass = true;

    // ---- rebuild() sizes q/qActual/memberJoint to the member-DOF count ----
    LiveRobot lr; lr.model = demoRobot3(); lr.rebuild();
    const int n = lr.ndof();
    const bool dofOk = (n == 3) && (int(lr.q.size()) == 3) && (int(lr.qActual.size()) == 3) && (int(lr.memberJoint.size()) == 3);
    pass = pass && dofOk;
    printf("[robotowner]   ndof=%d (expect 3)  q=%d qActual=%d members=%d  %s\n",
           n, int(lr.q.size()), int(lr.qActual.size()), int(lr.memberJoint.size()), dofOk ? "OK" : "FAIL");

    // ---- FK parity: LiveRobot::fkLinks() == SerialChain::fk(q) EXACTLY ----
    Eigen::VectorXd qtest(3); qtest << 0.3, -0.5, 0.7;
    lr.setCommandedQ(qtest);
    std::vector<krs::dyn::Pose> a = lr.fkLinks();
    std::vector<krs::dyn::Pose> b; lr.chain.fk(lr.q, b);
    double fkErr = 1e9;
    if (a.size() == b.size() && !a.empty()) {
        fkErr = 0.0;
        for (size_t k = 0; k < a.size(); ++k) {
            fkErr = std::max(fkErr, (a[k].p - b[k].p).cwiseAbs().maxCoeff());
            fkErr = std::max(fkErr, (a[k].R - b[k].R).cwiseAbs().maxCoeff());
        }
    }
    const bool fkOk = (fkErr < 1e-12);
    pass = pass && fkOk;
    printf("[robotowner]   FK parity err=%.3e (links=%d)  %s\n", fkErr, int(a.size()), fkOk ? "OK" : "FAIL");

    // ---- clamp to limits (j2 limit is +/-1.0) ----
    Eigen::VectorXd qover(3); qover << 0.0, 5.0, 0.0;   // j2 way over +1.0
    lr.setCommandedQ(qover);
    const bool clampOk = std::abs(lr.q[1] - 1.0) < 1e-12;
    pass = pass && clampOk;
    printf("[robotowner]   clamp q2 5.0 -> %.4f (expect 1.0)  %s\n", lr.q[1], clampOk ? "OK" : "FAIL");

    // ---- applyCommand writes only DRIVEN DOFs (undriven rest) ----
    lr.q = Eigen::VectorXd::Zero(3);
    std::vector<float> target = { 0.4f, 0.2f, 0.9f };
    std::vector<char>  driven = { 1, 0, 1 };            // DOF1 NOT driven
    lr.applyCommand(target, driven);
    const bool drivenOk = std::abs(lr.q[0] - 0.4) < 1e-6 && std::abs(lr.q[1] - 0.0) < 1e-12 && std::abs(lr.q[2] - 0.9) < 1e-6;
    pass = pass && drivenOk;
    printf("[robotowner]   applyCommand driven={1,0,1} -> q=[%.3f %.3f %.3f] (q1 must stay 0)  %s\n",
           lr.q[0], lr.q[1], lr.q[2], drivenOk ? "OK" : "FAIL");

    // NEG-CTRL: an all-undriven bus leaves q untouched (non-vacuous).
    const Eigen::VectorXd before = lr.q;
    const std::vector<char> none = { 0, 0, 0 };
    lr.applyCommand(target, none);
    const bool negOk = (lr.q - before).cwiseAbs().maxCoeff() < 1e-12;
    pass = pass && negOk;
    printf("[robotowner]   NEG-CTRL all-undriven bus leaves q unchanged: %s\n", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    // ---- STEP 2: instantiate-from-graph creates a named root + parents member bodies + real robotId ----
    {
        Scene scene;
        krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
        krs::rbuild::spawnGraphBodies(scene, g, /*untagged*/ -1);   // bodies exist as entities, robotId=-1
        LiveRobot* rb = instantiateFromGraph(scene, g, 7);
        auto& reg = scene.getRegistry();

        int roots = 0; entt::entity theRoot = entt::null;
        for (auto e : reg.view<RobotRootComponent>()) {
            ++roots;
            if (reg.get<RobotRootComponent>(e).robotId == 7) theRoot = e;
        }
        const std::set<int> mem = g.members();
        bool allTagged = true, allParented = true; int memberBodies = 0;
        for (int idx : mem) {
            const int eid = g.bodies[idx].entity; if (eid < 0) continue;
            ++memberBodies;
            const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
            const auto* sc = reg.try_get<RobotSubcomponentComponent>(e);
            const auto* pc = reg.try_get<ParentComponent>(e);
            if (!sc || sc->robotId != 7) allTagged = false;
            if (!pc || pc->parent != theRoot) allParented = false;
        }
        RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
        LiveRobot* got = rr ? rr->get(7) : nullptr;
        const bool ndofOk = got && got->chain.nq() == g.dof();
        const bool rootOk = (roots == 1) && (theRoot != entt::null) && rb && rb->root == theRoot;

        // NEG-CTRL: a NON-member body (demo B3, no committed joint) is NOT claimed by robot 7.
        bool negOk = true;
        for (int i = 0; i < int(g.bodies.size()); ++i) {
            if (mem.count(i)) continue;
            const int eid = g.bodies[i].entity; if (eid < 0) continue;
            const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
            const auto* sc = reg.try_get<RobotSubcomponentComponent>(e);
            if (sc && sc->robotId == 7) negOk = false;
        }

        const bool step2 = rootOk && allTagged && allParented && ndofOk && negOk;
        pass = pass && step2;
        printf("[robotowner]   STEP2 roots=%d root!=null=%s members=%d tagged=%s parented=%s chain.nq=%d==dof(%d)=%s\n",
               roots, (theRoot != entt::null) ? "yes" : "no", memberBodies, allTagged ? "yes" : "no",
               allParented ? "yes" : "no", got ? got->chain.nq() : -1, g.dof(), ndofOk ? "yes" : "no");
        printf("[robotowner]   STEP2 NEG-CTRL non-member body NOT claimed by robot 7: %s  %s\n",
               negOk ? "REJECTS(non-vacuous)" : "VACUOUS!", step2 ? "OK" : "FAIL");
    }

    // ---- STEP 3: Robot FK -> viz writeback (delta-from-rest); Robot is the viz source ----
    {
        Scene scene;
        krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
        krs::rbuild::spawnGraphBodies(scene, g, -1);
        LiveRobot* rb = instantiateFromGraph(scene, g, 3);
        auto& reg = scene.getRegistry();
        bool moved = false, matchExpected = false, negRest = false;
        double moveAmt = 0.0, expErr = 1e9, restErr = 1e9;
        if (rb && rb->ndof() >= 1 && !rb->linkEntities[0].empty()) {
            rb->useRobotFkViz = true;
            const entt::entity link0 = rb->linkEntities[0][0];
            const Eigen::Matrix4d Trest = eigFromTransform(reg.get<TransformComponent>(link0));
            rb->q = Eigen::VectorXd::Zero(rb->ndof()); rb->q[0] = 0.6;     // drive DOF0
            writeBackRobotViz(scene, *rb);
            const Eigen::Matrix4d Tnow = eigFromTransform(reg.get<TransformComponent>(link0));
            const Eigen::Matrix4d expected = rb->model.basePlacement * poseToEig(rb->fkLinks()[0]);
            moveAmt = (Tnow - Trest).cwiseAbs().maxCoeff();     moved = moveAmt > 1e-3;
            expErr  = (Tnow - expected).cwiseAbs().maxCoeff();  matchExpected = expErr < 1e-5;
            rb->q = Eigen::VectorXd::Zero(rb->ndof());                     // NEG-CTRL: back to rest
            writeBackRobotViz(scene, *rb);
            const Eigen::Matrix4d Tback = eigFromTransform(reg.get<TransformComponent>(link0));
            restErr = (Tback - Trest).cwiseAbs().maxCoeff();    negRest = restErr < 1e-6;
        }
        const bool step3 = moved && matchExpected && negRest;
        pass = pass && step3;
        printf("[robotowner]   STEP3 viz: link0 moved=%.4f(>1e-3=%s) matchFK err=%.2e(%s) q0-restores err=%.2e(%s)  %s\n",
               moveAmt, moved ? "yes" : "no", expErr, matchExpected ? "yes" : "no",
               restErr, negRest ? "yes" : "no", step3 ? "OK" : "FAIL");
    }

    // ---- STEP 4: node command bus drains INTO LiveRobot::q (Robot owns q; PhysX follows) ----
    {
        Scene scene;
        krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
        krs::rbuild::spawnGraphBodies(scene, g, -1);
        LiveRobot* rb = instantiateFromGraph(scene, g, 0);
        auto& reg = scene.getRegistry();
        ArticulationCommandComponent& bus = reg.ctx().emplace<ArticulationCommandComponent>();
        bus.target = { 0.5f, 0.9f }; bus.driven = { 1, 0 };   // drive DOF0 only
        if (rb) rb->q = Eigen::VectorXd::Zero(rb->ndof());
        const int n = drainCommandBusIntoRobots(reg);
        const bool busOk = (n == 1) && rb && std::abs(rb->q[0] - 0.5) < 1e-6 && std::abs(rb->q[1] - 0.0) < 1e-12;
        pass = pass && busOk;
        printf("[robotowner]   STEP4 bus->q: drained=%d q=[%.3f %.3f] (q0=0.5 driven, q1=0 undriven)  %s\n",
               n, rb ? rb->q[0] : -9.0, rb ? rb->q[1] : -9.0, busOk ? "OK" : "FAIL");
    }

    // ---- STEP 7: groupByRobot nests bodies under the right robot; multi-robot; loose excluded ----
    {
        Scene scene;
        auto& reg = scene.getRegistry();
        // robot 0: demo graph; robot 1: a second demo graph -> two named roots.
        krs::rbuild::RobotGraph g0 = krs::rbuild::buildDemoGraph();
        krs::rbuild::spawnGraphBodies(scene, g0, -1);
        instantiateFromGraph(scene, g0, 0);
        krs::rbuild::RobotGraph g1 = krs::rbuild::buildDemoGraph();
        krs::rbuild::spawnGraphBodies(scene, g1, -1);
        instantiateFromGraph(scene, g1, 1);
        // a loose named object (no robot affiliation)
        const entt::entity loose = reg.create();
        reg.emplace<TagComponent>(loose, std::string("LooseCube"));

        const SceneGrouping sg = groupByRobot(reg);
        const bool twoRobots = (sg.robots.size() == 2);
        bool idsOk = false, bodiesOwned = true, looseOk = false;
        if (twoRobots) {
            idsOk = ((sg.robots[0].robotId == 0 && sg.robots[1].robotId == 1) ||
                     (sg.robots[0].robotId == 1 && sg.robots[1].robotId == 0));
            for (const auto& rb : sg.robots) {
                if (rb.bodies.empty()) bodiesOwned = false;
                for (auto e : rb.bodies) {
                    const auto* sc = reg.try_get<RobotSubcomponentComponent>(e);
                    if (!sc || sc->robotId != rb.robotId) bodiesOwned = false;
                }
            }
        }
        // loose object present in loose, and NOT under any robot
        for (auto e : sg.loose) if (e == loose) looseOk = true;
        bool looseNotInRobot = true;
        for (const auto& rb : sg.robots) for (auto e : rb.bodies) if (e == loose) looseNotInRobot = false;

        const bool step7 = twoRobots && idsOk && bodiesOwned && looseOk && looseNotInRobot;
        pass = pass && step7;
        printf("[robotowner]   STEP7 groupByRobot: robots=%zu ids=%s bodiesOwned=%s loose=%s(notInRobot=%s)  %s\n",
               sg.robots.size(), idsOk ? "yes" : "no", bodiesOwned ? "yes" : "no",
               looseOk ? "yes" : "no", looseNotInRobot ? "yes" : "no", step7 ? "OK" : "FAIL");
    }

    // ---- STEP 8: live joint-LIMIT edit re-clamps the drive (the editable-FANUC path) ----
    // onApplyLimitLive() writes new [lo,hi] into model.joints[k] then rebuild()s; the new
    // limit must govern subsequent commands. NEG-CTRL: under the wide limit the same
    // command is accepted unclamped, so the clamp is a genuine effect of the edit.
    {
        LiveRobot lr;
        Joint j; j.type = krs::dyn::JType::Revolute; j.member = true;
        j.qLower = -3.0; j.qUpper = 3.0; j.axis = Eigen::Vector3d::UnitZ();
        lr.model.joints.push_back(j);
        lr.model.nLinks = 1;
        lr.rebuild();
        lr.setCommandedQ((Eigen::VectorXd(1) << 2.5).finished());
        const double wide = lr.q[0];                       // wide limit -> 2.5 accepted (NEG-CTRL)
        lr.model.joints[0].qLower = -1.0; lr.model.joints[0].qUpper = 1.0;   // the edit
        lr.rebuild();                                      // DOF unchanged -> q preserved
        lr.setCommandedQ((Eigen::VectorXd(1) << 2.5).finished());
        const double narrow = lr.q[0];                     // new limit -> clamped to 1.0
        const bool step8 = std::abs(wide - 2.5) < 1e-9 && std::abs(narrow - 1.0) < 1e-9;
        pass = pass && step8;
        printf("[robotowner]   STEP8 live-limit-edit: wide cmd2.5->%.3f ; after narrow[-1,1] cmd2.5->%.3f (clamped)  %s\n",
               wide, narrow, step8 ? "OK" : "FAIL");
    }

    // ---- STEP 9: a LiveRobot round-trips through an editable RobotGraph with IDENTICAL FK ----
    // buildGraphFromLiveRobot -> toRobot -> a fresh LiveRobot must reproduce the same FK at
    // arbitrary q. This is the keystone that lets the FANUC be an editable graph without the
    // pose drifting on a schema<->graph round-trip.
    {
        LiveRobot a;
        auto mkJ = [](Eigen::Vector3d axis, Eigen::Vector3d ptree, double lo, double hi) {
            Joint j; j.member = true; j.type = krs::dyn::JType::Revolute;
            j.axis = axis.normalized(); j.ptree = ptree; j.qLower = lo; j.qUpper = hi; return j;
        };
        a.model.joints = { mkJ({0,0,1},{0,0,0.2},-2,2), mkJ({0,1,0},{0.3,0,0},-1,1), mkJ({0,1,0},{0.4,0,0},-2,2) };
        a.model.nLinks = 4;
        a.rebuild();
        { std::vector<krs::dyn::Pose> p; a.chain.fk(Eigen::VectorXd::Zero(a.ndof()), p);
          a.restLinkWorld.assign(a.ndof(), Eigen::Matrix4d::Identity());
          for (int k = 0; k < a.ndof(); ++k) a.restLinkWorld[k] = a.model.basePlacement * poseToEig(p[k]);
          a.linkEntities.assign(a.ndof(), {}); }

        krs::rbuild::RobotGraph g = buildGraphFromLiveRobot(a);
        LiveRobot b; b.model = g.toRobot(); b.rebuild();

        const int nq = a.ndof();
        bool dofOk = (b.ndof() == nq) && (int(g.bodies.size()) == nq + 1) && (int(g.joints.size()) == nq);
        double maxErr = 0;
        for (int t = 0; t < 5 && dofOk; ++t) {
            Eigen::VectorXd q(nq); for (int i = 0; i < nq; ++i) q[i] = 0.3 * (i + 1) - 0.1 * t;
            std::vector<krs::dyn::Pose> pa, pb; a.chain.fk(q, pa); b.chain.fk(q, pb);
            for (int k = 0; k < nq; ++k)
                maxErr = std::max(maxErr,
                    (a.model.basePlacement * poseToEig(pa[k]) - b.model.basePlacement * poseToEig(pb[k])).cwiseAbs().maxCoeff());
        }
        const bool step9 = dofOk && maxErr < 1e-9;
        pass = pass && step9;
        printf("[robotowner]   STEP9 graph round-trip: bodies=%zu joints=%zu dofOk=%s FKerr=%.2e  %s\n",
               g.bodies.size(), g.joints.size(), dofOk ? "yes" : "no", maxErr, step9 ? "OK" : "FAIL");
    }

    printf("[robotowner] %s\n", pass ? "ALL PASS (LiveRobot is the q owner; FK exact; clamp + driven-only + live-limit-edit + graph-round-trip)"
                                     : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE MANIP-OPS -- the parent-rigid / child-IK / split / merge manipulation model, on a real scene.
bool runManipOpsGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[manip] GATE MANIP-OPS -- rigid translate moves all links; IK converges+clamps; split->2 robots; merge->1\n");

    Scene scene; auto& reg = scene.getRegistry();
    // 4-body planar arm along +x, 3 revolute-Z joints (DOF 3).
    krs::rbuild::RobotGraph g; g.base = 0;
    for (int i = 0; i < 4; ++i) { krs::rbuild::RBBody b; b.name = "L" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity(); b.placement(0, 3) = double(i); g.bodies.push_back(b); }
    for (int i = 0; i < 3; ++i) { krs::rbuild::RBJoint j; j.parent = i; j.child = i + 1;
        j.type = krs::rbuild::JType::Revolute; j.axisDir = { 0, 0, 1 };
        j.axisPos = glm::vec3(float(i + 1), 0, 0); j.orthonormalizeFrame(); g.addJoint(j); }
    g.robotId = 0;
    auto& gctx = reg.ctx().emplace<krs::rbuild::RobotGraph>(g);
    krs::rbuild::spawnGraphBodies(scene, gctx, 0);
    LiveRobot* lr = instantiateFromGraph(scene, gctx, 0);
    if (!lr || lr->ndof() < 3) { printf("[manip] FAIL: setup (ndof=%d)\n", lr ? lr->ndof() : -1); return false; }
    lr->useRobotFkViz = true;
    const int leaf = lr->ndof() - 1;

    auto firstEnt = [&](int k) -> entt::entity {
        return (k >= 0 && k < int(lr->linkEntities.size()) && !lr->linkEntities[k].empty())
               ? lr->linkEntities[k][0] : entt::null; };
    auto entX = [&](entt::entity e) { return reg.valid(e) ? double(reg.get<TransformComponent>(e).translation.x) : 0.0; };

    // --- TRANSLATE: every link entity shifts by the world delta ---
    const entt::entity e0 = firstEnt(0), eL = firstEnt(leaf);
    const double x0b = entX(e0), xLb = entX(eL);
    translateRobot(scene, 0, Eigen::Vector3d(1, 0, 0));
    const double dx0 = entX(e0) - x0b, dxL = entX(eL) - xLb;
    const bool transOk = std::abs(dx0 - 1.0) < 1e-5 && std::abs(dxL - 1.0) < 1e-5;
    translateRobot(scene, 0, Eigen::Vector3d(-1, 0, 0));   // undo
    printf("[manip]   translate: link0 dx=%.4f leaf dx=%.4f (want 1.0 each)  %s\n", dx0, dxL, transOk ? "PASS" : "FAIL");

    // --- IK: record a reachable leaf target (FK of q*), reset, drag back to it ---
    Eigen::VectorXd qstar(lr->ndof()); for (int i = 0; i < lr->ndof(); ++i) qstar[i] = 0.3;
    lr->q = qstar; writeBackRobotViz(scene, *lr);
    const krs::dyn::Pose ps = lr->chain.bodyPose(lr->q, leaf);
    const Eigen::Vector3d tgt = (lr->model.basePlacement * Eigen::Vector4d(ps.p.x(), ps.p.y(), ps.p.z(), 1.0)).head<3>();
    lr->q.setZero(); writeBackRobotViz(scene, *lr);
    const bool ikConv = ikDragLink(scene, 0, leaf, tgt);
    const krs::dyn::Pose pa = lr->chain.bodyPose(lr->q, leaf);
    const Eigen::Vector3d reached = (lr->model.basePlacement * Eigen::Vector4d(pa.p.x(), pa.p.y(), pa.p.z(), 1.0)).head<3>();
    const double ikErr = (reached - tgt).norm();
    const bool ikOk = ikErr < 1e-3;
    printf("[manip]   IK drag to reachable target: converged=%s pos-err=%.2e m (<1e-3)  %s\n",
           ikConv ? "yes" : "no", ikErr, ikOk ? "PASS" : "FAIL");

    // --- ADVERSARIAL: an unreachable target must not diverge/NaN, and q must stay limit-clamped ---
    lr->q.setZero(); writeBackRobotViz(scene, *lr);
    ikDragLink(scene, 0, leaf, Eigen::Vector3d(100, 100, 100));
    bool finite = true, clamped = true;
    for (int i = 0; i < int(lr->q.size()); ++i) {
        if (!std::isfinite(lr->q[i])) finite = false;
        if (std::abs(lr->q[i] - lr->clampDof(i, lr->q[i])) > 1e-9) clamped = false;
    }
    const bool advOk = finite && clamped;
    printf("[manip]   adversarial unreachable IK: q finite=%s within-limits=%s  %s\n",
           finite ? "yes" : "no", clamped ? "yes" : "no", advOk ? "PASS" : "FAIL");
    lr->q.setZero(); writeBackRobotViz(scene, *lr);

    // --- SPLIT: cut joint 1 (body1-body2) -> base{0,1}(dof1) + branch{2,3}(dof1) as a new robot ---
    int newId = -1;
    const bool splitRan = splitRobotAtJoint(scene, 0, 1, &newId);
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
    LiveRobot* lrBase = rr ? rr->get(0) : nullptr;
    LiveRobot* lrBranch = (rr && newId >= 0) ? rr->get(newId) : nullptr;
    const bool splitOk = splitRan && lrBase && lrBranch && newId > 0
                      && lrBase->ndof() == 1 && lrBranch->ndof() == 1;
    printf("[manip]   split joint1: newRobot=%d baseDof=%d branchDof=%d (want 1/1)  %s\n",
           newId, lrBase ? lrBase->ndof() : -1, lrBranch ? lrBranch->ndof() : -1, splitOk ? "PASS" : "FAIL");

    // --- TRANSFORM the detached branch by a RIGID rotation+translation; every branch link moves by T ---
    bool xformOk = false;
    if (lrBranch && newId > 0) {
        entt::entity be = entt::null;
        for (auto& v : lrBranch->linkEntities) if (!v.empty()) { be = v[0]; break; }
        Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
        T.block<3, 3>(0, 0) = Eigen::AngleAxisd(0.7, Eigen::Vector3d::UnitZ()).toRotationMatrix();
        T(0, 3) = 0.4; T(1, 3) = -0.2;
        if (reg.valid(be)) {
            const Eigen::Matrix4d before = eigFromTransform(reg.get<TransformComponent>(be));
            transformRobot(scene, newId, T);
            const Eigen::Matrix4d after = eigFromTransform(reg.get<TransformComponent>(be));
            xformOk = ((T * before) - after).cwiseAbs().maxCoeff() < 1e-4;
        }
        printf("[manip]   transformRobot(branch, rot0.7+trans): branch link moved by T exactly=%s  %s\n",
               xformOk ? "yes" : "no", xformOk ? "PASS" : "FAIL");
    }

    // --- MERGE: fold the (now-moved) branch back into the base at base-body 1 -> 1 robot, DOF 3 ---
    krs::rbuild::RBJoint cj; cj.type = krs::rbuild::JType::Revolute; cj.axisDir = { 0, 0, 1 };
    cj.axisPos = glm::vec3(2, 0, 0); cj.orthonormalizeFrame();
    const bool mergeRan = (newId > 0) && mergeRobots(scene, 0, newId, 1, cj);
    LiveRobot* lrM = rr ? rr->get(0) : nullptr;
    const bool childGone = !(rr && rr->get(newId));
    const bool mergeOk = mergeRan && lrM && lrM->ndof() == 3 && childGone;
    printf("[manip]   merge: mergedDof=%d (want 3) childRobotGone=%s  %s\n",
           lrM ? lrM->ndof() : -1, childGone ? "yes" : "no", mergeOk ? "PASS" : "FAIL");

    const bool pass = transOk && ikOk && advOk && splitOk && xformOk && mergeOk;
    printf("[manip] %s\n", pass ? "ALL PASS (rigid translate+rotate move all links; IK converges+clamps; split->2 robots; move; merge->1)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::robot
