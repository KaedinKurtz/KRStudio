// ConnectControlGate.cpp -- Phase G GATE CONNECT-AND-CONTROL: a robot-control program built by WIRING
// nodes, with a value set THROUGH A RENDERED INPUT WIDGET, driven by the LIVE time source, driving the
// live robot. Chain: time_source -> gen_sine -> math_add(.A wired, .B set via its mounted spinbox) ->
// joint command -> live PhysX link. Every stage asserted (causal chain); severing a wire localizes the
// break. This is the end-to-end proof that the editor is wireable AND inputs are settable through the body.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeDelegate.hpp"
#include "ConnectControlGate.hpp"
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "FanucArticulation.hpp"
#include "ArticulationSpec.hpp"

#include <QWidget>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QThread>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cmath>
#include <vector>
#include <array>
#include <string>

namespace krs::nodes {

namespace {
constexpr double TAU = 6.283185307179586;
double outD(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value()) {
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
            try { return double(std::any_cast<float>(p.packet->data)); } catch (...) {}
        }
    return std::nan("");
}
void feedD(Node& n, const char* port, double v) { PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk); }
bool wireOut(Node& src, const char* outName, Node& dst, const char* inName) {
    for (const auto& p : src.getPorts())
        if (p.direction == Port::Direction::Output && p.name == outName && p.packet.has_value()) { dst.setInput(inName, *p.packet); return true; }
    return false;
}
double linkAngle(const std::array<float, 7>& p, const glm::quat& rest) {
    const glm::quat q(p[6], p[3], p[4], p[5]); const glm::quat d = q * glm::inverse(rest);
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, double(std::abs(d.w)))));
}
QWidget* findCtl(QWidget* body, const char* port) {
    for (QWidget* w : body->findChildren<QWidget*>())
        if (w->property("krs_input_port").isValid() && w->property("krs_input_port").toString() == QString(port)) return w;
    return nullptr;
}
} // namespace

bool runConnectControlGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[connect-ctrl] GATE CONNECT-AND-CONTROL -- wired node program (live time, widget-set value) drives the live robot\n");
    if (!QApplication::instance()) { printf("[connect-ctrl] FAIL: needs QApplication\n"); return false; }

    // live robot
    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    const int nDof = int(spec.joints.size());
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(spec); sim.play();
    if (sim.articDofCount() != nDof) { sim.stop(); printf("[connect-ctrl] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    const glm::quat restQ(sim.articLinkPoses()[0][6], sim.articLinkPoses()[0][3], sim.articLinkPoses()[0][4], sim.articLinkPoses()[0][5]);

    // the wired program. math_add is built through a NodeDelegate so its "B" can be set through the
    // RENDERED input widget (the literal), exactly as the operator would in the editor.
    auto timeNode = NodeFactory::instance().createNode("time_source");
    auto sine = NodeFactory::instance().createNode("gen_sine");
    NodeDelegate addDelegate("math_add");
    Node* add = addDelegate.backendNode();
    QWidget* addBody = addDelegate.embeddedWidget();
    if (!timeNode || !sine || !add || !addBody) { sim.stop(); printf("[connect-ctrl] FAIL: nodes\n"); return false; }
    sine->setParam<double>("freq", 0.5); sine->setParam<double>("amp", 0.4); sine->setParam<double>("phase", 0.0);

    // set add.B = 0.20 THROUGH THE MOUNTED INPUT WIDGET (not setParam/setInput) -- the rendered control.
    const double bOffset = 0.20;
    auto* bCtl = qobject_cast<QDoubleSpinBox*>(findCtl(addBody, "B"));
    if (!bCtl) { sim.stop(); printf("[connect-ctrl] FAIL: add.B widget not mounted\n"); return false; }
    bCtl->setValue(bOffset);   // -> setPortLiteral("B", 0.20)

    // run the chain at a LIVE wall-clock time; sever `cut` (0 none, 1 time->sine, 2 sine->add, 3 add->robot).
    double stageVals[4];
    auto runChain = [&](int cut, double out[4]) {
        timeNode->process(); const double tLive = outD(*timeNode, "Time");
        out[0] = tLive;
        if (cut != 1) feedD(*sine, "t", tLive); else feedD(*sine, "t", 0.0);   // sever time->sine
        sine->process(); out[1] = outD(*sine, "Out");                          // sine(t)
        // add: A wired from sine (unless severed), B = the widget literal 0.20
        add->clearInputPacket("A");                                            // start from no connection
        if (cut != 2) wireOut(*sine, "Out", *add, "A");                        // sever sine->add: leave A unset
        addDelegate.recomputeAndPropagate();
        out[2] = outD(*add, "Result");                                         // sine + 0.20
        // joint command -> live robot
        sim.setArticJointPositions(q0); sim.singleStep();
        const double cmd = (cut == 3) ? 0.0 : (std::isfinite(out[2]) ? out[2] : 0.0);
        std::vector<float> q(nDof, 0.0f); q[0] = float(cmd);
        sim.setArticJointPositions(q); sim.singleStep();
        out[3] = linkAngle(sim.articLinkPoses()[0], restQ);                    // live link angle
    };

    // the expected stage values for a given LIVE time t (recomputed per run, since wall-clock advances).
    auto expectedFor = [&](double t, double e[4]) {
        const double eSine = 0.4 * std::sin(TAU * 0.5 * t);
        e[0] = t; e[1] = eSine; e[2] = eSine + bOffset; e[3] = eSine + bOffset;
    };
    QThread::msleep(120);          // let real wall-clock advance so the live time drives a clear angle
    runChain(0, stageVals);
    const double tLive = stageVals[0];
    double exp[4]; expectedFor(tLive, exp);
    double maxErr = 0; for (int i = 1; i < 4; ++i) maxErr = std::max(maxErr, std::abs(stageVals[i] - exp[i]));
    const bool chainOk = std::isfinite(tLive) && tLive > 0.0 && maxErr < 1e-4;

    // severing each wire breaks the chain at exactly that stage (expected recomputed for THIS run's time).
    auto firstBreak = [&](int cut) {
        double s[4]; runChain(cut, s);
        double e[4]; expectedFor(s[0], e);
        for (int i = 1; i < 4; ++i) if (!std::isfinite(s[i]) || std::abs(s[i] - e[i]) > 1e-4) return i + 1;
        return 0;
    };
    const int b1 = firstBreak(1), b2 = firstBreak(2), b3 = firstBreak(3);
    const bool localizes = (b1 == 2) && (b2 == 3) && (b3 == 4);   // cut time->sine breaks sine(stage2); sine->add breaks add(3); add->robot breaks link(4)
    sim.stop();

    const bool pass = chainOk && localizes;
    printf("[connect-ctrl]   wired program: time=%.3f -> sine=%.4f -> add(+0.20 via widget)=%.4f -> live link=%.4f, max err=%.2e  %s\n",
           tLive, stageVals[1], stageVals[2], stageVals[3], maxErr, chainOk ? "PASS" : "FAIL");
    printf("[connect-ctrl]   NEG-CTRL sever localizes: cut time->%d, cut sine->%d, cut add->%d (want 2,3,4)  %s\n",
           b1, b2, b3, localizes ? "PASS" : "FAIL!");
    printf("[connect-ctrl] %s\n", pass ? "ALL PASS (wired program, widget-set value, live time, drives the live robot; severing localizes)"
                                       : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
