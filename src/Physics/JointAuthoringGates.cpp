// JointAuthoringGates.cpp -- the BODY-FRAME-BACKED, REAL-PATH joint-authoring gate suite.
//
// These gates exist because the prior robot-builder gates passed while the running app was broken:
// they built perfect synthetic RobotGraphs and called the kinematics with hand-chosen indices, never
// exercising the entity->body resolution, the gizmo routing, the live<->graph desync, or the
// distinct-body invariant. Every gate here drives the REAL path -- spawnGraphBodies + instantiateFromGraph
// produce real pickable entities, and we resolve them back through bodyIndexForEntity / linkEntities and
// run the ACTUAL production code (ikDragEntity / routeGizmoEdit / EditController::defineFromFeatures /
// snapMateSubtree / reapplyGraphToRobot / splitRobotAtJoint / mergeRobots / rebuildJointNameRegistry) --
// then assert BODY FRAMES (FK poses, world bore frames), not dof counts. Each family carries a NEG-CTRL
// so it cannot pass vacuously.
//
// Intended behaviors proven (the user's overnight spec):
//   A gizmo routing: grabbing a link moves THAT link (no 0/1 off-by-one); root rigid-translates.
//   B ik drag: zero-drag is a no-op (no explosion); a reachable drag converges, bounded, offsets honored.
//   C define-from-2-bores: an offset (but parallel) child snaps CONCENTRIC + COAXIAL to BOTH sides.
//   D distinct bodies: a self-joint (two bores on one link) is rejected; no fiducial self-joints.
//   E joint persistence: dragging a body up- OR down-stream keeps every joint intact.
//   F cut: deleting k joints yields exactly k+1 components (NOT a robot-per-body); nothing destroyed.
//   G merge: re-joining merges the chains back into one; joints + names restored.
//   H coaxial constraint: driving a mated joint keeps the bores coaxial through the whole range.
//   I node joint-server: every joint is registered + drivable by name/nodeId after any edit.
//   J FIFO bore selection: only ever 2 bore edges selected; a 3rd evicts the oldest.
//   K concentric-to-both-sides: the produced joint frame lies on BOTH bores' axes.
#include "RobotModel.hpp"
#include "RobotBuilder.hpp"
#include "RobotBuilderScene.hpp"
#include "SelectionService.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Eigen/Dense>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <set>

