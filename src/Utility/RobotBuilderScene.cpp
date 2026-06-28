#include "RobotBuilderScene.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "PrimitiveBuilders.hpp"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <Eigen/Dense>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace krs::rbuild {

namespace {

glm::vec3 placePos(const Eigen::Matrix4d& M) {
    return glm::vec3(float(M(0, 3)), float(M(1, 3)), float(M(2, 3)));
}
glm::quat placeRot(const Eigen::Matrix4d& M) {
    Eigen::Matrix3d R = M.block<3, 3>(0, 0);
    Eigen::Quaterniond q(R); q.normalize();
    return glm::quat(float(q.w()), float(q.x()), float(q.y()), float(q.z()));
}
Eigen::Matrix4d trans(double x, double y, double z) {
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    M(0, 3) = x; M(1, 3) = y; M(2, 3) = z; return M;
}
// Add a cylindrical bore face (analytic, in the body's LOCAL frame). The render
// bridge stores these on the entity; picking world-transforms them by the entity
// placement -> the same WORLD bore the gate derives via faceToWorld.
void addBore(RBBody& b, const glm::vec3& axisPosLocal, const glm::vec3& axisDirLocal, float radius) {
    BRepFace f;
    f.type = 1;                              // cylinder
    f.axisPos = axisPosLocal;
    f.axisDir = glm::normalize(axisDirLocal);
    f.normal = f.axisDir;
    f.radius = radius;
    b.faces.push_back(f);
}

// Spawn ONE body as a renderable, pickable entity (const body). Shared by the
// main-scene bridge (records entity) and the preview mirror (read-only).
entt::entity spawnOneBody(entt::registry& reg, const RBBody& b, int robotId) {
    entt::entity e = reg.create();
    auto& mesh = reg.emplace<RenderableMeshComponent>(e);
    std::vector<uint32_t> idx;
    buildUnitCube(mesh.vertices, idx);
    const glm::vec3 vis = b.visSize;   // per-axis size -> proper-looking links, not equal cubes
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (auto& v : mesh.vertices) { v.position *= vis; mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
    mesh.indices.assign(idx.begin(), idx.end());
    mesh.hasUVs = false; mesh.hasTangents = false;
    mesh.aabbMin = mn; mesh.aabbMax = mx;
    reg.emplace<TransformComponent>(e, placePos(b.placement), placeRot(b.placement), glm::vec3(1.0f));
    reg.emplace<TriPlanarMaterialTag>(e);
    reg.emplace<TagComponent>(e, b.name.empty() ? std::string("RBody") : b.name);
    reg.emplace<RobotSubcomponentComponent>(e, robotId);
    if (!b.faces.empty()) { BRepFaceComponent fc; fc.faces = b.faces; reg.emplace<BRepFaceComponent>(e, std::move(fc)); }
    return e;
}

} // namespace

RobotGraph buildDemoGraph() {
    RobotGraph g; g.base = 0;
    g.bodies.resize(4);
    // A small but RECOGNISABLE articulated arm (base -> shoulder -> upper arm -> wrist),
    // not 4 identical cubes. Per-link visSize gives proper proportions; the joint
    // structure + the coaxial B2/B3 bores (for define-from-features) are preserved so
    // the krs::rbuild gates still pass.
    g.bodies[0].name = "Base";      g.bodies[0].placement = trans(0.0, 0.00, 0.00); g.bodies[0].visSize = { 0.32f, 0.10f, 0.32f };
    g.bodies[1].name = "Shoulder";  g.bodies[1].placement = trans(0.0, 0.30, 0.00); g.bodies[1].visSize = { 0.16f, 0.45f, 0.16f };
    g.bodies[2].name = "UpperArm";  g.bodies[2].placement = trans(0.0, 0.62, 0.22); g.bodies[2].visSize = { 0.16f, 0.16f, 0.50f };
    g.bodies[3].name = "Wrist";     g.bodies[3].placement = trans(0.0, 0.62, 0.58); g.bodies[3].visSize = { 0.18f, 0.18f, 0.18f };

    // B2 & B3 share a COAXIAL bore (world axis at (0,0.62,0.40) along X): selecting both
    // bores defines a revolute J2 -> DOF 2->3, frame at that axis.
    addBore(g.bodies[2], glm::vec3(0.0f, 0.0f, 0.18f),  glm::vec3(1, 0, 0), 0.05f);  // world (0,0.62,0.40)
    addBore(g.bodies[3], glm::vec3(0.0f, 0.0f, -0.18f), glm::vec3(1, 0, 0), 0.05f);  // world (0,0.62,0.40)

    // Committed serial joints: J0 base-yaw (Y) at the base, J1 shoulder-pitch (X). DOF 2.
    { RBJoint j; j.parent = 0; j.child = 1; j.type = JType::Revolute; j.axisDir = glm::vec3(0, 1, 0); j.axisPos = glm::vec3(0, 0.20f, 0); g.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.type = JType::Revolute; j.axisDir = glm::vec3(1, 0, 0); j.axisPos = glm::vec3(0, 0.55f, 0); g.addJoint(j); }
    return g;
}

void spawnGraphBodies(Scene& scene, RobotGraph& g, int robotId) {
    auto& reg = scene.getRegistry();
    for (int i = 0; i < int(g.bodies.size()); ++i)
        g.bodies[i].entity = int(spawnOneBody(reg, g.bodies[i], robotId));
}

int mirrorGraphIntoScene(Scene& preview, const RobotGraph& g, int robotId) {
    auto& reg = preview.getRegistry();
    int n = 0;
    for (const auto& b : g.bodies) { spawnOneBody(reg, b, robotId); ++n; }
    return n;
}

int bodyIndexForEntity(const RobotGraph& g, int entity) {
    for (int i = 0; i < int(g.bodies.size()); ++i)
        if (g.bodies[i].entity == entity) return i;
    return -1;
}

glm::vec3 turntableCameraPos(const glm::vec3& base, float dist, float elev, float angleRad) {
    return base + glm::vec3(dist * std::cos(angleRad), elev, dist * std::sin(angleRad));
}

void syncRobotTagsToMembership(Scene& scene, const RobotGraph& g) {
    auto& reg = scene.getRegistry();
    for (int i = 0; i < int(g.bodies.size()); ++i) {
        const int eid = g.bodies[i].entity;
        if (eid < 0) continue;
        const entt::entity e = entt::entity(static_cast<std::uint32_t>(eid));
        if (!reg.valid(e)) continue;
        if (g.isMember(i))                              // owned by the chain -> locked
            reg.emplace_or_replace<RobotSubcomponentComponent>(e, g.robotId);
        else if (reg.all_of<RobotSubcomponentComponent>(e))  // detached/unjointed -> grabbable
            reg.remove<RobotSubcomponentComponent>(e);
    }
}

// ===========================================================================
// GATE BRIDGE-RENDER -- the user's explicit requirement: the demo graph's bodies
// must actually become RENDERED entities, not just exist in memory. The on-screen
// pixels are OPERATOR-VISUAL-CONFIRM, but "is the body a renderable scene entity"
// is gateable: assert each body is a valid entity carrying a non-empty render mesh
// + transform + robot tag, and that the bridge CREATED real entities (didn't fake ids).
// ===========================================================================
bool runRobotBuilderBridgeGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE BRIDGE-RENDER -- demo graph bodies become renderable scene entities (graph-in-memory is NOT enough)\n");

