// DemoGraphGate.cpp -- the SPINE gates. The default boot scene is a REAL editor node graph
// (time_source -> gen_sine -> physics_articulation_drive) that drives the live robot; the hardcoded demo
// sweep is gone, so the node graph is the SINGLE joint writer.
//
// GATE DEMO-GRAPH: editing a VISIBLE node on the canvas (the sine's amp dial, through its mounted widget)
//   changes the robot's REAL motion. The live joint tracks amp*sin(2pi*f*t) for the amp the dial is set to;
//   after editing the dial the robot follows the NEW amp and NO LONGER the old one (a hardcoded/decorative
//   bypass would keep following the old amp -> that is the negative control).
// GATE OWNERSHIP: the node command is the sole joint driver (live joint == node command, FK <1e-4; the link
//   actually rotates); NEG-CTRL: with NO graph the joint does not move (no hidden second writer); rapid
//   source-switching (changing the commanded DOF every tick) never crashes/corrupts the articulation.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"
#include "RobotGraph.hpp"
#include "NodeWidgets.hpp"          // toTick
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "FanucArticulation.hpp"
#include "ArticulationSpec.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QApplication>
#include <QWidget>
#include <QAbstractSlider>
#include <QThread>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cmath>
#include <vector>
#include <array>
#include <memory>