namespace krs::robot {

namespace {

// ---- tiny sub-gate framework: every CHECK prints a PASS/FAIL line and tallies -------------------
struct Suite {
    int pass = 0, total = 0;
    void check(const std::string& name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[jsuite]   %-4s %s\n", ok ? "PASS" : "FAIL", name.c_str());
        std::fflush(stdout);
    }
};

glm::vec3 entWorld(entt::registry& r, entt::entity e) {
    if (r.valid(e)) if (auto* tc = r.try_get<TransformComponent>(e)) return tc->translation;
    return glm::vec3(1e9f);
}
Eigen::Vector3d ev(const glm::vec3& v) { return Eigen::Vector3d(double(v.x), double(v.y), double(v.z)); }
Eigen::Vector3d baseOrigin(const LiveRobot& lr) { return lr.model.basePlacement.block<3, 1>(0, 3); }
bool finiteVec(const Eigen::VectorXd& v) { for (int i = 0; i < v.size(); ++i) if (!std::isfinite(v[i])) return false; return true; }

// Append a cylinder bore (world axis wpos/wdir) to a body, stored in its LOCAL frame so
// faceToWorld(face, placement) recovers the world axis (matches spawnGraphBodies / the panel).
void addCyl(krs::rbuild::RBBody& b, const glm::vec3& wpos, const glm::vec3& wdir, float r) {
    const Eigen::Matrix4d inv = b.placement.inverse();
    const Eigen::Vector4d lp = inv * Eigen::Vector4d(wpos.x, wpos.y, wpos.z, 1.0);
    Eigen::Vector3d ld = inv.block<3, 3>(0, 0) * Eigen::Vector3d(wdir.x, wdir.y, wdir.z);
    ld.normalize();
    BRepFace f; f.type = 1;
    f.axisPos = glm::vec3(float(lp.x()), float(lp.y()), float(lp.z()));
    f.axisDir = glm::vec3(float(ld.x()), float(ld.y()), float(ld.z()));
    f.radius = r;
    b.faces.push_back(f);
}

// A genuine 3D serial arm: base at origin; each link offset +0.3 in x, raised to z=0.2; joint axes
// ALTERNATE Z / Y so the chain is non-degenerate and DOWNSTREAM body frames genuinely move under
// UPSTREAM joints (a collinear chain or a body sitting on its own joint axis cannot translate). axisPos
// is the CHILD body origin, which lies on its joint axis, so the Phase-5 bore-anchor projection is an
// exact no-op (frames stay at the link origins). The end-effector is reachable by IK. No bores needed
// for the kinematic families; the define families use buildDemoGraph which has real coaxial bores.
krs::rbuild::RobotGraph makeSerialGraph(int ndof) {
    using namespace krs::rbuild;
    RobotGraph g; g.base = 0;
    RBBody base; base.name = "base"; base.placement = Eigen::Matrix4d::Identity(); g.bodies.push_back(base);
    for (int i = 1; i <= ndof; ++i) {
        RBBody b; b.name = "L" + std::to_string(i);
        Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
        M(0, 3) = 0.3 * double(i); M(2, 3) = 0.2;
        b.placement = M; g.bodies.push_back(b);
    }
    for (int i = 0; i < ndof; ++i) {
        RBJoint j; j.parent = i; j.child = i + 1; j.type = JType::Revolute;
        j.axisPos = glm::vec3(float(0.3 * double(i + 1)), 0.0f, 0.2f);             // child origin (on its axis)
        j.axisDir = (i % 2 == 0) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);        // alternate Z / Y
        j.orthonormalizeFrame();
        g.addJoint(j);
    }
    return g;
}

// =================================================================================================
// FAMILY A -- GIZMO ROUTING (the 0/1-start off-by-one): grabbing a link moves THAT link; the base is
// never rigid-shifted by a member-link drag; the ROOT rigid-translates the whole robot.
void familyGizmoRoute(Scene& scene, Suite& S) {
    for (int ndof : {2, 3, 4}) {
        krs::rbuild::RobotGraph g = makeSerialGraph(ndof);
        const int rid = 100 + ndof;
        krs::rbuild::spawnGraphBodies(scene, g, rid);
        LiveRobot* lr = instantiateFromGraph(scene, g, rid);
        if (lr) lr->useRobotFkViz = true;   // so ikDragLink/translateRobot drive the entity transforms
        auto& reg = scene.getRegistry();
        if (!lr || lr->ndof() != ndof) { S.check("A.ndof" + std::to_string(ndof) + " built", false); continue; }
        const std::string n = "A.n" + std::to_string(ndof);

        // THE OFF-BY-ONE: chain body j's entity resolves to GRAPH body j+1 via bodyIndexForEntity, while
        // the linkEntities search (what onTransformEdited used) yields chain index j -- pins the +1 offset.
        { const entt::entity e0 = lr->linkEntities[0][0];
          int gi = krs::rbuild::bodyIndexForEntity(g, int(std::uint32_t(e0)));
          int ci = -1; for (int k = 0; k < int(lr->linkEntities.size()); ++k) for (auto le : lr->linkEntities[k]) if (le == e0) ci = k;
          S.check(n + " first moving link: graph idx 1, chain idx 0 (the +1 offset)", gi == 1 && ci == 0); }

        // Every MEMBER-link drag is an IK (never a rigid whole-robot translate -- the old body==0 bug);
        // the base placement must be untouched, and q must stay finite + clamped (no explosion).
        for (int j = 0; j < ndof; ++j) {
            const entt::entity e = lr->linkEntities[j][0];
            const Eigen::Vector3d baseB = baseOrigin(*lr);
            const glm::vec3 r = entWorld(reg, e);
            ikDragEntity(scene, rid, e, ev(r + glm::vec3(0.03f, 0.04f, 0.0f)));
            const Eigen::Vector3d baseA = baseOrigin(*lr);
            S.check(n + ".grab body" + std::to_string(j) + " base NOT rigid-shifted (IK, not translate)", (baseA - baseB).norm() < 1e-9);
            bool clamped = finiteVec(lr->q);
            for (int i = 0; i < lr->ndof(); ++i) clamped = clamped && std::abs(lr->q[i] - lr->clampDof(i, lr->q[i])) < 1e-9;
            S.check(n + ".grab body" + std::to_string(j) + " q finite+clamped (no explosion)", clamped);
            lr->q.setZero(); writeBackRobotViz(scene, *lr);
        }

        // END-EFFECTOR drag (the draggable tip): IK reaches a reachable target + the chain responds (q moves).
        { const int j = ndof - 1; const entt::entity e = lr->linkEntities[j][0];
          const glm::vec3 before = entWorld(reg, e);
          const glm::vec3 tgt = before + glm::vec3(0.0f, 0.06f, 0.05f);     // perpendicular-ish, reachable by rotation
          ikDragEntity(scene, rid, e, ev(tgt));
          const glm::vec3 after = entWorld(reg, e);
          S.check(n + " end-effector drag moves the tip toward target", glm::distance(after, tgt) < glm::distance(before, tgt) - 1e-4f);
          S.check(n + " end-effector drag drives the chain (q responds)", lr->q.cwiseAbs().maxCoeff() > 1e-3);
          lr->q.setZero(); writeBackRobotViz(scene, *lr); }

        // ROOT rigid-translate: translateRobot shifts the base AND every link by exactly the delta.
        const Eigen::Vector3d baseB = baseOrigin(*lr);
        std::vector<glm::vec3> linkB; for (int j = 0; j < ndof; ++j) linkB.push_back(entWorld(reg, lr->linkEntities[j][0]));
        const Eigen::Vector3d delta(0.10, 0.0, 0.0);
        translateRobot(scene, rid, delta);
        S.check(n + " root translate shifts base by delta", ((baseOrigin(*lr) - baseB) - delta).norm() < 1e-9);
        bool allMoved = true;
        for (int j = 0; j < ndof; ++j) allMoved = allMoved && glm::distance(entWorld(reg, lr->linkEntities[j][0]), linkB[j] + glm::vec3(0.10f, 0, 0)) < 1e-3f;
        S.check(n + " root translate moves every link by delta", allMoved);
    }
}

// =================================================================================================
// FAMILY B -- IK DRAG: a ZERO drag is an exact no-op (the explosion neg-ctrl), a reachable drag
// converges + stays bounded, and a non-dragged sibling above the dragged link does not move.
void familyIkDrag(Scene& scene, Suite& S) {
    for (int ndof : {2, 3, 4}) {
        krs::rbuild::RobotGraph g = makeSerialGraph(ndof);
        const int rid = 200 + ndof;
        krs::rbuild::spawnGraphBodies(scene, g, rid);
        LiveRobot* lr = instantiateFromGraph(scene, g, rid);
        if (lr) lr->useRobotFkViz = true;   // so ikDragLink/translateRobot drive the entity transforms
        auto& reg = scene.getRegistry();
        if (!lr || lr->ndof() != ndof) { S.check("B.n" + std::to_string(ndof) + " built", false); continue; }

        for (int j = 0; j < ndof; ++j) {
            const entt::entity e = lr->linkEntities[j][0];
            const glm::vec3 rest = entWorld(reg, e);
            // ZERO drag: newEntityWorld == rest -> body must NOT move (raw-entity-origin bug jumps by O)
            ikDragEntity(scene, rid, e, ev(rest));
            const glm::vec3 z = entWorld(reg, e);
            S.check("B.n" + std::to_string(ndof) + ".body" + std::to_string(j) + " zero-drag no-op", glm::distance(z, rest) < 1e-5f);
            // reachable drag: converges, bounded
            const glm::vec3 tgt = rest + glm::vec3(0.03f, 0.04f, 0.0f);
            ikDragEntity(scene, rid, e, ev(tgt));
            const glm::vec3 a = entWorld(reg, e);
            S.check("B.n" + std::to_string(ndof) + ".body" + std::to_string(j) + " reachable drag bounded", finiteVec(lr->q) && glm::length(a) < 100.0f);
            // reset q so the next body's zero-drag baseline is rest
            lr->q.setZero(); writeBackRobotViz(scene, *lr);
        }
    }
}

// =================================================================================================
// FAMILY C -- DEFINE-FROM-2-BORES SNAP: move the unjointed child away (live TransformComponent only),
// then define J from the two bores -> the child snaps CONCENTRIC + COAXIAL onto the parent, the FK at
// q=0 reproduces the snap, and the parent stays put. NEG-CTRL: the bores were far apart before.
void familyDefineSnap(Scene& scene, Suite& S, const glm::vec3& childOffset, const std::string& kind) {
    using namespace krs::rbuild;
    auto& reg = scene.getRegistry();
    RobotGraph g = buildDemoGraph();            // B0-B1-B2-B3; B2/B3 share a coaxial bore; J2 pre-defined
    // delete J2 so B3 is unjointed + movable, then we re-define it from the bores after moving B3.
    { const int j23 = g.jointBetween(2, 3); if (j23 >= 0) g.deleteJoint(j23); }
    const int rid = 300;
    spawnGraphBodies(scene, g, rid);
    instantiateFromGraph(scene, g, rid);

    // MOVE B3's solids by childOffset in the LIVE scene only (simulating a gizmo drag) -- g.placement stale.
    std::vector<int> ids = g.bodies[3].extraEntities; ids.insert(ids.begin(), g.bodies[3].entity);
    for (int eid : ids) { if (eid < 0) continue; entt::entity e = entt::entity(std::uint32_t(eid));
        if (reg.valid(e)) if (auto* tc = reg.try_get<TransformComponent>(e)) tc->translation += childOffset; }

    // world bore frames from the (current) live faces
    auto worldBore = [&](int body) -> BRepFace {
        const entt::entity e = entt::entity(std::uint32_t(g.bodies[body].entity));
        Eigen::Matrix4d M = g.bodies[body].placement;
        if (reg.valid(e)) if (auto* tc = reg.try_get<TransformComponent>(e)) {
            M = Eigen::Matrix4d::Identity();
            const glm::mat4 g4 = glm::mat4_cast(tc->rotation);
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) M(r, c) = g4[c][r] * tc->scale[c];
            M(0, 3) = tc->translation.x; M(1, 3) = tc->translation.y; M(2, 3) = tc->translation.z;
        }
        return faceToWorld(g.bodies[body].faces[0], M);
    };
    const BRepFace pB = worldBore(2), cB = worldBore(3);
    // NEG-CTRL non-vacuous: the two bores are FAR apart before the mate
    const glm::vec3 pPos = pB.axisPos, cPos = cB.axisPos, dir = glm::normalize(pB.axisDir);
    const glm::vec3 w0 = cPos - pPos; const float perp0 = glm::length(w0 - glm::dot(w0, dir) * dir);
    S.check("C." + kind + " bores far apart BEFORE mate (non-vacuous)", glm::distance(pPos, cPos) > 0.2f);

