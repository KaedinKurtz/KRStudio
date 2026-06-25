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
    const float vis = 0.12f;
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
    for (int i = 0; i < 4; ++i) {
        g.bodies[i].name = "DemoLink" + std::to_string(i);
        g.bodies[i].placement = trans(0.4 * double(i), 0.0, 0.0);
    }
    // B2 & B3 share a COAXIAL bore (world axis at x=1.0 along Z): define-from-features
    // (selecting both bores) adds J2 -> DOF 2->3, frame at (1.0,0,*) dir (0,0,1).
    addBore(g.bodies[2], glm::vec3(0.2f, 0.0f, 0.0f), glm::vec3(0, 0, 1), 0.05f);   // world 0.8+0.2 = 1.0
    addBore(g.bodies[3], glm::vec3(-0.2f, 0.0f, 0.0f), glm::vec3(0, 0, 1), 0.05f);  // world 1.2-0.2 = 1.0
    // Committed serial joints J0 (B0-B1), J1 (B1-B2) -> DOF 2. Deleting J1 detaches
    // the B2(-B3 once defined) subtree intact.
    { RBJoint j; j.parent = 0; j.child = 1; j.type = JType::Revolute; j.axisDir = glm::vec3(0, 0, 1); j.axisPos = glm::vec3(0.2f, 0, 0); g.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.type = JType::Revolute; j.axisDir = glm::vec3(0, 0, 1); j.axisPos = glm::vec3(0.6f, 0, 0); g.addJoint(j); }
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

} // namespace krs::rbuild
