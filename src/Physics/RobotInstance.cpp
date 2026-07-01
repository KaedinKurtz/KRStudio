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
    const std::vector<krs::dyn::Pose> poses = lr.fkLinks();  // indexed by CHAIN BODY (base-excluded)
    // Index by CHAIN BODY (nbody), NOT ndof: a Fixed/ambiguous member joint makes nbody > ndof, and an
    // ndof-sized array drops every body past the Fixed one -> those solids are never FK-driven -> they
    // stay put while the chain bends = shatter. poses/restLinkWorld/linkEntityRestWorld/linkEntities all
    // share the SAME chain-body index. For an all-revolute chain nbody==ndof (bit-identical to before).
    const int nb = lr.chain.nbody();
    lr.restLinkWorld.assign(nb, Eigen::Matrix4d::Identity());
    lr.linkEntityRestWorld.assign(nb, {});
    for (int k = 0; k < nb; ++k) {
        if (k < int(poses.size())) lr.restLinkWorld[k] = lr.model.basePlacement * poseToEig(poses[k]);
        if (k >= int(lr.linkEntities.size())) continue;
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
    // Drive EVERY chain body (nbody), not just the first ndof: with a Fixed member joint nbody > ndof,
    // and the old ndof loop left every body past the Fixed one un-driven (static) -> shatter. All of
    // poses/restLinkWorld/linkEntities share the chain-body index.
    for (int k = 0; k < int(poses.size()); ++k) {
        if (k >= int(lr.restLinkWorld.size()) || k >= int(lr.linkEntities.size())) continue;
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
    // Size by CHAIN BODY count (nbody == order.size()-1), NOT ndof: a Fixed member joint makes
    // nbody > ndof, and an ndof-sized array + the old `k-1 < ndof` guard DROPPED every solid past
    // the Fixed joint from linkEntities -> those bodies were never FK-driven (static) = shatter.
    lr.linkEntities.assign(lr.chain.nbody(), {});
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
            if (k >= 1 && int(k - 1) < int(lr.linkEntities.size())) lr.linkEntities[k - 1].push_back(e);
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
        const krs::robot::Joint& mj = lr.model.joints[lr.memberJoint[k]];
        // Tree parent body: treeParent -1 = base (graph body 0); >=0 = chain idx t -> graph body t+1;
        // < -1 (unset) = serial fallback (parent = previous body k). Serial robots reconstruct
        // unchanged while a branched robot recovers its true parent.
        const int parentBody = (mj.treeParent < -1) ? k
                             : (mj.treeParent == -1) ? 0
                                                     : (mj.treeParent + 1);
        RBJoint j; j.parent = parentBody; j.child = k + 1;     // link parent -> link k+1
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
        // Preserve joint identity across the live<->graph round-trip (else selection/names reset
        // every refresh). Trust the live model's id/nodeId only when it carries a real identity
        // (id != 0, i.e. it came through toRobot); otherwise let addJoint mint a fresh one with a
        // canonical "J{dof}" name and dof-indexed nodeId on this first build.
        j.id     = mj.id;
        j.name   = !mj.name.empty() ? mj.name : ("J" + std::to_string(k));
        j.nodeId = (mj.id != 0) ? mj.nodeId : k;
        g.addJoint(j);                                   // mints identity iff still unassigned
    }
    return g;
}