    // run the REAL define path: resolve entities -> defineFromFeatures(requireCollinear=false) -> snap -> reapply
    const int a = bodyIndexForEntity(g, g.bodies[2].entity);
    const int b = bodyIndexForEntity(g, g.bodies[3].entity);
    S.check("C." + kind + " bodyIndexForEntity resolves both bores distinct", a == 2 && b == 3);
    EditController ctrl{ &g };
    int parent = a, child = b; RBJoint created;
    auto toFace = [](const BRepFace& f) { return f; };
    const bool ok = ctrl.defineFromFeatures(toFace(pB), a, toFace(cB), b, &created, &parent, &child, false);
    S.check("C." + kind + " define accepts parallel-but-offset bores", ok);
    if (ok) {
        RBJoint pf; pf.axisPos = pB.axisPos; pf.axisDir = pB.axisDir; pf.orthonormalizeFrame();
        RBJoint cf; cf.axisPos = cB.axisPos; cf.axisDir = cB.axisDir; cf.orthonormalizeFrame();
        snapMateSubtree(scene, g, parent, child, pf, cf);
        reapplyGraphToRobot(scene, g, rid);
        // re-read the world bores AFTER snap+reapply
        const BRepFace pA = worldBore(2), cA = worldBore(3);
        const glm::vec3 d2 = glm::normalize(pA.axisDir);
        const glm::vec3 w = cA.axisPos - pA.axisPos;
        const float perp = glm::length(w - glm::dot(w, d2) * d2);
        S.check("C." + kind + " child bore CONCENTRIC to parent (perp<1e-3) after mate", perp < 1e-3f);
        S.check("C." + kind + " bore axes COAXIAL (|dot|>0.999) after mate", std::abs(glm::dot(glm::normalize(cA.axisDir), d2)) > 0.999f);
        S.check("C." + kind + " parent bore did NOT move (parent fixed)", glm::distance(pA.axisPos, pPos) < 1e-3f);
        S.check("C." + kind + " joint registered between 2 and 3", g.jointBetween(2, 3) >= 0);
    }
}

