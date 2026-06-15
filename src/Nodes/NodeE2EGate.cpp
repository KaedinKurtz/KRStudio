// NodeE2EGate.cpp -- Phase 5 GATE NODE-E2E: a real canvas program built from the new node types drives
// the live robot through a non-trivial motion, with EVERY stage's value asserted (the causal-chain
// discipline) and the glass robot showing the planned config. The graph is
//   t -> gen_sine -> math_affine (potentiometer offset) -> joint command -> live robot link.
// NEG-CTRL: severing any node breaks the chain at EXACTLY that stage (firstBreak localizes the cut).

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "MqttNodes.hpp"        // krs::nodes namespace + runMqttNodeGate decl neighbourhood
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "FanucArticulation.hpp"
#include "ArticulationSpec.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <memory>

namespace krs::nodes {

namespace {
constexpr double TAU = 6.283185307179586;

void feedD(Node& n, const char* port, double v) { PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk); }
double outD(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value())
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
    return std::nan("");
}
bool wire(Node& src, const char* outName, Node& dst, const char* inName) {
    for (const auto& p : src.getPorts())
        if (p.direction == Port::Direction::Output && p.name == outName && p.packet.has_value()) {
            dst.setInput(inName, *p.packet); return true; }
    return false;
}
double linkAngle(const std::array<float, 7>& p, const glm::quat& rest) {
    const glm::quat q(p[6], p[3], p[4], p[5]); const glm::quat d = q * glm::inverse(rest);
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, double(std::abs(d.w)))));
}
} // namespace

bool runNodeE2EGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-e2e] GATE NODE-E2E -- canvas program (t->sine->affine->joint->robot) drives the live robot\n");

    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    const int nDof = int(spec.joints.size());
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(spec); sim.play();
    if (sim.articDofCount() != nDof) { sim.stop(); printf("[node-e2e] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    const glm::quat restQ(sim.articLinkPoses()[0][6], sim.articLinkPoses()[0][3], sim.articLinkPoses()[0][4], sim.articLinkPoses()[0][5]);

    // a glass robot marker entity (driven at the PLANNED commanded angle).
    auto& reg = scene.getRegistry();
    const entt::entity glass = reg.create(); reg.emplace<TransformComponent>(glass); reg.emplace<GlassComponent>(glass);

    // chain params
    const double t = 0.30, freq = 1.0, amp = 0.40, phase = 0.0, gain = 1.0, offset = 0.20;
    const double e1 = amp * std::sin(TAU * freq * t + phase);   // expected sine
    const double e2 = e1 * gain + offset;                       // expected commanded angle
    const double e3 = e2;                                       // expected live link angle (reaches command)

    // run the chain, severing at `sever` (0 = none, 1 = sine input, 2 = sine->affine wire, 3 = affine->robot).
    auto runChain = [&](int sever, double out[4]) {
        auto gen = NodeFactory::instance().createNode("gen_sine");
        auto aff = NodeFactory::instance().createNode("math_affine");
        gen->setParam<double>("freq", freq); gen->setParam<double>("amp", amp); gen->setParam<double>("phase", phase);
        aff->setParam<double>("gain", gain); aff->setParam<double>("offset", offset);
        // stage 1: sine
        if (sever != 1) feedD(*gen, "t", t);                    // severing 1 = no time into the generator
        gen->process(); out[0] = outD(*gen, "Out");
        // stage 2: affine (potentiometer offset)
        if (sever != 2) wire(*gen, "Out", *aff, "In");          // severing 2 = cut the gen->affine wire
        aff->process(); out[1] = outD(*aff, "Out");
        // stage 3: joint command -> live robot
        sim.setArticJointPositions(q0); sim.singleStep();       // reset
        double cmd = (sever == 3) ? 0.0 : out[1];               // severing 3 = command never reaches the robot
        std::vector<float> q(nDof, 0.0f); q[0] = float(cmd);
        sim.setArticJointPositions(q); sim.singleStep();
        out[2] = linkAngle(sim.articLinkPoses()[0], restQ);     // live link angle
        // stage 4: glass shows the PLANNED commanded angle (R_y about joint 0)
        reg.get<TransformComponent>(glass).rotation = glm::angleAxis(float(out[1]), glm::vec3(0, 1, 0));
        const glm::quat gq = reg.get<TransformComponent>(glass).rotation;
        out[3] = 2.0 * std::acos(std::min(1.0, std::max(-1.0, double(std::abs(gq.w)))));
    };

    double v[4]; runChain(0, v);
    const double exp[4] = { e1, e2, e3, e2 };
    double maxErr = 0; for (int i = 0; i < 4; ++i) maxErr = std::max(maxErr, std::abs(v[i] - exp[i]));
    const bool chainOk = maxErr < 1e-4;

    // severing each stage must break the chain at EXACTLY that stage (firstBreak == sever).
    auto firstBreak = [&](int sever) {
        double s[4]; runChain(sever, s);
        for (int i = 0; i < 4; ++i) if (std::abs(s[i] - exp[i]) > 1e-4) return i + 1;
        return 0;
    };
    const int b1 = firstBreak(1), b2 = firstBreak(2), b3 = firstBreak(3);
    const bool localizes = (b1 == 1) && (b2 == 2) && (b3 == 3);
    sim.stop();

    const bool pass = chainOk && localizes;
    printf("[node-e2e]   chain stages: sine=%.4f (exp %.4f), affine=%.4f (exp %.4f), live link=%.4f, glass=%.4f -> max err=%.2e  %s\n",
           v[0], e1, v[1], e2, v[2], v[3], maxErr, chainOk ? "PASS" : "FAIL");
    printf("[node-e2e]   NEG-CTRL sever localizes: cut sine->firstBreak=%d, cut wire->%d, cut joint->%d (want 1,2,3)  %s\n",
           b1, b2, b3, localizes ? "PASS" : "FAIL!");
    printf("[node-e2e] %s\n", pass ? "ALL PASS (canvas program drives the live robot; every stage asserted; severing localizes)"
                                    : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