// Rebuild the ctx JointNameRegistry from every live robot: each DOF contributes a {name, nodeId} ->
// {robotId, dof} mapping, so a node can drive a joint by its stable name/nodeId. Cheap (a few joints);
// called on demand by the drive path. Name/nodeId collisions across robots: last writer wins.
void rebuildJointNameRegistry(entt::registry& reg)
{
    JointNameRegistry* nrp = reg.ctx().find<JointNameRegistry>();
    if (!nrp) nrp = &reg.ctx().emplace<JointNameRegistry>();
    JointNameRegistry& nr = *nrp;
    nr.clear();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
    if (!rr) return;
    for (auto& sp : rr->robots) {
        if (!sp) continue;
        LiveRobot& lr = *sp;
        const int nd = lr.ndof();
        for (int k = 0; k < nd; ++k) {
            if (k >= int(lr.memberJoint.size())) break;
            const int mjIdx = lr.memberJoint[k];
            if (mjIdx < 0 || mjIdx >= int(lr.model.joints.size())) continue;
            const krs::robot::Joint& mj = lr.model.joints[mjIdx];
            JointRef jr; jr.robotId = lr.robotId; jr.dof = k; jr.id = mj.id; jr.nodeId = mj.nodeId; jr.name = mj.name;
            const int idx = int(nr.joints.size());
            nr.joints.push_back(jr);
            if (!jr.name.empty()) nr.byName[jr.name] = idx;
            if (jr.nodeId >= 0)   nr.byNodeId[jr.nodeId] = idx;
        }
    }
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
        // The WORLD-pose source of truth for the builder is g.bodies[].placement (buildGraphFromLiveRobot
        // sets it from restLinkWorld == the live world). We must NOT read the entity TransformComponent
        // here: for a CAD-imported FANUC link the geometry is baked to world and tc is identity (or an
        // FK-viz delta), so T * tc would collapse EVERY subtree body onto T -- the "shoves everything
        // down" bug. Drive the snap off the graph placement; reapplyGraphToRobot + writeBackRobotViz
        // then update the live viz from FK for an FK-viz robot.
        const Eigen::Matrix4d snapped = T * g.bodies[bdy].placement;
        g.bodies[bdy].placement = snapped;
        // Also move the live solids directly (covers a non-FK-viz robot, e.g. the demo, whose viz is the
        // raw TransformComponent). For an FK-viz robot this is harmlessly overwritten by writeBackRobotViz.
        std::vector<int> ids = g.bodies[bdy].extraEntities;
        ids.insert(ids.begin(), g.bodies[bdy].entity);
        for (int eid : ids) {
            if (eid < 0) continue;
            const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
            if (!reg.valid(e)) continue;
            if (auto* tc = reg.try_get<TransformComponent>(e))
                setTransformFromEig(*tc, T * eigFromTransform(*tc));
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

// Resolve the (chain body, entity slot) a member solid occupies. -1/-1 if not a member link.
static void resolveMemberSlot(const LiveRobot& lr, entt::entity e, int& body, int& slot) {
    body = -1; slot = -1;
    for (int k = 0; k < int(lr.linkEntities.size()) && body < 0; ++k)
        for (int s = 0; s < int(lr.linkEntities[k].size()); ++s)
            if (lr.linkEntities[k][s] == e) { body = k; slot = s; break; }
}

// GESTURE START: snapshot the pose + grabbed-entity/body world anchors ONCE, from the LIVE pose. The
// subsequent per-frame ikDragEntity measures its delta from these anchors, so a zero drag is an EXACT
// no-op at ANY pose -- killing the frozen-q=0-rest target that commanded the arm "forward violently"
// when it was away from home. qDragStart also seeds the hold-posture regularizer.
void beginIkDrag(Scene& scene, int robotId, entt::entity e)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return;
    LiveRobot* lr = rr->get(robotId); if (!lr || lr->ndof() <= 0) { return; }
    int body = -1, slot = -1; resolveMemberSlot(*lr, e, body, slot);
    if (body < 0 || body >= lr->chain.nbody()) { lr->dragActive = false; return; }
    lr->qDragStart = lr->q;
    lr->dragActive = true;
    lr->dragBody   = body;
    lr->dragEntityAnchorW = Eigen::Vector3d::Zero();
    lr->dragEntityAnchorR = Eigen::Matrix3d::Identity();
    if (reg.valid(e)) if (auto* tc = reg.try_get<TransformComponent>(e)) {
        lr->dragEntityAnchorW = Eigen::Vector3d(tc->translation.x, tc->translation.y, tc->translation.z);
        lr->dragEntityAnchorR = Eigen::Quaterniond(double(tc->rotation.w), double(tc->rotation.x),
                                                   double(tc->rotation.y), double(tc->rotation.z)).normalized().toRotationMatrix();
    }
    const krs::dyn::Pose p = lr->chain.bodyPose(lr->q, body);
    lr->dragBodyAnchorW = (lr->model.basePlacement * Eigen::Vector4d(p.p.x(), p.p.y(), p.p.z(), 1.0)).head<3>();
    lr->dragBodyAnchorR = lr->model.basePlacement.block<3,3>(0,0) * p.R;   // body FK world orientation @ start
}

void endIkDrag(Scene& scene, int robotId)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return;
    if (LiveRobot* lr = rr->get(robotId)) { lr->dragActive = false; lr->dragBody = -1; }
}