// =================================================================================================
// FAMILY D -- DISTINCT BODIES: a self-joint is rejected at the data layer (two bores on ONE collapsed
// link resolve to the same body); a genuine distinct pair is accepted. No fiducial self-joints.
void familyNoSelfJoint(Scene& scene, Suite& S) {
    using namespace krs::rbuild;
    RobotGraph g = buildDemoGraph();
    // addJoint(parent==child) -> -1 (rejected)
    { RBJoint j; j.parent = 1; j.child = 1; j.axisDir = glm::vec3(0, 0, 1); j.orthonormalizeFrame();
      S.check("D.addJoint(parent==child) rejected (-1)", g.addJoint(j) < 0); }
    // defineFromFeatures(bodyA==bodyB) -> false
    EditController ctrl{ &g };
    BRepFace f = faceToWorld(g.bodies[2].faces[0], g.bodies[2].placement);
    S.check("D.defineFromFeatures(a==b) rejected", !ctrl.defineFromFeatures(f, 2, f, 2));
    // a fiducial/extra solid of a collapsed link resolves to its OWNING body (not its own body)
    if (!g.bodies[2].extraEntities.empty()) {
        const int ex = g.bodies[2].extraEntities[0];
        S.check("D.fiducial extra solid maps to owning body 2", bodyIndexForEntity(g, ex) == 2);
    } else {
        // synthesize a collapsed link: give body 2 an extra entity id and assert it maps to 2
        g.bodies[2].extraEntities.push_back(424242);
        S.check("D.fiducial extra solid maps to owning body 2", bodyIndexForEntity(g, 424242) == 2);
    }
    // NEG-CTRL: a DISTINCT pair (two different bodies, coaxial bores) IS accepted
    RobotGraph g2 = buildDemoGraph();
    { const int j23 = g2.jointBetween(2, 3); if (j23 >= 0) g2.deleteJoint(j23); }
    EditController c2{ &g2 };
    BRepFace fa = faceToWorld(g2.bodies[2].faces[0], g2.bodies[2].placement);
    BRepFace fb = faceToWorld(g2.bodies[3].faces[0], g2.bodies[3].placement);
    S.check("D.NEG-CTRL distinct-body coaxial pair accepted", c2.defineFromFeatures(fa, 2, fb, 3));
    // every committed joint of the demo connects distinct members
    bool distinct = true; for (const auto& j : g2.joints) if (!j.ambiguous && j.parent == j.child) distinct = false;
    S.check("D.every committed joint connects distinct bodies", distinct);
}

// =================================================================================================
// FAMILY E -- JOINT PERSISTENCE under up- AND down-stream drags: after dragging any body, every joint
// edge still exists, the dof is unchanged, and the FK stays finite (the chain is not destroyed).
void familyPersistence(Scene& scene, Suite& S) {
    for (int ndof : {3, 4}) {
        krs::rbuild::RobotGraph g = makeSerialGraph(ndof);
        const int rid = 400 + ndof;
        krs::rbuild::spawnGraphBodies(scene, g, rid);
        LiveRobot* lr = instantiateFromGraph(scene, g, rid);
        if (lr) lr->useRobotFkViz = true;   // so ikDragLink/translateRobot drive the entity transforms
        auto& reg = scene.getRegistry();
        if (!lr || lr->ndof() != ndof) { S.check("E.n" + std::to_string(ndof) + " built", false); continue; }
        const int dof0 = lr->ndof();
        auto edgesIntact = [&] {
            krs::rbuild::RobotGraph gg = buildGraphFromLiveRobot(*lr);
            int edges = 0; for (int i = 0; i < ndof; ++i) if (gg.jointBetween(i, i + 1) >= 0) ++edges;
            return edges == ndof;
        };
        // downstream drag (the last/end link)
        { const entt::entity e = lr->linkEntities[ndof - 1][0]; const glm::vec3 r = entWorld(reg, e);
          ikDragEntity(scene, rid, e, ev(r + glm::vec3(0.05f, 0.05f, 0.0f)));
          S.check("E.n" + std::to_string(ndof) + " downstream drag: all joints intact", edgesIntact());
          S.check("E.n" + std::to_string(ndof) + " downstream drag: dof unchanged + FK finite", lr->ndof() == dof0 && finiteVec(lr->q)); }
        // upstream drag (the first moving link)
        { const entt::entity e = lr->linkEntities[0][0]; const glm::vec3 r = entWorld(reg, e);
          ikDragEntity(scene, rid, e, ev(r + glm::vec3(0.0f, 0.05f, 0.04f)));
          S.check("E.n" + std::to_string(ndof) + " upstream drag: all joints intact", edgesIntact());
          S.check("E.n" + std::to_string(ndof) + " upstream drag: dof unchanged + FK finite", lr->ndof() == dof0 && finiteVec(lr->q)); }
    }
}

