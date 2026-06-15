// NodeGraphGate.cpp -- Phase 5 GATE ND: prove the visual node graph is wired to the live backend,
// headlessly. The Physics nodes already mutate the ECS; the missing piece was a SOURCE feeding them
// the live registry. SceneContextNode (Phase 5) is that bridge. This gate builds real node graphs,
// injects a Scene, evaluates compute(), and asserts the BACKEND changed -- with disconnected-node and
// wrong-type negative controls so a pass can't be vacuous.

#include "BridgeNodes.hpp"
#include "PhysicsNodes.hpp"
#include "NodeFactory.hpp"
#include "Node.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdio>
#include <string>

namespace krs::nodes {

namespace {

template<typename T>
void feed(Node& n, const std::string& port, const T& v) {
    PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk);
}

// Copy a source node's output packet into a destination node's input -- a real graph connection.
bool wire(Node& src, const std::string& outName, Node& dst, const std::string& inName) {
    for (const auto& p : src.getPorts())
        if (p.direction == Port::Direction::Output && p.name == outName && p.packet.has_value()) {
            dst.setInput(inName, *p.packet); return true;
        }
    return false;
}

// True if an input port currently holds NO packet (used to make "disconnected" an explicit precondition).
bool inputUnset(const Node& n, const std::string& port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Input && p.name == port) return !p.packet.has_value();
    return false;
}

} // namespace