// IK DRAG: drag link `body` (a chain-body index) toward a world target point -- solve the DoF ABOVE it
// (DLS IK) so the chain bends to reach the goal, then re-drive the viz. Orientation is held at the
// link's current value (a positional drag). Returns true if the solver converged. GROUND TRUTH: the
// only writable robot state is a limit-clamped q, and the ONLY link-pose writer is writeBackRobotViz
// fed from FK(q) -- so a defined joint is structurally un-violable regardless of the target.
bool ikDragLink(Scene& scene, int robotId, int body, const Eigen::Vector3d& targetWorld,
                const Eigen::Matrix3d* targetWorldR)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr || lr->ndof() <= 0) return false;
    if (body < 0 || body >= lr->chain.nbody()) return false;
    const Eigen::Matrix4d invBase = lr->model.basePlacement.inverse();
    krs::dyn::Pose target;
    // POSE target (targetWorldR != null): command a world orientation for the end body -> chain base frame
    // (same world->base mapping as target.p; rigid base). Else HOLD the current orientation (positional drag).
    if (targetWorldR) target.R = invBase.block<3,3>(0,0) * (*targetWorldR);
    else              target.R = lr->chain.bodyPose(lr->q, body).R;
    const Eigen::Vector4d tw(targetWorld.x(), targetWorld.y(), targetWorld.z(), 1.0);
    target.p = (invBase * tw).head<3>();                     // world -> chain base frame (IK frame)

    // Hold-posture seed: while a gesture is active, pull undragged dofs toward the gesture-start pose
    // so the arm resists being swept to serve the target (it "tries to hold").
    const bool useHold = lr->dragActive && lr->qDragStart.size() == lr->q.size();
    const Eigen::VectorXd* qSeed = useHold ? &lr->qDragStart : nullptr;
    const double holdWeight = useHold ? 0.20 : 0.0;
    const double rotWeight  = targetWorldR ? 1.0 : 0.05;     // honor orientation for an explicit pose target

    Eigen::VectorXd q = lr->q;
    const krs::dyn::SerialChain::IKResult res =
        lr->chain.ik(target, body, q, 0.05, 200, 1e-6, qSeed, holdWeight, rotWeight);
    // Commit the CLOSEST-REACHABLE q (bestQ), NEVER the diverged final iterate: an unreachable drag
    // settles at the reachable boundary instead of flinging the arm forward. NEVER write a pose here.
    Eigen::VectorXd qc = (res.bestQ.size() == lr->q.size()) ? res.bestQ : q;
    for (int i = 0; i < int(qc.size()); ++i) qc[i] = lr->clampDof(i, qc[i]);
    if (!qc.allFinite()) return false;                       // never publish a NaN pose
    lr->q = qc;
    if (lr->useRobotFkViz) writeBackRobotViz(scene, *lr);    // the SOLE writer of member link poses
    return res.ok;
}

// 6-DoF POSE drag: command BOTH a world position and a world orientation for chain body `body`.
bool ikDragLinkPose(Scene& scene, int robotId, int body,
                    const Eigen::Vector3d& targetWorld, const Eigen::Matrix3d& targetWorldR)
{
    return ikDragLink(scene, robotId, body, targetWorld, &targetWorldR);
}

// IK DRAG by ENTITY (the production gizmo handler for a member link). Resolves the chain body + slot the
// dragged solid occupies and builds a body-FK-frame target from the grabbed entity's world drag delta.
// With a gesture active (beginIkDrag ran), the delta is measured from the LIVE gesture-start anchors so a
// zero drag is an EXACT no-op at any pose (no explosion); otherwise it falls back to the q=0 rest + the
// current FK body origin. Returns true on IK convergence; false if the entity isn't a member link.
bool ikDragEntity(Scene& scene, int robotId, entt::entity e, const Eigen::Vector3d& newEntityWorld)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr || lr->ndof() <= 0) return false;
    int body = -1, slot = -1; resolveMemberSlot(*lr, e, body, slot);
    if (body < 0 || body >= lr->chain.nbody()) return false;

    Eigen::Vector3d entityAnchor, bodyAnchor;
    if (lr->dragActive && lr->dragBody == body) {            // live gesture anchors (the correct path)
        entityAnchor = lr->dragEntityAnchorW;
        bodyAnchor   = lr->dragBodyAnchorW;
    } else {                                                 // fallback for a scripted single call
        entityAnchor = Eigen::Vector3d::Zero();
        if (body < int(lr->linkEntityRestWorld.size()) && slot >= 0 && slot < int(lr->linkEntityRestWorld[body].size()))
            entityAnchor = lr->linkEntityRestWorld[body][slot].block<3, 1>(0, 3);
        const krs::dyn::Pose p = lr->chain.bodyPose(lr->q, body);
        bodyAnchor = (lr->model.basePlacement * Eigen::Vector4d(p.p.x(), p.p.y(), p.p.z(), 1.0)).head<3>();
    }
    const Eigen::Vector3d dWorld = newEntityWorld - entityAnchor;
    return ikDragLink(scene, robotId, body, bodyAnchor + dWorld);
}