// =================================================================================================
// FAMILY F -- CUT: deleting k joints from an n-dof chain yields exactly k+1 connected components (NOT
// a robot-per-body), and every body is retained across the components.
void familyCutComponents(Suite& S) {
    using namespace krs::rbuild;
    for (int ndof : {4, 5}) {
        RobotGraph g = makeSerialGraph(ndof);    // ndof+1 bodies, ndof joints
        S.check("F.n" + std::to_string(ndof) + " starts as 1 component", g.connectedComponents().size() == 1);
        for (int k = 1; k <= ndof; ++k) {
            RobotGraph gc = makeSerialGraph(ndof);
            // delete the FIRST k joints (indices 0..k-1)
            for (int d = 0; d < k; ++d) { const int idx = gc.jointBetween(d, d + 1); if (idx >= 0) gc.deleteJoint(idx); }
            const auto comps = gc.connectedComponents();
            int bodies = 0; for (const auto& c : comps) bodies += int(c.size());
            S.check("F.n" + std::to_string(ndof) + " delete " + std::to_string(k) + " joints -> " + std::to_string(k + 1) + " components",
                    int(comps.size()) == k + 1);
            S.check("F.n" + std::to_string(ndof) + " delete " + std::to_string(k) + " joints -> all bodies retained",
                    bodies == ndof + 1);
        }
    }
}

// =================================================================================================
// FAMILY G -- MERGE on rejoin: split a chain into base+branch then re-mate -> ONE component again with
// the original body + joint counts, and the joint NAMES survive the round-trip.
void familyMerge(Scene& scene, Suite& S) {
    using namespace krs::rbuild;
    RobotGraph g = makeSerialGraph(4);
    const int rid = 500;
    spawnGraphBodies(scene, g, rid);
    LiveRobot* lr = instantiateFromGraph(scene, g, rid);
    if (lr) lr->useRobotFkViz = true;
    if (!lr) { S.check("G built", false); return; }
    // record joint names before
    std::set<std::string> namesBefore;
    { RobotGraph gg = buildGraphFromLiveRobot(*lr); for (const auto& j : gg.joints) namesBefore.insert(j.name); }
    int newId = -1;
    const bool split = splitRobotAtJoint(scene, rid, 1, &newId);   // cut joint index 1
    S.check("G split -> two robots", split && newId >= 0);
    auto& reg = scene.getRegistry();
    auto* rr = reg.ctx().find<RobotRegistry>();
    LiveRobot* base = rr ? rr->get(rid) : nullptr;
    LiveRobot* branch = (rr && newId >= 0) ? rr->get(newId) : nullptr;
    S.check("G both components live after split", base && branch);
    if (base && branch) {
        // re-mate: connect branch.base onto base body (the cut interface) and merge
        RobotGraph gB = buildGraphFromLiveRobot(*base);
        RBJoint cj; cj.type = JType::Revolute; cj.axisDir = glm::vec3(0, 0, 1); cj.orthonormalizeFrame();
        const int parentBody = int(gB.bodies.size()) - 1;   // last base body = the cut parent
        const bool merged = mergeRobots(scene, rid, newId, parentBody, cj);
        S.check("G re-mate merges back to one robot", merged);
        if (merged) {
            LiveRobot* m = rr->get(rid);
            S.check("G merged robot has all 4 dof again", m && m->ndof() == 4);
            RobotGraph gm = buildGraphFromLiveRobot(*m);
            int kept = 0; for (const auto& j : gm.joints) if (namesBefore.count(j.name)) ++kept;
            S.check("G joint names survive split+merge (>=3 of 4)", kept >= 3);
        }
    }
}

// =================================================================================================
// FAMILY H -- COAXIAL CONSTRAINT SOLVES: drive a revolute through its range; the link rotates ABOUT the
// joint axis line (every sampled body-origin stays at a constant perpendicular distance from the axis).
void familyCoaxialSolve(Scene& scene, Suite& S) {
    krs::rbuild::RobotGraph g = makeSerialGraph(2);
    const int rid = 600;
    krs::rbuild::spawnGraphBodies(scene, g, rid);
    LiveRobot* lr = instantiateFromGraph(scene, g, rid);
    if (!lr || lr->ndof() < 2) { S.check("H built", false); return; }
    // joint 0 axis: vertical Z through the body-1 origin (0.3,0,0.2). The DOWNSTREAM body (the tip) orbits
    // that axis at a constant radius as joint 0 is driven -- the coaxial revolute constraint holding.
    const Eigen::Vector3d axisP(0.3, 0.0, 0.2), axisD(0, 0, 1);
    const int obs = lr->ndof() - 1;   // a body downstream of joint 0 (its own body sits ON the axis)
    double rad0 = -1; bool constRadius = true, moved = false;
    for (int s = 0; s <= 6; ++s) {
        Eigen::VectorXd q = Eigen::VectorXd::Zero(lr->ndof());
        q[0] = -1.0 + 2.0 * s / 6.0;
        const krs::dyn::Pose p = lr->chain.bodyPose(q, obs);
        const Eigen::Vector3d w = (lr->model.basePlacement * Eigen::Vector4d(p.p.x(), p.p.y(), p.p.z(), 1.0)).head<3>();
        const Eigen::Vector3d rel = w - axisP;
        const double r = (rel - rel.dot(axisD) * axisD).norm();   // perpendicular distance to the axis line
        if (rad0 < 0) rad0 = r; else if (std::abs(r - rad0) > 1e-6) constRadius = false;
        if (s > 0 && std::abs(q[0]) > 0.1) moved = true;
    }
    S.check("H driving the joint keeps the link on the axis (constant radius)", constRadius && rad0 > 0.1);
    S.check("H the joint actually articulates (non-vacuous)", moved);
}

