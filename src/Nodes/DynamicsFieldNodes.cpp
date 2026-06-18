// ===========================================================================
// Avoidance-field sprint, Phase 3 — dynamics-driven field-amplitude law node
// (krs::field). Reads the object's velocity + acceleration from the Phase-1
// catalog (krs::twin) and outputs the avoidance-field AMPLITUDE via the
// dynamicAmplitude law: scarier when accelerating-toward, less scary when
// decelerating. The Amplitude output pipes into the Phase-2 emitter's Amplitude
// input (so dynamics drive the field). Weights (vel vs accel) are node inputs.
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "Scene.hpp"
#include "PropertyCatalog.hpp"   // krs::twin catalog + publishSceneState
#include "AvoidanceField.hpp"    // krs::field::dynamicAmplitude / geometryOnlyAmplitude
#include "components.hpp"        // PointEffectorComponent (pipe assertion)

#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::field {

namespace {

class DynamicsFieldNode : public Node {
public:
    DynamicsFieldNode() {
        m_id = "dynamics_field";
        m_ports.push_back({ "Object",      { "entt::entity", "handle" }, Port::Direction::Input,  this });
        m_ports.push_back({ "VelWeight",   { "double", "unitless" },     Port::Direction::Input,  this });
        m_ports.push_back({ "AccelWeight", { "double", "unitless" },     Port::Direction::Input,  this });
        m_ports.push_back({ "Amplitude",   { "double", "unitless" },     Port::Direction::Output, this });
    }
    void compute() override {
        for (auto& p : m_ports) if (p.direction == Port::Direction::Output) p.packet.reset();
        const auto obj = getInput<entt::entity>("Object");
        if (!obj) return;
        const std::uint32_t id = std::uint32_t(entt::to_integral(*obj));
        const auto& cat = krs::twin::catalog();
        const auto* posE = cat.get(id, "position");
        const auto* velE = cat.get(id, "linearVelocity");
        const auto* accE = cat.get(id, "linearAcceleration");
        if (!posE || !velE || !accE) return;                  // object not published -> no output
        const glm::vec3 P(float(posE->v[0]), float(posE->v[1]), float(posE->v[2]));
        const glm::vec3 V(float(velE->v[0]), float(velE->v[1]), float(velE->v[2]));
        const glm::vec3 A(float(accE->v[0]), float(accE->v[1]), float(accE->v[2]));
        const glm::vec3 R = getParam<glm::vec3>("protected", glm::vec3(10, 0, 0));   // protected point
        const glm::vec3 d = R - P; const float len = glm::length(d);
        const glm::vec3 dir = len > 1e-6f ? d / len : glm::vec3(0.0f);
        const double vApp = glm::dot(V, dir);                 // closing speed (toward R)
        const double aApp = glm::dot(A, dir);                 // accel toward R (sign matters)
        const double wV = getInputD("VelWeight", 1.0);
        const double wA = getInputD("AccelWeight", 0.5);
        setOutput<double>("Amplitude", dynamicAmplitude(vApp, aApp, wV, wA, getParam<double>("base", 1.0)));
    }
};

struct DynFieldRegistrar {
    DynFieldRegistrar() {
        NodeFactory::instance().registerNodeType("dynamics_field",
            { "Dynamics Field", "Twin", "Avoidance-field amplitude from object velocity+acceleration (scarier when accelerating-toward)." },
            []() { return std::make_unique<DynamicsFieldNode>(); });
    }
};
static DynFieldRegistrar g_dynFieldRegistrar;

// gate helpers
void feedObject(Node& n, entt::entity e) {
    PortDataPacket pk; pk.data = e; pk.type = { "entt::entity", "handle" }; n.setInput("Object", pk);
}
void feedD(Node& n, const std::string& port, double v) {
    PortDataPacket pk; pk.data = v; pk.type = { "double", "unitless" }; n.setInput(port, pk);
}
double readOutD(Node& n, const std::string& port) {
    for (const auto& p : n.getPorts())
        if (p.name == port && p.direction == Port::Direction::Output && p.packet)
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
    return std::nan("");
}

// Set up the catalog so body `e` has velocity V and acceleration A (the catalog
// derives accel by finite-difference, so publish v0 = V - A*dt at t=0 then V at dt).
void publishDynamics(entt::registry& reg, entt::entity e, glm::vec3 V, glm::vec3 A) {
    const double dt = 1.0 / 240.0;
    krs::twin::AccelTracker acc;
    reg.get<RigidBodyComponent>(e).linearVelocity = V - A * float(dt);
    krs::twin::publishSceneState(krs::twin::catalog(), reg, 0.0, dt, acc);
    reg.get<RigidBodyComponent>(e).linearVelocity = V;
    krs::twin::publishSceneState(krs::twin::catalog(), reg, dt, dt, acc);
}

entt::entity mkBody(Scene& s, const char* name, glm::vec3 pos) {
    auto& reg = s.getRegistry();
    entt::entity e = reg.create();
    reg.emplace<TagComponent>(e).tag = name;
    reg.emplace<TransformComponent>(e).translation = pos;
    reg.emplace<RigidBodyComponent>(e);
    return e;
}

} // namespace