// 6-DoF POSE variant (the ROTATE-gizmo handler for a member link). Reorients the end body IN PLACE: the
// position target is the HELD end-effector control point (current FK body origin), NOT the gizmo's
// orbit-circle translation -- so rotating changes ORIENTATION, not the phantom "ghost circle" traversal.
// newEntityWorldR is the world orientation the gizmo just commanded. Returns false if e isn't a member link.
bool ikDragEntityPose(Scene& scene, int robotId, entt::entity e, const Eigen::Matrix3d& newEntityWorldR)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr || lr->ndof() <= 0) return false;
    int body = -1, slot = -1; resolveMemberSlot(*lr, e, body, slot);
    if (body < 0 || body >= lr->chain.nbody()) return false;
    Eigen::Vector3d bodyWorld; Eigen::Matrix3d entAnchorR, bodyAnchorR;
    if (lr->dragActive && lr->dragBody == body) {                 // live gesture anchors (the correct path)
        bodyWorld = lr->dragBodyAnchorW; entAnchorR = lr->dragEntityAnchorR; bodyAnchorR = lr->dragBodyAnchorR;
    } else {                                                      // fallback: entity treated as aligned with the body
        const krs::dyn::Pose p = lr->chain.bodyPose(lr->q, body);
        bodyWorld   = (lr->model.basePlacement * Eigen::Vector4d(p.p.x(), p.p.y(), p.p.z(), 1.0)).head<3>();
        bodyAnchorR = lr->model.basePlacement.block<3,3>(0,0) * p.R;
        entAnchorR  = bodyAnchorR;
    }
    // The gizmo commanded a world rotation of the grabbed SOLID (newEntityWorldR relative to its start
    // entAnchorR). Apply that SAME world delta to the body's start orientation, so the solid -- which is
    // rigidly attached to the body -- reaches the commanded orientation despite the solid<->body offset.
    const Eigen::Matrix3d deltaR = newEntityWorldR * entAnchorR.transpose();
    const Eigen::Matrix3d targetBodyR = deltaR * bodyAnchorR;
    return ikDragLinkPose(scene, robotId, body, bodyWorld, targetBodyR);
}

// The TRUE world orientation of the chain link that entity `e` belongs to = FK(q) body orientation. The
// entity TransformComponent carries only the FK delta-from-home for baked-CAD parts, so it cannot supply
// this -- the gizmo's BODY frame for a robot link must come from here. Returns false if e isn't a member.
bool linkWorldRot(Scene& scene, int robotId, entt::entity e, Eigen::Quaterniond& outWorldRot)
{
    auto& reg = scene.getRegistry();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>(); if (!rr) return false;
    LiveRobot* lr = rr->get(robotId); if (!lr) return false;
    int body = -1, slot = -1; resolveMemberSlot(*lr, e, body, slot);
    if (body < 0 || body >= lr->chain.nbody()) return false;
    const krs::dyn::Pose p = lr->chain.bodyPose(lr->q, body);
    outWorldRot = Eigen::Quaterniond(Eigen::Matrix3d(lr->model.basePlacement.block<3,3>(0,0) * p.R));
    return true;
}