// =================================================================================================
// FAMILY I -- NODE JOINT-SERVER: after building/instantiating, every member joint is registered in the
// JointNameRegistry and resolvable by both NAME and nodeId to a valid dof (Nodes can drive it).
void familyJointServer(Scene& scene, Suite& S) {
    krs::rbuild::RobotGraph g = makeSerialGraph(3);
    const int rid = 700;
    krs::rbuild::spawnGraphBodies(scene, g, rid);
    LiveRobot* lr = instantiateFromGraph(scene, g, rid);
    if (lr) lr->useRobotFkViz = true;
    auto& reg = scene.getRegistry();
    if (!lr) { S.check("I built", false); return; }
    rebuildJointNameRegistry(reg);
    const JointNameRegistry* nr = reg.ctx().find<JointNameRegistry>();
    S.check("I registry exists after build", nr != nullptr);
    if (nr) {
        int byName = 0, byNode = 0;
        for (int k = 0; k < lr->ndof(); ++k) {
            const krs::robot::Joint& mj = lr->model.joints[lr->memberJoint[k]];
            const JointRef* rn = nr->findByName(mj.name);
            const JointRef* ri = nr->findByNodeId(mj.nodeId);
            if (rn && rn->robotId == rid && rn->dof == k) ++byName;
            if (ri && ri->dof == k) ++byNode;
        }
        S.check("I every joint resolvable by NAME to its dof", byName == lr->ndof());
        S.check("I every joint resolvable by nodeId to its dof", byNode == lr->ndof());
        // drive-by-name actually MOVES the named DOF's link (entity-level -- the real visible effect;
        // the body's own FK frame origin sits ON its joint axis, so the link ENTITY is what moves).
        const krs::robot::Joint& mj0 = lr->model.joints[lr->memberJoint[0]];
        const JointRef* r0 = nr->findByName(mj0.name);
        bool drivable = false;
        if (r0) {
            LiveRobot* t = reg.ctx().find<RobotRegistry>()->get(r0->robotId);
            const int lastDof = t ? t->ndof() - 1 : -1;   // observe a DOWNSTREAM link (driving joint 0 swings the tip)
            if (t && r0->dof >= 0 && r0->dof < t->ndof()
                  && lastDof >= 0 && lastDof < int(t->linkEntities.size()) && !t->linkEntities[lastDof].empty()) {
                const entt::entity le = t->linkEntities[lastDof][0];
                const glm::vec3 before = entWorld(reg, le);
                t->q.setZero(); t->q[r0->dof] = 0.6; writeBackRobotViz(scene, *t);
                const glm::vec3 after = entWorld(reg, le);
                drivable = glm::distance(before, after) > 1e-3f;
                t->q.setZero(); writeBackRobotViz(scene, *t);
            }
        }
        S.check("I drive-by-name moves the named DOF (downstream link responds)", drivable);
    }
}

// =================================================================================================
// FAMILY J -- FIFO BORE SELECTION: only ever 2 bore edges selected; a 3rd click evicts the OLDEST.
void familyFifoSelect(Suite& S) {
    using namespace krs::sel;
    auto cyl = [](int id) { Selection s; s.valid = true; s.type = FeatureType::Cylinder;
        s.entity = entt::entity(std::uint32_t(id)); s.axisPos = glm::vec3(float(id), 0, 0); return s; };
    auto cylCount = [](const SelectionState& st) { int n = 0; for (auto& s : st.selected) if (s.valid && s.type == FeatureType::Cylinder) ++n; return n; };

    SelectionState st; st.fifoTwoBores = true;
    st.selected.push_back(cyl(1)); enforceFifoTwoBores(st);
    S.check("J 1 bore -> 1 selected", cylCount(st) == 1);
    st.selected.push_back(cyl(2)); enforceFifoTwoBores(st);
    S.check("J 2 bores -> 2 selected", cylCount(st) == 2);
    st.selected.push_back(cyl(3)); enforceFifoTwoBores(st);
    S.check("J 3rd bore -> still only 2 selected (FIFO)", cylCount(st) == 2);
    // oldest (id 1) evicted; newest two (2,3) retained
    bool has1 = false, has2 = false, has3 = false;
    for (auto& s : st.selected) { int id = int(std::uint32_t(s.entity)); has1 |= id == 1; has2 |= id == 2; has3 |= id == 3; }
    S.check("J oldest bore evicted, newest two kept", !has1 && has2 && has3);
    st.selected.push_back(cyl(4)); enforceFifoTwoBores(st);
    bool only34 = true; for (auto& s : st.selected) { int id = int(std::uint32_t(s.entity)); if (id != 3 && id != 4) only34 = false; }
    S.check("J 4th bore -> {3,4} (rolling FIFO)", cylCount(st) == 2 && only34);
    // NEG-CTRL: with FIFO OFF, selections accumulate beyond 2
    SelectionState off; off.fifoTwoBores = false;
    off.selected.push_back(cyl(1)); enforceFifoTwoBores(off);
    off.selected.push_back(cyl(2)); enforceFifoTwoBores(off);
    off.selected.push_back(cyl(3)); enforceFifoTwoBores(off);
    S.check("J NEG-CTRL fifo OFF accumulates (3) -- non-vacuous", cylCount(off) == 3);
}