bool runNodeGraphGateND()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node] GATE ND -- visual node graph wired to the live ECS backend (headless)\n");

    // ---------------- ND1: factory audit -- every registered type instantiates (M-of-M) ----------------
    // Coverage = every descriptor in the registry produces a real node. (A node may legitimately have
    // no data ports, e.g. the "Comment" annotation node, so the bar is instantiation, not port count.)
    const auto& types = NodeFactory::instance().getRegisteredNodeTypes();
    int total = 0, made = 0, withPorts = 0;
    for (const auto& kv : types) {
        ++total;
        auto n = NodeFactory::instance().createNode(kv.first);
        if (n) { ++made; if (!n->getPorts().empty()) ++withPorts; }
    }
    // NEG-CTRL: the factory must NOT fabricate an unregistered type (returns nullptr, not a blank node).
    const bool bogusNull = (NodeFactory::instance().createNode("__krs_no_such_node__") == nullptr);
    const bool nd1ok = (total > 0) && (made == total) && bogusNull;

    Scene scene;
    entt::registry& reg = scene.getRegistry();

    // The Phase 5 bridge root: a scene-injected source emitting the live registry pointer.
    NodeLibrary::SceneContextNode sc; sc.setScene(&scene); sc.process();

    // ---------------- ND2: per-node backend effect through the bridge ----------------
    const entt::entity e = reg.create(); reg.emplace<RigidBodyComponent>(e);     // velocity (0,0,0)
    NodeLibrary::SetLinearVelocityNode sv;
    const bool wired = wire(sc, "Registry", sv, "Registry");                     // bridge: scene -> node
    feed(sv, "Entity", e);
    feed(sv, "Velocity", glm::vec3(1.0f, 2.0f, 3.0f));
    sv.process();
    const glm::vec3 got = reg.get<RigidBodyComponent>(e).linearVelocity;
    const bool nd2ok = wired && got == glm::vec3(1.0f, 2.0f, 3.0f);

    // ND2 NEG-CTRL (disconnected node): same node, Registry NOT wired -> backend untouched. The
    // "disconnected" precondition is asserted explicitly (the Registry input holds no packet).
    const entt::entity e2 = reg.create(); reg.emplace<RigidBodyComponent>(e2);
    NodeLibrary::SetLinearVelocityNode svDisc;
    feed(svDisc, "Entity", e2);
    feed(svDisc, "Velocity", glm::vec3(9.0f, 9.0f, 9.0f));
    svDisc.process();
    const bool ndDisc = inputUnset(svDisc, "Registry")
                     && reg.get<RigidBodyComponent>(e2).linearVelocity == glm::vec3(0.0f);

    // ---------------- ND3: graph -> robot round-trip (joint angle -> REAL link transform in the ECS) ----
    // A two-node graph: SetJointAngle writes the canonical joint angle, then RevoluteLinkFkNode (a real
    // ENGINE node that CONSUMES JointComponent.currentPosition) writes the moving link's TransformComponent.
    // `moved` is then read straight off the link's ECS transform -- not a formula in the test -- so it is
    // the graph that moved the robot link.
    const glm::vec3 jOrigin(0.3f, 0.0f, 0.0f), jAxis(0.0f, 0.0f, 1.0f);
    const glm::vec3 jRest = jOrigin + glm::vec3(0.5f, 0.0f, 0.0f);
    const entt::entity j = reg.create(); reg.emplace<JointComponent>(j);          // currentPosition 0
    const entt::entity link = reg.create();
    reg.emplace<TransformComponent>(link).translation = jRest;                    // link at rest
    NodeLibrary::SetJointAngleNode sj;
    wire(sc, "Registry", sj, "Registry"); feed(sj, "Entity", j); feed(sj, "Angle", 0.7f); sj.process();
    const double jPos = reg.get<JointComponent>(j).currentPosition;
    NodeLibrary::RevoluteLinkFkNode fk;
    wire(sc, "Registry", fk, "Registry");
    feed(fk, "Joint Entity", j); feed(fk, "Link Entity", link);
    feed(fk, "Origin", jOrigin); feed(fk, "Axis", jAxis); feed(fk, "Rest Point", jRest);
    fk.process();
    const glm::vec3 linkPos = reg.get<TransformComponent>(link).translation;      // READ FROM ECS
    const double moved = double(glm::length(linkPos - jRest));                     // ECS-derived motion
    const bool nd3ok = (std::abs(jPos - 0.7) < 1e-4) && (moved > 0.1);

    // ND3 NEG-CTRL (command can't reach the robot): disconnected SetJointAngle -> joint stays 0, so even a
    // fully-wired FK node leaves the link at rest. Frozen-robot: no command -> no joint -> no link motion.
    const entt::entity j2 = reg.create(); reg.emplace<JointComponent>(j2);
    const entt::entity link2 = reg.create(); reg.emplace<TransformComponent>(link2).translation = jRest;
    NodeLibrary::SetJointAngleNode sjDisc;
    feed(sjDisc, "Entity", j2); feed(sjDisc, "Angle", 0.7f); sjDisc.process();     // Registry NOT wired
    NodeLibrary::RevoluteLinkFkNode fk2;
    wire(sc, "Registry", fk2, "Registry");
    feed(fk2, "Joint Entity", j2); feed(fk2, "Link Entity", link2);
    feed(fk2, "Origin", jOrigin); feed(fk2, "Axis", jAxis); feed(fk2, "Rest Point", jRest);
    fk2.process();
    const double moved2 = double(glm::length(reg.get<TransformComponent>(link2).translation - jRest));
    const bool nd3Disc = inputUnset(sjDisc, "Registry")
                      && reg.get<JointComponent>(j2).currentPosition == 0.0 && moved2 < 1e-9;

    // ---------------- ND4: type safety -- isolate the TYPE as the only cause of no-op ----------------
    // First confirm the IDENTICAL bridge wiring DOES write with the correct type, then flip ONLY the
    // Velocity type to float on a fresh entity -> rejected. A pass thus uniquely implicates type rejection
    // (not a silently-failed wire / entity), and proves no crash on the bad cast.
    const entt::entity e4ok = reg.create(); reg.emplace<RigidBodyComponent>(e4ok);
    NodeLibrary::SetLinearVelocityNode svOk;
    wire(sc, "Registry", svOk, "Registry"); feed(svOk, "Entity", e4ok); feed(svOk, "Velocity", glm::vec3(4.0f, 4.0f, 4.0f));
    svOk.process();
    const bool ctrlWrites = reg.get<RigidBodyComponent>(e4ok).linearVelocity == glm::vec3(4.0f, 4.0f, 4.0f);
    const entt::entity e4 = reg.create(); reg.emplace<RigidBodyComponent>(e4);
    NodeLibrary::SetLinearVelocityNode svType;
    wire(sc, "Registry", svType, "Registry"); feed(svType, "Entity", e4); feed(svType, "Velocity", 5.0f); // a float
    svType.process();
    const bool nd4ok = ctrlWrites && reg.get<RigidBodyComponent>(e4).linearVelocity == glm::vec3(0.0f);

    const bool pass = nd1ok && nd2ok && ndDisc && nd3ok && nd3Disc && nd4ok;
    printf("[node]   ND1 factory audit: %d/%d registered types instantiate (%d with data ports); "
           "unknown-id->nullptr=%s  %s\n",
           made, total, withPorts, bogusNull ? "yes" : "NO!", nd1ok ? "PASS" : "FAIL");
    printf("[node]   ND2 backend effect: scene->SceneContext->SetLinearVelocity -> ECS vel=(%.0f,%.0f,%.0f)  %s ; "
           "disconnected node (Registry unset) -> vel unchanged %s\n",
           got.x, got.y, got.z, nd2ok ? "PASS" : "FAIL", ndDisc ? "PASS" : "FAIL!");
    printf("[node]   ND3 graph->robot: node set joint=%.3f rad -> FK node moved link %.3f m in ECS (>0.1)  %s ; "
           "disconnected cmd -> joint 0, link moved %.1e (rest) %s\n",
           jPos, moved, nd3ok ? "PASS" : "FAIL", moved2, nd3Disc ? "PASS" : "FAIL!");
    printf("[node]   ND4 type safety: same wiring writes w/ vec3 (%s), but a float is rejected -> vel unchanged  %s\n",
           ctrlWrites ? "yes" : "NO!", nd4ok ? "PASS" : "FAIL");
    printf("[node] %s\n", pass ? "ALL PASS (graph drives live ECS + a real FK node moves the link; disconnected/wrong-type neg-ctrls hold)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
