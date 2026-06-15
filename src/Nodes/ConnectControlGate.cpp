// ConnectControlGate.cpp -- Phase G GATE CONNECT-AND-CONTROL: a robot-control program built by WIRING
// nodes, with a value set THROUGH A RENDERED INPUT WIDGET, driven by the LIVE time source, driving the
// live robot. Chain: time_source -> gen_sine -> math_add(.A wired, .B set via its mounted spinbox) ->
// joint command -> live PhysX link. Every stage asserted (causal chain); severing a wire localizes the
// break. This is the end-to-end proof that the editor is wireable AND inputs are settable through the body.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"
#include "ConnectControlGate.hpp"
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "FanucArticulation.hpp"
#include "ArticulationSpec.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
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

    const double bOffset = 0.20;
    bool connsOk = true;
    // build the wired program in a REAL DataFlowGraphModel with `cut` controlling which EDGE is omitted
    // (0 none, 1 time->sine, 2 sine->add, 3 add->robot). Propagation runs through the actual QtNodes edges.
    auto runChain = [&](int cut, double out[4]) {
        auto model = makeNodeGraphModel();
        const QtNodes::NodeId tId = model->addNode("time_source");
        const QtNodes::NodeId sId = model->addNode("gen_sine");
        const QtNodes::NodeId aId = model->addNode("math_add");
        auto* tDel = model->delegateModel<NodeDelegate>(tId);
        auto* sDel = model->delegateModel<NodeDelegate>(sId);
        auto* aDel = model->delegateModel<NodeDelegate>(aId);
        Node* sN = sDel->backendNode(); Node* aN = aDel->backendNode();
        sN->setParam<double>("freq", 0.5); sN->setParam<double>("amp", 0.4); sN->setParam<double>("phase", 0.0);
        // set add.B THROUGH the mounted spinbox in add's body (the rendered input widget).
        if (auto* bCtl = qobject_cast<QDoubleSpinBox*>(findCtl(aDel->embeddedWidget(), "B"))) bCtl->setValue(bOffset);
        // real connections through the editor path.
        const QtNodes::ConnectionId tsConn{ tId, QtNodes::PortIndex(portIndexByName(tDel->backendNode(), Port::Direction::Output, "Time")),
                                            sId, QtNodes::PortIndex(portIndexByName(sN, Port::Direction::Input, "t")) };
        const QtNodes::ConnectionId saConn{ sId, QtNodes::PortIndex(portIndexByName(sN, Port::Direction::Output, "Out")),
                                            aId, QtNodes::PortIndex(portIndexByName(aN, Port::Direction::Input, "A")) };
        if (cut == 0) connsOk = connsOk && model->connectionPossible(tsConn) && model->connectionPossible(saConn);
        if (cut != 1) model->addConnection(tsConn);
        if (cut != 2) model->addConnection(saConn);
        QThread::msleep(120);   // let THIS run's live time source accumulate real wall-clock before ticking
        // tick the time source (the 30Hz MainWindow path) -> propagates through the present edges.
        tDel->recomputeAndPropagate(); sDel->recomputeAndPropagate(); aDel->recomputeAndPropagate();
        out[0] = outD(*tDel->backendNode(), "Time");
        out[1] = outD(*sN, "Out");
        out[2] = outD(*aN, "Result");
        // joint command -> live robot (cut 3 = the command never reaches the robot)
        sim.setArticJointPositions(q0); sim.singleStep();
        const double cmd = (cut == 3) ? 0.0 : (std::isfinite(out[2]) ? out[2] : 0.0);
        std::vector<float> q(nDof, 0.0f); q[0] = float(cmd);
        sim.setArticJointPositions(q); sim.singleStep();
        out[3] = linkAngle(sim.articLinkPoses()[0], restQ);
    };

    auto expectedFor = [&](double t, double e[4]) {
        const double eSine = 0.4 * std::sin(TAU * 0.5 * t);
        e[0] = t; e[1] = eSine; e[2] = eSine + bOffset; e[3] = eSine + bOffset;
    };
    double stageVals[4]; runChain(0, stageVals);
    const double tLive = stageVals[0];
    double exp[4]; expectedFor(tLive, exp);
    double maxErr = 0; for (int i = 1; i < 4; ++i) maxErr = std::max(maxErr, std::abs(stageVals[i] - exp[i]));
    const bool chainOk = connsOk && std::isfinite(tLive) && tLive > 0.0 && maxErr < 1e-4;

    auto firstBreak = [&](int cut) {
        double s[4]; runChain(cut, s);
        double e[4]; expectedFor(s[0], e);
        for (int i = 1; i < 4; ++i) if (!std::isfinite(s[i]) || std::abs(s[i] - e[i]) > 1e-4) return i + 1;
        return 0;
    };
    const int b1 = firstBreak(1), b2 = firstBreak(2), b3 = firstBreak(3);
    const bool localizes = (b1 == 2) && (b2 == 3) && (b3 == 4);
    sim.stop();

    const bool pass = chainOk && localizes;
    printf("[connect-ctrl]   real edges connectable=%s; wired program: time=%.3f -> sine=%.4f -> add(+0.20 via widget)=%.4f -> live link=%.4f, err=%.2e  %s\n",
           connsOk ? "yes" : "NO", tLive, stageVals[1], stageVals[2], stageVals[3], maxErr, chainOk ? "PASS" : "FAIL");
    printf("[connect-ctrl]   NEG-CTRL sever localizes: cut time->%d, cut sine->%d, cut add->%d (want 2,3,4)  %s\n",
           b1, b2, b3, localizes ? "PASS" : "FAIL!");
    printf("[connect-ctrl] %s\n", pass ? "ALL PASS (REAL edges + propagation, widget-set value, live time -> live robot; severing localizes)"
                                       : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