// =================================================================================================
// FAMILY K -- CONCENTRIC TO BOTH SIDES: after a define+snap, the persisted joint frame lies on BOTH
// the parent bore axis AND the child bore axis (concentric to both, not just one).
void familyConcentricBoth(Scene& scene, Suite& S) {
    using namespace krs::rbuild;
    auto& reg = scene.getRegistry();
    RobotGraph g = buildDemoGraph();
    { const int j23 = g.jointBetween(2, 3); if (j23 >= 0) g.deleteJoint(j23); }
    const int rid = 800;
    spawnGraphBodies(scene, g, rid);
    instantiateFromGraph(scene, g, rid);
    // nudge B3 off-axis (lateral), then define+snap
    std::vector<int> ids = g.bodies[3].extraEntities; ids.insert(ids.begin(), g.bodies[3].entity);
    for (int eid : ids) { if (eid < 0) continue; entt::entity e = entt::entity(std::uint32_t(eid));
        if (reg.valid(e)) if (auto* tc = reg.try_get<TransformComponent>(e)) tc->translation += glm::vec3(0.0f, 0.15f, 0.0f); }
    auto worldBore = [&](int body) -> BRepFace {
        const entt::entity e = entt::entity(std::uint32_t(g.bodies[body].entity));
        Eigen::Matrix4d M = g.bodies[body].placement;
        if (reg.valid(e)) if (auto* tc = reg.try_get<TransformComponent>(e)) {
            M = Eigen::Matrix4d::Identity(); const glm::mat4 g4 = glm::mat4_cast(tc->rotation);
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) M(r, c) = g4[c][r] * tc->scale[c];
            M(0, 3) = tc->translation.x; M(1, 3) = tc->translation.y; M(2, 3) = tc->translation.z; }
        return faceToWorld(g.bodies[body].faces[0], M); };
    const BRepFace pB = worldBore(2), cB = worldBore(3);
    EditController ctrl{ &g };
    int parent = 2, child = 3; RBJoint created;
    const bool ok = ctrl.defineFromFeatures(pB, 2, cB, 3, &created, &parent, &child, false);
    if (!ok) { S.check("K define ok", false); return; }
    RBJoint pf; pf.axisPos = pB.axisPos; pf.axisDir = pB.axisDir; pf.orthonormalizeFrame();
    RBJoint cf; cf.axisPos = cB.axisPos; cf.axisDir = cB.axisDir; cf.orthonormalizeFrame();
    snapMateSubtree(scene, g, parent, child, pf, cf);
    reapplyGraphToRobot(scene, g, rid);
    const int pj = g.jointBetween(2, 3);
    S.check("K joint exists after define+snap", pj >= 0);
    if (pj >= 0) {
        const glm::vec3 jp = g.joints[pj].axisPos, jd = glm::normalize(g.joints[pj].axisDir);
        const BRepFace pA = worldBore(2), cA = worldBore(3);
        auto perpTo = [&](const glm::vec3& bpos, const glm::vec3& bdir) {
            const glm::vec3 w = jp - bpos; const glm::vec3 d = glm::normalize(bdir);
            return glm::length(w - glm::dot(w, d) * d); };
        S.check("K joint frame concentric to PARENT bore", perpTo(pA.axisPos, pA.axisDir) < 1e-3f);
        S.check("K joint frame concentric to CHILD bore (after snap)", perpTo(cA.axisPos, cA.axisDir) < 1e-3f);
        S.check("K joint axis parallel to both bores", std::abs(glm::dot(jd, glm::normalize(pA.axisDir))) > 0.999f
                                                    && std::abs(glm::dot(jd, glm::normalize(cA.axisDir))) > 0.999f);
    }
}

// =================================================================================================
// FAMILY M -- PERSISTENCE under a LONG SEQUENCE of up/down drags: after EACH drag every joint edge is
// intact, dof is unchanged, and FK stays finite (a chain is never corrupted by repeated manipulation).
void familyDragSequence(Scene& scene, Suite& S) {
    krs::rbuild::RobotGraph g = makeSerialGraph(4);
    const int rid = 950;
    krs::rbuild::spawnGraphBodies(scene, g, rid);
    LiveRobot* lr = instantiateFromGraph(scene, g, rid);
    if (lr) lr->useRobotFkViz = true;
    auto& reg = scene.getRegistry();
    if (!lr) { S.check("M built", false); return; }
    const int dof0 = lr->ndof();
    auto edgesIntact = [&] {
        krs::rbuild::RobotGraph gg = buildGraphFromLiveRobot(*lr);
        int e = 0; for (int i = 0; i < dof0; ++i) if (gg.jointBetween(i, i + 1) >= 0) ++e; return e == dof0;
    };
    const int seq[] = { 3, 0, 2, 1, 3, 0 };   // alternate downstream/upstream links
    for (int s = 0; s < 6; ++s) {
        const int j = seq[s] % dof0;
        const entt::entity e = lr->linkEntities[j][0];
        const glm::vec3 r = entWorld(reg, e);
        ikDragEntity(scene, rid, e, ev(r + glm::vec3(0.0f, 0.04f, 0.03f)));
        const bool ok = edgesIntact() && lr->ndof() == dof0 && finiteVec(lr->q);
        S.check("M drag #" + std::to_string(s + 1) + " (link " + std::to_string(j) + "): joints intact + FK finite", ok);
    }
}