    Scene scene;
    auto& reg = scene.getRegistry();
    RobotGraph g = buildDemoGraph();
    const int nBodies = int(g.bodies.size());

    // Before the bridge: nothing is rendered (every body.entity == -1).
    int unspawned = 0; for (const auto& b : g.bodies) if (b.entity < 0) ++unspawned;
    const bool preNone = (unspawned == nBodies);

    spawnGraphBodies(scene, g, /*robotId*/ 7);

    // After the bridge: EVERY body is a valid entity carrying the render + robot-tag
    // components, with a non-empty mesh.
    int rendered = 0;
    for (const auto& b : g.bodies) {
        if (b.entity < 0) continue;
        const entt::entity e = entt::entity(static_cast<std::uint32_t>(b.entity));
        if (!reg.valid(e)) continue;
        const bool hasMesh  = reg.all_of<RenderableMeshComponent>(e);
        const bool hasXform = reg.all_of<TransformComponent>(e);
        const bool hasTag   = reg.all_of<RobotSubcomponentComponent>(e);
        const bool meshOk   = hasMesh && !reg.get<RenderableMeshComponent>(e).vertices.empty();
        if (hasMesh && hasXform && hasTag && meshOk) ++rendered;
    }
    const bool allRendered = (rendered == nBodies);

    // The bridge created real entities (the count of robot-tagged entities == nBodies).
    int liveRobotEntities = 0;
    for (auto e : reg.view<RobotSubcomponentComponent>()) { (void)e; ++liveRobotEntities; }
    const bool createdReal = (liveRobotEntities == nBodies);

    // entity<->body round-trip resolves.
    bool roundTrip = true;
    for (int i = 0; i < nBodies; ++i)
        if (bodyIndexForEntity(g, g.bodies[i].entity) != i) roundTrip = false;

    // NEG-CTRL 1: an un-spawned graph (skip the bridge) renders nothing.
    RobotGraph gNo = buildDemoGraph();
    int renderedNo = 0; for (const auto& b : gNo.bodies) if (b.entity >= 0) ++renderedNo;
    const bool negUnspawned = (renderedNo == 0);

    // NEG-CTRL 2: a FAKE bridge that sets entity to a bogus id without creating it
    // -> not valid in the registry (proves we check rendering, not just a set field).
    RobotGraph gFake = buildDemoGraph();
    for (auto& b : gFake.bodies) b.entity = 999999;
    int renderedFake = 0;
    for (const auto& b : gFake.bodies) {
        const entt::entity e = entt::entity(static_cast<std::uint32_t>(b.entity));
        if (reg.valid(e)) ++renderedFake;
    }
    const bool negFake = (renderedFake == 0);