bool runFieldLawGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fieldlaw] GATE FIELD-LAW -- dynamics-driven amplitude (accel>const>decel>static) + authorable + pipe\n");
    bool allOk = true;
    const double wV = 1.0, wA = 0.5, base = 1.0, v = 2.0;

    // ---- FIELD-DYNAMICS: the can't-fake amplitude ORDERING ----
    {
        const double aAccel  = dynamicAmplitude(v, +1.0, wV, wA, base);   // accelerating toward
        const double aConst  = dynamicAmplitude(v,  0.0, wV, wA, base);   // constant velocity
        const double aDecel  = dynamicAmplitude(v, -1.0, wV, wA, base);   // decelerating
        const double aStatic = dynamicAmplitude(0.0, 0.0, wV, wA, base);  // static
        const bool ordering = aAccel > aConst && aConst > aDecel && aDecel > aStatic;
        const bool baselineOk = std::abs(aStatic - base) < 1e-9;          // static == geometry baseline

        // REAL NEG-CTRL (function): the geometry-only model IGNORES acceleration ->
        // accel == const == decel -> the strict ordering is LOST (a genuine failing model).
        const double gA = geometryOnlyAmplitude(v, +1.0, wV, wA, base);
        const double gC = geometryOnlyAmplitude(v,  0.0, wV, wA, base);
        const double gD = geometryOnlyAmplitude(v, -1.0, wV, wA, base);
        const bool negFails = (gA == gC && gC == gD) && !(gA > gC) && !(gC > gD);

        // NODE: read the FULL chain from the catalog -- accel/const/decel/static +
        // a RECEDING body -- all published together (same geometry, varying dynamics).
        krs::twin::catalog().clear();
        Scene sc; auto& reg = sc.getRegistry();
        const entt::entity ea = mkBody(sc, "accel",  glm::vec3(0));
        const entt::entity ec = mkBody(sc, "const",  glm::vec3(0));
        const entt::entity ed = mkBody(sc, "decel",  glm::vec3(0));
        const entt::entity es = mkBody(sc, "static", glm::vec3(0));
        const entt::entity er = mkBody(sc, "recede", glm::vec3(0));
        const double dt = 1.0 / 240.0; krs::twin::AccelTracker acc;
        const glm::vec3 Vt(float(v), 0, 0);   // velocity TOWARD R=(10,0,0)
        auto setV = [&](entt::entity e, glm::vec3 vv) { reg.get<RigidBodyComponent>(e).linearVelocity = vv; };
        // step 0: v0 = V - A*dt (so the step-dt finite-diff yields accel = A)
        setV(ea, Vt - glm::vec3(1, 0, 0) * float(dt)); setV(ec, Vt); setV(ed, Vt - glm::vec3(-1, 0, 0) * float(dt));
        setV(es, glm::vec3(0)); setV(er, glm::vec3(-float(v), 0, 0));
        krs::twin::publishSceneState(krs::twin::catalog(), reg, 0.0, dt, acc);
        setV(ea, Vt); setV(ec, Vt); setV(ed, Vt); setV(es, glm::vec3(0)); setV(er, glm::vec3(-float(v), 0, 0));
        krs::twin::publishSceneState(krs::twin::catalog(), reg, dt, dt, acc);

        DynamicsFieldNode node; node.setScene(&sc); node.setParam<glm::vec3>("protected", glm::vec3(10, 0, 0));
        auto nodeAmp = [&](entt::entity e, double wAw) { feedObject(node, e); feedD(node, "AccelWeight", wAw); node.process(); return readOutD(node, "Amplitude"); };
        const double nA = nodeAmp(ea, wA), nC = nodeAmp(ec, wA), nD = nodeAmp(ed, wA), nS = nodeAmp(es, wA), nR = nodeAmp(er, wA);
        const bool nodeChainOk = nA > nC && nC > nD && nD > nS
                              && std::abs(nA - aAccel) < 1e-2 && std::abs(nC - aConst) < 1e-2
                              && std::abs(nD - aDecel) < 1e-2 && std::abs(nS - base) < 1e-2;
        // "where it's GOING": a receding object (vApp<0, clamped) reads ~baseline, below the closing-constant.
        const bool recedeOk = std::abs(nR - base) < 1e-2 && nR < nC - 0.5;
        // NODE NEG-CTRL: AccelWeight=0 -> the node IGNORES acceleration -> accel==decel (ordering lost).
        const double nA0 = nodeAmp(ea, 0.0), nD0 = nodeAmp(ed, 0.0);
        const bool nodeNegOk = std::abs(nA0 - nD0) < 1e-2 && !(nA0 > nD0 + 1e-2);

        const bool ok = ordering && baselineOk && negFails && nodeChainOk && recedeOk && nodeNegOk;
        printf("[fieldlaw]   FIELD-DYNAMICS: fn %.2f>%.2f>%.2f>%.2f (ordering:%d, base:%d); geom-only accel==decel:%d; "
               "NODE chain %.2f>%.2f>%.2f>%.2f:%d; recede=%.2f~base<const:%d; node wA=0 accel==decel:%d  %s\n",
               aAccel, aConst, aDecel, aStatic, int(ordering), int(baselineOk), int(negFails),
               nA, nC, nD, nS, int(nodeChainOk), nR, int(recedeOk), int(nodeNegOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- FIELD-AUTHORABLE: the weighting drives the amplitude ----
    {
        // function: more acceleration weight -> higher amplitude (accel case), by exactly dwA*aApp.
        const double ampLo = dynamicAmplitude(v, 1.0, wV, 0.5, base);   // wA=0.5 -> 3.5
        const double ampHi = dynamicAmplitude(v, 1.0, wV, 1.0, base);   // wA=1.0 -> 4.0
        const bool predictOk = ampHi > ampLo && std::abs((ampHi - ampLo) - (1.0 - 0.5) * 1.0) < 1e-9;

        // NODE: a CONNECTED AccelWeight drives the output; an UNCONNECTED one uses
        // the default (no effect from a value that isn't wired).
        krs::twin::catalog().clear();
        Scene sc; auto& reg = sc.getRegistry();
        const entt::entity e = mkBody(sc, "o", glm::vec3(0));
        publishDynamics(reg, e, glm::vec3(float(v), 0, 0), glm::vec3(+1, 0, 0));
        DynamicsFieldNode node; node.setScene(&sc); node.setParam<glm::vec3>("protected", glm::vec3(10, 0, 0));

        feedObject(node, e); node.process();                                   // AccelWeight unconnected -> default 0.5
        const double ampDefault = readOutD(node, "Amplitude");
        feedObject(node, e); feedD(node, "AccelWeight", 0.2); node.process();  // connected low
        const double ampWLo = readOutD(node, "Amplitude");
        feedObject(node, e); feedD(node, "AccelWeight", 2.0); node.process();  // connected high
        const double ampWHi = readOutD(node, "Amplitude");
        const bool nodeAuthorOk = ampWHi > ampWLo                                   // connected weight drives it
                               && std::abs(ampDefault - dynamicAmplitude(v, 1.0, wV, 0.5, base)) < 1e-2   // unconnected == default 0.5
                               && std::abs(ampDefault - ampWHi) > 0.5;              // and clearly NOT the wired-high value

        const bool ok = predictOk && nodeAuthorOk;
        printf("[fieldlaw]   FIELD-AUTHORABLE: wA 0.5->1.0 amp %.3f->%.3f (predictable:%d); NODE connected wA 0.2->2.0 amp %.3f->%.3f:%d; "
               "unconnected wA -> default amp %.3f:%d  %s\n",
               ampLo, ampHi, int(predictOk), ampWLo, ampWHi, int(ampWHi > ampWLo), ampDefault, int(nodeAuthorOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- 3.2 PIPE: law amplitude -> emitter Amplitude -> field strength ----
    {
        krs::twin::catalog().clear();
        Scene sc; auto& reg = sc.getRegistry();
        const entt::entity e = mkBody(sc, "mover", glm::vec3(0));
        publishDynamics(reg, e, glm::vec3(float(v), 0, 0), glm::vec3(+1, 0, 0));
        DynamicsFieldNode node; node.setScene(&sc); node.setParam<glm::vec3>("protected", glm::vec3(10, 0, 0));
        feedObject(node, e); node.process();
        const double amp = readOutD(node, "Amplitude");

        auto emitter = NodeFactory::instance().createNode("field_emitter");
        bool pipeOk = false;
        if (emitter) {
            emitter->setScene(&sc); emitter->setParam<int>("type", 0);
            { PortDataPacket pk; pk.data = e; pk.type = { "entt::entity", "handle" }; emitter->setInput("Object", pk); }
            // wire the law's Amplitude output into the emitter's Amplitude input.
            for (const auto& p : node.getPorts())
                if (p.name == "Amplitude" && p.direction == Port::Direction::Output && p.packet) emitter->setInput("Amplitude", *p.packet);
            emitter->process();
            if (reg.all_of<PointEffectorComponent>(e))
                pipeOk = std::abs(reg.get<PointEffectorComponent>(e).strength - amp) < 1e-4;
        }
        printf("[fieldlaw]   PIPE law->emitter: dynamic amplitude=%.3f drives field strength (match:%d)  %s\n",
               amp, int(pipeOk), pipeOk ? "PASS" : "FAIL");
        allOk = allOk && pipeOk;
    }

    printf("[fieldlaw] %s\n", allOk ? "ALL PASS (dynamics ordering; geometry-only fails; authorable; law->emitter pipe)"
                                    : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::field