// Production gizmo routing for a MEMBER chain link (shared by MainWindow::onTransformEdited AND the
// real-path gates, so the gate exercises the identical decision): a member link is ALWAYS IK-dragged.
// Rigid whole-robot translation is the ROOT entity's behavior (RobotRootComponent), never a member link
// at chain-body index 0 (that index is the first MOVING link, not the fixed base).
bool routeGizmoEdit(Scene& scene, int robotId, int body, const Eigen::Vector3d& targetWorld)
{
    if (body < 0) return false;
    return ikDragLink(scene, robotId, body, targetWorld);
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
        // JOINT GROUND TRUTH: a robot being hand-dragged owns its q for the duration of the gesture.
        // Without this guard the auto-play node graph (time->sine->drive J1) re-stomps q[J1] EVERY
        // frame while the user drags, so the arm fights the drag and never settles ("still breaking").
        if (rp->dragActive) continue;
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

        // STEP9b -- joint IDENTITY (Phase 0): buildGraphFromLiveRobot MINTS a stable id + name +
        // nodeId for every joint, and toRobot() CARRIES that identity into the derived model (so it
        // survives the round-trip). For this serial graph, g.joints[i] lowers to rr.joints[i].
        bool minted = !g.joints.empty();
        for (const auto& jj : g.joints)
            if (jj.id == 0 || jj.name.empty() || jj.nodeId < 0) minted = false;
        const krs::robot::Robot rr = g.toRobot();
        bool carried = (rr.joints.size() == g.joints.size());
        for (size_t i = 0; i < rr.joints.size() && carried; ++i)
            carried = (rr.joints[i].id == g.joints[i].id) &&
                      (rr.joints[i].name == g.joints[i].name) &&
                      (rr.joints[i].nodeId == g.joints[i].nodeId);
        const bool step9b = minted && carried;
        pass = pass && step9b;
        printf("[robotowner]   STEP9b joint identity: minted=%s carried=%s  %s\n",
               minted ? "yes" : "no", carried ? "yes" : "no", step9b ? "OK" : "FAIL");
    }

    printf("[robotowner] %s\n", pass ? "ALL PASS (LiveRobot is the q owner; FK exact; clamp + driven-only + live-limit-edit + graph-round-trip)"
                                     : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE CUT-KEEPS-DRIVABLE -- the joint-primary cut semantics. Cutting a joint splits the robot into
// TWO derived components (NOT a deleted subtree); both keep their joint names/ids, both stay drivable
// BY NAME, the cut joint's DOF is gone, and every body survives (the detached forearm is a free-floating
// controllable chain). This is the data/drivability half of the fix; rendering all components is Phase 6.
bool runCutKeepsDrivableGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[cutdrive] GATE CUT-KEEPS-DRIVABLE -- cut a joint -> two components; both keep names + stay drivable by name; nothing destroyed\n");

    Scene scene; auto& reg = scene.getRegistry();
    // 4-DOF serial robot -> joints J0..J3, bodies L0..L4.
    krs::rbuild::RobotGraph g; g.base = 0;
    for (int i = 0; i < 5; ++i) {
        krs::rbuild::RBBody b; b.name = "L" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity(); b.placement(0, 3) = double(i);
        g.bodies.push_back(b);
    }
    for (int i = 0; i < 4; ++i) {
        krs::rbuild::RBJoint j; j.parent = i; j.child = i + 1; j.type = krs::rbuild::JType::Revolute;
        j.axisDir = glm::vec3(0, 0, 1); j.orthonormalizeFrame(); g.addJoint(j);
    }
    LiveRobot* lr0 = instantiateFromGraph(scene, g, 0);
    const bool built = lr0 && lr0->ndof() == 4;
    rebuildJointNameRegistry(reg);
    const JointNameRegistry* nrB = reg.ctx().find<JointNameRegistry>();
    const bool before = nrB && nrB->findByName("J0") && nrB->findByName("J3") && nrB->joints.size() == 4;

    // CUT joint J1 (graph joint index 1): branch = subtree {2,3,4} (joints J2,J3); base keeps J0; J1 dropped.
    int newId = -1;
    const bool splitRan = splitRobotAtJoint(scene, 0, 1, &newId);

    rebuildJointNameRegistry(reg);
    const JointNameRegistry* nr = reg.ctx().find<JointNameRegistry>();
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
    const JointRef* rJ0 = nr ? nr->findByName("J0") : nullptr;
    const JointRef* rJ2 = nr ? nr->findByName("J2") : nullptr;
    const JointRef* rJ3 = nr ? nr->findByName("J3") : nullptr;
    const bool namesKept   = rJ0 && rJ2 && rJ3;
    const bool baseHasJ0   = rJ0 && rJ0->robotId == 0;
    const bool branchHasJ23= rJ2 && rJ3 && rJ2->robotId == newId && rJ3->robotId == newId;
    const bool j1Cut       = nr && !nr->findByName("J1");                  // the cut DOF is gone
    LiveRobot* base   = rr ? rr->get(0) : nullptr;
    LiveRobot* branch = rr && newId >= 0 ? rr->get(newId) : nullptr;
    const bool bothExist = base && branch && base->ndof() == 1 && branch->ndof() == 2;  // nothing destroyed

    // Drive each component BY NAME: resolve name -> (robotId,dof), move that DOF, assert the EE pose changes.
    auto drivableByName = [&](const char* nm) -> bool {
        const JointRef* jr = nr ? nr->findByName(nm) : nullptr;
        LiveRobot* t = (jr && rr) ? rr->get(jr->robotId) : nullptr;
        if (!t || !jr || jr->dof < 0 || jr->dof >= t->ndof()) return false;
        Eigen::VectorXd q0 = Eigen::VectorXd::Zero(t->ndof());
        Eigen::VectorXd q1 = q0; q1[jr->dof] = 0.5;
        const int ee = t->chain.nbody() - 1;
        const krs::dyn::Pose p0 = t->chain.bodyPose(q0, ee), p1 = t->chain.bodyPose(q1, ee);
        return (p0.p - p1.p).norm() > 1e-3 || (p0.R - p1.R).cwiseAbs().maxCoeff() > 1e-3;
    };
    const bool baseDrivable   = drivableByName("J0");
    const bool branchDrivable = drivableByName("J2") && drivableByName("J3");

    const bool pass = built && before && splitRan && namesKept && baseHasJ0 && branchHasJ23
                   && j1Cut && bothExist && baseDrivable && branchDrivable;
    printf("[cutdrive]   before: 4 joints J0..J3 on one component=%s\n", before ? "yes" : "NO");
    printf("[cutdrive]   after cut J1: J0->base(0)=%s J2,J3->branch(%d)=%s J1 gone=%s ; both exist (dof 1 & 2, nothing destroyed)=%s  %s\n",
           baseHasJ0 ? "yes" : "NO", newId, branchHasJ23 ? "yes" : "NO", j1Cut ? "yes" : "NO", bothExist ? "yes" : "NO",
           (namesKept && baseHasJ0 && branchHasJ23 && j1Cut && bothExist) ? "PASS" : "FAIL");
    printf("[cutdrive]   drivable by name: base J0=%s branch J2&J3=%s  %s\n",
           baseDrivable ? "yes" : "NO", branchDrivable ? "yes" : "NO", (baseDrivable && branchDrivable) ? "PASS" : "FAIL");
    printf("[cutdrive] %s\n", pass ? "ALL PASS (cut -> two components; names+ids kept; both drivable by name; nothing destroyed)"
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

// GATE IK-POSE (env KRS_IKPOSE_SELFTEST): the 6-DoF IK feature -- ikDragLinkPose reaches a FULL pose
// (position AND orientation); ikDragEntityPose reorients the end-effector IN PLACE (a rotate changes
// orientation, not a ghost-circle translation). Each sub-gate has a non-vacuous position-only neg-ctrl.
bool runIkPoseGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ikpose] GATE IK-POSE -- 6-DoF pose IK + rotate-reorients-in-place (not the ghost circle)\n");

    Scene scene; auto& reg = scene.getRegistry();
    // 4-body planar arm, 3 revolute-Z joints (DOF 3): fully controls (x, y, yaw) -- enough to exercise
    // BOTH the position and the orientation (yaw) channel of the 6-DoF solver.
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
    if (!lr || lr->ndof() < 3) { printf("[ikpose] FAIL: setup (ndof=%d)\n", lr ? lr->ndof() : -1); return false; }
    lr->useRobotFkViz = true;
    const int leaf = lr->ndof() - 1;
    const Eigen::Matrix3d Rb = lr->model.basePlacement.block<3,3>(0,0);
    auto leafWorld = [&](Eigen::Vector3d& p, Eigen::Matrix3d& R){ const krs::dyn::Pose ps = lr->chain.bodyPose(lr->q, leaf);
        p = (lr->model.basePlacement * Eigen::Vector4d(ps.p.x(), ps.p.y(), ps.p.z(), 1.0)).head<3>(); R = Rb * ps.R; };
    auto rotErr = [](const Eigen::Matrix3d& A, const Eigen::Matrix3d& B){ Eigen::AngleAxisd aa(A.transpose() * B); return std::abs(aa.angle()); };

    // ===== Gate 1: 6-DoF pose reach (position AND orientation) =====
    Eigen::VectorXd qstar(lr->ndof()); qstar << 0.4, -0.3, 0.5;
    lr->q = qstar; writeBackRobotViz(scene, *lr);
    Eigen::Vector3d pStar; Eigen::Matrix3d RStar; leafWorld(pStar, RStar);      // a reachable pose (FK of q*)
    lr->q.setZero(); writeBackRobotViz(scene, *lr);
    ikDragLinkPose(scene, 0, leaf, pStar, RStar);
    Eigen::Vector3d p1; Eigen::Matrix3d R1; leafWorld(p1, R1);
    const double pe1 = (p1 - pStar).norm(), re1 = rotErr(R1, RStar);
    const bool g1 = pe1 < 1e-3 && re1 < 1e-3;
    printf("[ikpose]   G1 6-DoF pose reach: posErr=%.2e rotErr=%.2e (both <1e-3)  %s\n", pe1, re1, g1 ? "PASS" : "FAIL");
    lr->q.setZero(); writeBackRobotViz(scene, *lr);
    ikDragLink(scene, 0, leaf, pStar);                                          // NEG-CTRL: position-only (holds R)
    Eigen::Vector3d p1n; Eigen::Matrix3d R1n; leafWorld(p1n, R1n);
    const bool g1neg = (p1n - pStar).norm() < 1e-3 && rotErr(R1n, RStar) > 0.05;
    printf("[ikpose]   G1 NEG-CTRL position-only: posErr=%.2e rotErr=%.2e (pos reached, orient NOT)  %s\n",
           (p1n - pStar).norm(), rotErr(R1n, RStar), g1neg ? "PASS" : "FAIL");

    // ===== Gate 2: a ROTATE reorients the EE in place -- orientation changes, control point stays =====
    lr->q << 0.3, 0.2, -0.2; writeBackRobotViz(scene, *lr);
    Eigen::Vector3d p0; Eigen::Matrix3d R0; leafWorld(p0, R0);
    const entt::entity leafE = lr->linkEntities[leaf].empty() ? entt::null : lr->linkEntities[leaf][0];
    if (leafE == entt::null) { printf("[ikpose] FAIL: no leaf entity\n"); return false; }
    beginIkDrag(scene, 0, leafE);
    const Eigen::Matrix3d dR = Eigen::AngleAxisd(0.4, Eigen::Vector3d::UnitZ()).toRotationMatrix();  // reachable yaw delta
    ikDragEntityPose(scene, 0, leafE, dR * lr->dragEntityAnchorR);              // rotate the grabbed solid by dR
    endIkDrag(scene, 0);
    Eigen::Vector3d p2; Eigen::Matrix3d R2; leafWorld(p2, R2);
    const Eigen::Matrix3d bodyTargetR = dR * R0;                                // the body should rotate by dR
    const double posHold = (p2 - p0).norm(), reAfter = rotErr(R2, bodyTargetR);
    const bool g2 = posHold < 1e-3 && reAfter < 1e-3;
    printf("[ikpose]   G2 rotate-in-place: control-point moved=%.2e (want ~0, no ghost circle) rotErr=%.2e (<1e-3)  %s\n",
           posHold, reAfter, g2 ? "PASS" : "FAIL");
    lr->q << 0.3, 0.2, -0.2; writeBackRobotViz(scene, *lr);
    ikDragLink(scene, 0, leaf, p0);                                             // NEG-CTRL: position-only holds orientation
    Eigen::Vector3d p2n; Eigen::Matrix3d R2n; leafWorld(p2n, R2n);
    const bool g2neg = rotErr(R2n, bodyTargetR) > 0.2;
    printf("[ikpose]   G2 NEG-CTRL position-only holds orientation: rotErr=%.2e (unchanged)  %s\n",
           rotErr(R2n, bodyTargetR), g2neg ? "PASS" : "FAIL");

    const bool pass = g1 && g1neg && g2 && g2neg;
    printf("[ikpose] %s\n", pass ? "ALL PASS (6-DoF pose IK reaches pos+orient; rotate reorients the EE in place; position-only neg-ctrls confirm)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::robot