    const bool pass = preNone && allRendered && createdReal && roundTrip && negUnspawned && negFake;

    printf("[rbuild]   bridge: %d/%d bodies are valid renderable entities (mesh+xform+robot-tag); created %d real entities; entity<->body round-trip %s  %s\n",
           rendered, nBodies, liveRobotEntities, roundTrip ? "ok" : "BAD",
           (allRendered && createdReal && roundTrip) ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL un-spawned graph renders %d (want 0); fake-id bridge valid %d (want 0)  %s\n",
           renderedNo, renderedFake, (negUnspawned && negFake) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (demo graph bodies are genuinely rendered entities)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE SUBTREE-GRAB-INVOKED (Phase 3) -- after a mid-chain delete, the proven
// SUBTREE-DETACH leaves the downstream sub-assembly intact; the viewport drag
// lock-out reads RobotSubcomponentComponent, and syncRobotTagsToMembership() makes
// that tag track LIVE membership. So a detached body becomes grabbable (untagged)
// while still-attached bodies stay locked (tagged); re-mate re-locks. The on-screen
// drag is OPERATOR-VISUAL-CONFIRM; the grabbability LOGIC (driven by genuine
// detachment) is gated. NEG-CTRL: a grab on a still-attached/robot-tagged body fails.
// ===========================================================================
bool runRobotSubtreeGrabGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE SUBTREE-GRAB-INVOKED -- detached subtree is grabbable; still-attached bodies stay locked; re-mate re-locks\n");

    Scene scene;
    auto& reg = scene.getRegistry();
    RobotGraph g = buildDemoGraph();
    spawnGraphBodies(scene, g, 0);

    // Define J2 (B2-B3) so the chain is B0-B1-B2-B3 (members 0..3).
    EditController ctrl{ &g };
    const BRepFace wB2 = faceToWorld(g.bodies[2].faces[0], g.bodies[2].placement);
    const BRepFace wB3 = faceToWorld(g.bodies[3].faces[0], g.bodies[3].placement);
    ctrl.defineFromFeatures(wB2, 2, wB3, 3);
    syncRobotTagsToMembership(scene, g);

    // grabbable mirrors the ViewportWidget lock-out: a valid entity that is NOT
    // robot-tagged is free to drag.
    auto grabbable = [&](int bodyIdx) {
        const entt::entity e = entt::entity(static_cast<std::uint32_t>(g.bodies[bodyIdx].entity));
        return reg.valid(e) && !reg.all_of<RobotSubcomponentComponent>(e);
    };

    // Before detach: every body is a chain member -> NONE grabbable.
    const bool preLocked = !grabbable(0) && !grabbable(1) && !grabbable(2) && !grabbable(3);

    // Delete the mid joint J1 (B1-B2): SUBTREE-DETACH -> {B2,B3} come off intact.
    ctrl.deleteJoint(g.jointBetween(1, 2));
    syncRobotTagsToMembership(scene, g);

    const bool detachFree   = g.freeMoveAllowed(2) && g.freeMoveAllowed(3)
                           && !g.freeMoveAllowed(0) && !g.freeMoveAllowed(1);
    const bool grabDetached = grabbable(2) && grabbable(3);   // the detached subtree is grabbable
    const bool lockAttached = !grabbable(0) && !grabbable(1); // NEG-CTRL: attached bodies stay locked
    // the detached subtree is still articulated (its internal J2 survived).
    const bool subtreeIntact = (g.jointBetween(2, 3) >= 0);

    // Re-mate B1-B2 (proven re-mate op = addJoint) -> membership restored -> re-locked.
    RBJoint rj; rj.parent = 1; rj.child = 2; rj.type = JType::Revolute; rj.axisDir = glm::vec3(0, 0, 1);
    g.addJoint(rj);
    syncRobotTagsToMembership(scene, g);
    const bool remateLocks = !grabbable(2) && !grabbable(3);

    const bool pass = preLocked && detachFree && grabDetached && lockAttached
                   && subtreeIntact && remateLocks;

    printf("[rbuild]   pre-detach all-locked=%s ; after mid-delete: detached{B2,B3} grabbable=%s, attached{B0,B1} locked=%s, subtree-intact=%s ; re-mate re-locks=%s  %s\n",
           preLocked ? "yes" : "no", grabDetached ? "yes" : "no", lockAttached ? "yes" : "no",
           subtreeIntact ? "yes" : "no", remateLocks ? "yes" : "no",
           pass ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL: grab on still-attached/robot-tagged body blocked=%s ; grab only after genuine detach=%s  %s\n",
           lockAttached ? "yes" : "no", (preLocked && grabDetached) ? "yes" : "no",
           (lockAttached && preLocked && grabDetached) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (grab operates on the genuinely-detached subtree; attached bodies locked; re-mate re-locks)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rbuild
