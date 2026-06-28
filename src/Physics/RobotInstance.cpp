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

    // Named root entity. Identity TransformComponent so parenting is a transform
    // NO-OP (propagateTransforms composes world = parent * local; identity parent
    // leaves the existing absolute-world body transforms correct -- no double-apply).
    const entt::entity root = reg.create();
    reg.emplace<RobotRootComponent>(root, RobotRootComponent{ lr.name, robotId });
    reg.emplace<TagComponent>(root, lr.name);
    reg.emplace<TransformComponent>(root, glm::vec3(0.0f),
                                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    lr.root = root;

    // Map chain bodies -> graph body entities using the SAME ordering toRobot() used,
    // and stamp the real robotId + parent each member body under the root.
    const std::vector<int> order = g.chainBodyOrder();   // order[0]=base; order[k>=1]=chain body k-1
    lr.linkEntities.assign(lr.ndof(), {});
    for (size_t k = 0; k < order.size(); ++k) {
        const int bodyIdx = order[k];
        if (bodyIdx < 0 || bodyIdx >= int(g.bodies.size())) continue;
        const int eid = g.bodies[bodyIdx].entity;
        if (eid < 0) continue;
        const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
        if (!reg.valid(e)) continue;
        reg.emplace_or_replace<ParentComponent>(e, root);
        reg.emplace_or_replace<RobotSubcomponentComponent>(e, robotId);
        if (k >= 1 && int(k - 1) < lr.ndof()) lr.linkEntities[k - 1].push_back(e);
    }
    captureRobotRest(scene, lr);   // q is still 0 here -> rest = the authored pose
    return &lr;
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

    printf("[robotowner] %s\n", pass ? "ALL PASS (LiveRobot is the q owner; FK exact; clamp + driven-only honoured)"
                                     : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::robot