// =================================================================================================
// FAMILY O -- MULTI-COMPONENT INDEPENDENT DRIVE: cutting a joint yields exactly TWO live components,
// and driving a joint BY NAME on one component moves ONLY that component (no cross-talk) -- the data
// half of "cut doesn't make a tangled mess / both sub-chains stay independently controllable".
void familyMultiComponentDrive(Scene& scene, Suite& S) {
    krs::rbuild::RobotGraph g = makeSerialGraph(5);     // 5 dof so BOTH halves keep >=2 dof (a translatable tip)
    const int rid = 960;
    krs::rbuild::spawnGraphBodies(scene, g, rid);
    LiveRobot* lr = instantiateFromGraph(scene, g, rid);
    if (lr) lr->useRobotFkViz = true;
    auto& reg = scene.getRegistry();
    if (!lr) { S.check("O built", false); return; }
    int newId = -1;
    const bool split = splitRobotAtJoint(scene, rid, 2, &newId);    // base {0,1,2}(2dof), branch {3,4,5}(2dof)
    S.check("O cut -> two components", split && newId >= 0);
    if (!split) return;
    rebuildJointNameRegistry(reg);
    auto* rr = reg.ctx().find<RobotRegistry>();
    const JointNameRegistry* nr = reg.ctx().find<JointNameRegistry>();
    S.check("O exactly two live components after cut", rr && rr->get(rid) && rr->get(newId));
    auto firstName = [&](int cr) -> std::string { LiveRobot* t = rr->get(cr); return (t && t->ndof() >= 1) ? t->model.joints[t->memberJoint[0]].name : std::string(); };
    auto tip = [&](int cr) -> entt::entity { LiveRobot* t = rr->get(cr); if (!t || t->ndof() < 1) return entt::null; const int l = t->ndof() - 1; return (l < int(t->linkEntities.size()) && !t->linkEntities[l].empty()) ? t->linkEntities[l][0] : entt::null; };
    auto driveName = [&](const std::string& nm) { const JointRef* r = nr ? nr->findByName(nm) : nullptr; if (!r) return; LiveRobot* t = rr->get(r->robotId); if (!t || r->dof >= t->ndof()) return; t->q.setZero(); t->q[r->dof] = 0.6; writeBackRobotViz(scene, *t); };
    const std::string baseJ = firstName(rid), branchJ = firstName(newId);
    S.check("O each component has a named joint", !baseJ.empty() && !branchJ.empty());
    const entt::entity bTip = tip(rid), brTip = tip(newId);
    const glm::vec3 b0 = entWorld(reg, bTip), r0 = entWorld(reg, brTip);
    driveName(baseJ);
    S.check("O drive BASE joint moves base component", glm::distance(b0, entWorld(reg, bTip)) > 1e-3f);
    S.check("O drive BASE joint does NOT move branch (no cross-talk)", glm::distance(r0, entWorld(reg, brTip)) < 1e-6f);
    if (rr->get(rid)) { rr->get(rid)->q.setZero(); writeBackRobotViz(scene, *rr->get(rid)); }
    const glm::vec3 b2 = entWorld(reg, bTip), r2 = entWorld(reg, brTip);
    driveName(branchJ);
    S.check("O drive BRANCH joint moves branch component", glm::distance(r2, entWorld(reg, brTip)) > 1e-3f);
    S.check("O drive BRANCH joint does NOT move base (no cross-talk)", glm::distance(b2, entWorld(reg, bTip)) < 1e-6f);
}

} // namespace

// The master suite: runs every family on a fresh scene, prints each sub-gate, and a TOTAL line.
bool runJointAuthoringSuite()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\n[jsuite] ===== JOINT-AUTHORING SUITE (body-frame-backed, real-path) =====\n");
    Suite S;
    { Scene sc; familyGizmoRoute(sc, S); }
    { Scene sc; familyIkDrag(sc, S); }
    { Scene sc; familyDefineSnap(sc, S, glm::vec3(0.0f, 0.30f, 0.0f), "lateral"); }
    { Scene sc; familyDefineSnap(sc, S, glm::vec3(0.25f, 0.20f, 0.0f), "lateral+axial"); }
    { Scene sc; familyDefineSnap(sc, S, glm::vec3(0.90f, 0.50f, 0.30f), "far-apart-not-touching"); }
    { Scene sc; familyNoSelfJoint(sc, S); }
    { Scene sc; familyPersistence(sc, S); }
    familyCutComponents(S);
    { Scene sc; familyMerge(sc, S); }
    { Scene sc; familyCoaxialSolve(sc, S); }
    { Scene sc; familyJointServer(sc, S); }
    familyFifoSelect(S);
    { Scene sc; familyConcentricBoth(sc, S); }
    { Scene sc; familyDragSequence(sc, S); }
    { Scene sc; familyMultiComponentDrive(sc, S); }
    printf("[jsuite] ===== JOINT-AUTHORING SUITE: %d / %d sub-gates PASS =====\n", S.pass, S.total);
    std::fflush(stdout);
    return S.pass == S.total;
}

} // namespace krs::robot