namespace krs::nodes {
namespace {
constexpr double TAU = 6.283185307179586;
constexpr double J1_LIMIT = 3.14159265358979;   // real FANUC J1 ~ +-180 deg; the demo must stay within.

double outOf(Node& n, const char* port) {
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
// edit a tagged param dial/slider in the node body (the rendered widget path).
bool driveParamWidget(QWidget* body, const char* param, double min, double max, double value) {
    if (!body) return false;
    for (QWidget* w : body->findChildren<QWidget*>())
        if (w->property("krs_param").isValid() && w->property("krs_param").toString() == QString(param))
            if (auto* s = qobject_cast<QAbstractSlider*>(w)) { s->setValue(krs::nodeui::toTick(min, max, value)); return true; }
    return false;
}
} // namespace

bool runDemoGraphGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[demo-graph] GATE DEMO-GRAPH -- editing the canvas sine's amp (its mounted dial) changes the live robot's motion\n");
    if (!QApplication::instance()) { printf("[demo-graph] FAIL: needs QApplication\n"); return false; }

    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    const int nDof = int(spec.joints.size());
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(spec); sim.play();
    if (sim.articDofCount() != nDof) { sim.stop(); printf("[demo-graph] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    const glm::quat rest(sim.articLinkPoses()[0][6], sim.articLinkPoses()[0][3], sim.articLinkPoses()[0][4], sim.articLinkPoses()[0][5]);
    sim.play();   // singleStep() paused the sim; re-enter Playing so the tick loop steps + applies commands

    // The DEFAULT boot graph -- the SAME helper the app calls -- drives J1. Faster freq so a large-|sin|
    // phase is reached quickly in the headless loop.
    auto model = makeNodeGraphModel();
    const double freq = 0.8, amp1 = 0.5;
    BootGraphHandles h = spawnDefaultRobotGraph(*model, &scene, /*joint*/0, freq, amp1);
    if (!h.ok) { sim.stop(); printf("[demo-graph] FAIL: spawnDefaultRobotGraph\n"); return false; }
    auto* tDel = model->delegateModel<NodeDelegate>(h.timeId);
    auto* sDel = model->delegateModel<NodeDelegate>(h.sineId);
    Node* tN = tDel->backendNode(); Node* sN = sDel->backendNode();
    QWidget* sBody = sDel->embeddedWidget();   // the sine node body (carries the amp dial)

    // tick the graph + sim with wall-clock until |sine| is large; return (t, sineOut, liveQ0, linkAng, maxAbsQ0).
    auto sampleAtLargeSine = [&](double& t, double& sineOut, double& liveQ0, double& linkAng, double& maxAbsQ0) {
        maxAbsQ0 = 0.0;
        for (int i = 0; i < 240; ++i) {
            tickRobotGraph(*model); sim.tick(); QThread::msleep(12);
            const double s = outOf(*sN, "Out");
            const double a = sN->getParam<double>("amp", 0.0);
            const double q = sim.articJointPositions()[0];
            maxAbsQ0 = std::max(maxAbsQ0, std::abs(q));
            if (a > 1e-6 && std::abs(s) / a > 0.6) {                  // |sin(2pi f t)| > 0.6 -> a clear sample
                t = outOf(*tN, "Time"); sineOut = s; liveQ0 = q;
                linkAng = linkAngle(sim.articLinkPoses()[0], rest);
                return true;
            }
        }
        return false;
    };

    double t1, s1, q1, link1, max1;
    if (!sampleAtLargeSine(t1, s1, q1, link1, max1)) { sim.stop(); printf("[demo-graph] FAIL: never reached a clear sine phase\n"); return false; }
    const double pred1 = amp1 * std::sin(TAU * freq * t1);
    const bool sineCorrect1 = std::abs(s1 - pred1) < 1e-3;            // sine analytically correct
    const bool robotTracks1 = std::abs(q1 - s1) < 1e-4;              // live joint == node command (FK-exact)
    const bool linkMoved1   = std::abs(link1 - std::abs(q1)) < 1e-3; // the link actually rotated by q0
    const bool within1      = max1 <= J1_LIMIT;                       // boot motion within joint limits

    // ---- EDIT the visible node: drag the sine's amp dial to amp2 (through its mounted widget) ----
    const double amp2 = 0.20;
    const bool edited = driveParamWidget(sBody, "amp", 0.0, 5.0, amp2);
    const double amp2eff = sN->getParam<double>("amp", 0.0);          // the amp the dial actually set

    double t2, s2, q2, link2, max2;
    const bool got2 = sampleAtLargeSine(t2, s2, q2, link2, max2);
    const double predNew = amp2eff * std::sin(TAU * freq * t2);       // robot SHOULD follow the new amp
    const double predOld = amp1   * std::sin(TAU * freq * t2);        // a bypass would still follow the old amp
    const bool robotTracks2 = got2 && std::abs(q2 - s2) < 1e-4 && std::abs(q2 - predNew) < 1e-3;
    const bool changed      = got2 && std::abs(q2 - predOld) > 0.1;   // the edit really changed the motion
    const bool within2      = max2 <= J1_LIMIT;

    sim.stop();

    const bool pass = sineCorrect1 && robotTracks1 && linkMoved1 && within1 && within2 &&
                      edited && robotTracks2 && changed && (std::abs(amp2eff - amp2) < 0.02);
    printf("[demo-graph]   amp1=%.3f: t=%.3f sine=%.4f -> live joint=%.4f (link rot=%.4f); tracks node command=%s, sine correct=%s, within limits=%s\n",
           amp1, t1, s1, q1, link1, robotTracks1 ? "yes" : "NO", sineCorrect1 ? "yes" : "NO", within1 ? "yes" : "NO");
    printf("[demo-graph]   EDIT amp dial -> %.3f (eff %.3f): live joint=%.4f follows NEW amp pred=%.4f (%s), NO LONGER old pred=%.4f (delta %.4f)\n",
           amp2, amp2eff, q2, predNew, robotTracks2 ? "yes" : "NO", predOld, std::abs(q2 - predOld));
    printf("[demo-graph]   NEG-CTRL: a hardcoded/decorative bypass would keep following the old amp -> editing changes motion: %s\n",
           changed ? "PASS (edit propagated to the robot)" : "FAIL!");
    printf("[demo-graph] %s\n", pass ? "ALL PASS (the canvas node graph IS the robot's driver; editing it changes the real motion; within limits)"
                                     : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

bool runOwnershipGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ownership] GATE OWNERSHIP -- the node command is the SOLE joint driver (no hardcoded second writer)\n");
    if (!QApplication::instance()) { printf("[ownership] FAIL: needs QApplication\n"); return false; }

    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    const int nDof = int(spec.joints.size());

    auto makeRobot = [&](Scene& scene, SimulationController& sim, glm::quat& rest) -> bool {
        sim.setRobotArticulationSpec(spec); sim.play();
        if (sim.articDofCount() != nDof) return false;
        sim.setSceneGravity(0, 0, 0);
        std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
        rest = glm::quat(sim.articLinkPoses()[0][6], sim.articLinkPoses()[0][3], sim.articLinkPoses()[0][4], sim.articLinkPoses()[0][5]);
        sim.play();   // re-enter Playing (singleStep paused it) so the caller's tick loop applies commands
        return true;
    };

    // (1) the node command drives the joint, FK-exact; the link actually rotates.
    double cmd = 0, liveQ0 = 0, linkAng = 0, maxAbsQ0 = 0; bool finiteAll = true;
    {
        Scene scene; SimulationController sim(&scene); glm::quat rest;
        if (!makeRobot(scene, sim, rest)) { sim.stop(); printf("[ownership] FAIL: no articulation\n"); return false; }
        auto model = makeNodeGraphModel();
        BootGraphHandles h = spawnDefaultRobotGraph(*model, &scene, 0, 0.8, 0.5);
        Node* sN = model->delegateModel<NodeDelegate>(h.sineId)->backendNode();
        for (int i = 0; i < 60; ++i) {
            tickRobotGraph(*model); sim.tick(); QThread::msleep(12);
            for (float v : sim.articJointPositions()) if (!std::isfinite(v)) finiteAll = false;
            maxAbsQ0 = std::max(maxAbsQ0, std::abs(double(sim.articJointPositions()[0])));
            if (std::abs(outOf(*sN, "Out")) > 0.3) {
                cmd = outOf(*sN, "Out"); liveQ0 = sim.articJointPositions()[0];
                linkAng = linkAngle(sim.articLinkPoses()[0], rest);
            }
        }
        sim.stop();
    }
    const bool sole = std::abs(liveQ0 - cmd) < 1e-4;          // joint == node command (no other writer adds to it)
    const bool fk   = std::abs(linkAng - std::abs(liveQ0)) < 1e-3;
    const bool within = maxAbsQ0 <= J1_LIMIT;

    // (2) NEG-CTRL: NO graph -> the joint does NOT move (the removed demo sweep is truly gone).
    double restMax = 0;
    {
        Scene scene; SimulationController sim(&scene); glm::quat rest;
        if (!makeRobot(scene, sim, rest)) { sim.stop(); printf("[ownership] FAIL: no articulation (neg-ctrl)\n"); return false; }
        for (int i = 0; i < 60; ++i) { sim.tick(); QThread::msleep(8);
            restMax = std::max(restMax, std::abs(double(sim.articJointPositions()[0]))); }
        sim.stop();
    }
    const bool noSecondWriter = restMax < 1e-4;

    // (3) rapid source-switching (change the commanded DOF every tick) -> no crash / corruption.
    bool switchFinite = true; double switchMax = 0;
    {
        Scene scene; SimulationController sim(&scene); glm::quat rest;
        if (!makeRobot(scene, sim, rest)) { sim.stop(); printf("[ownership] FAIL: no articulation (switch)\n"); return false; }
        auto model = makeNodeGraphModel();
        BootGraphHandles h = spawnDefaultRobotGraph(*model, &scene, 0, 1.2, 0.4);
        Node* dN = model->delegateModel<NodeDelegate>(h.driveId)->backendNode();
        for (int i = 0; i < 80; ++i) {
            dN->setPortLiteral<int>("Joint", i % nDof);          // hammer the commanded DOF
            tickRobotGraph(*model); sim.tick(); QThread::msleep(4);
            for (float v : sim.articJointPositions()) { if (!std::isfinite(v)) switchFinite = false; switchMax = std::max(switchMax, std::abs(double(v))); }
        }
        sim.stop();
    }
    const bool switchOk = switchFinite && switchMax <= J1_LIMIT + 1e-3;

    const bool pass = sole && fk && within && noSecondWriter && switchOk;
    printf("[ownership]   node command sole driver: live joint=%.4f == command=%.4f (%s); link rot=%.4f matches |q0| (%s); within limits (%s)\n",
           liveQ0, cmd, sole ? "yes" : "NO", linkAng, fk ? "yes" : "NO", within ? "yes" : "NO");
    printf("[ownership]   NEG-CTRL no graph -> joint max moved=%.2e (no second writer): %s\n", restMax, noSecondWriter ? "PASS" : "FAIL!");
    printf("[ownership]   rapid DOF-switching: finite=%d maxAbs=%.3f (no crash/corruption): %s\n", int(switchFinite), switchMax, switchOk ? "PASS" : "FAIL!");
    printf("[ownership] %s\n", pass ? "ALL PASS (one writer: the node graph; FK-exact; rest when ungraphed; robust to switching)"
                                    : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
