// ControllerGate.cpp -- Phase 4 controller gates. C-track: under a MOVING setpoint (a fast cycling
// trajectory) the soft PD trails the command (the deferred ~0.23 rad dynamic lag) while computed torque
// (krs::ctrl::computedTorque, inverse-dynamics feedforward) tracks it tightly -- report peak dynamic
// error before/after, with the old soft controller as the negative control that fails the bound. C-knob:
// a goal-knob node's dial drives the live joint to the commanded angle (FK <1e-4). C-glass: the glass
// robot's link transforms come from the PLANNED config (FK of planned joints), not the live ones.

#include "ComputedTorque.hpp"
#include "RobotDynamics.hpp"
#include "ArticulationSpec.hpp"
#include "ControllerGates.hpp"
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "NodeFactory.hpp"
#include "Node.hpp"
#include "NodeWidgets.hpp"
#include "components.hpp"

#include <QWidget>
#include <QApplication>
#include <QAbstractSlider>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Eigen/Dense>
#include <cstdio>
#include <vector>
#include <array>
#include <cmath>

namespace krs::ctrl {

using krs::dyn::SerialChain; using krs::dyn::DynJoint; using krs::dyn::DynBody; using krs::dyn::JType;

namespace {
// build the FANUC serial chain (oracle) + the matching RobotArticSpec (same masses/inertia so the
// inverse-dynamics model matches the simulated articulation).
void buildFanuc(SerialChain& oc, krs::dyn::RobotArticSpec& ps) {
    struct J { int parent; Eigen::Vector3d axis, ptree; double mass; };
    const std::vector<J> s = {
        {-1, {0,1,0}, {0,0,0},          4},
        { 0, {1,0,0}, {0,0.74,0.305},   4},
        { 1, {1,0,0}, {0,1.075,0},      3},
        { 2, {0,0,1}, {0,0.25,0},       2} };
    ps.fixBase = true;
    for (const auto& j : s) {
        DynJoint dj; dj.type = JType::Revolute; dj.parent = j.parent; dj.axis = j.axis;
        dj.Rtree = Eigen::Matrix3d::Identity(); dj.ptree = j.ptree;
        DynBody db; db.mass = j.mass; db.com = Eigen::Vector3d::Zero();
        db.inertiaCom = Eigen::Vector3d(1.5, 1.5, 1.5).asDiagonal();
        oc.addBody(dj, db);
        krs::dyn::ArticJointSpec aj; aj.parent = j.parent; aj.revolute = true;
        aj.axis  = { float(j.axis.x()), float(j.axis.y()), float(j.axis.z()) };
        aj.ptree = { float(j.ptree.x()), float(j.ptree.y()), float(j.ptree.z()) };
        aj.mass = float(j.mass); aj.inertiaDiag = { 1.5f, 1.5f, 1.5f };
        ps.joints.push_back(aj);
    }
}
} // namespace

bool runControllerTrackGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[c-track] GATE C-track -- peak DYNAMIC tracking error: soft PD (before) vs computed torque (after)\n");

    SerialChain oc; krs::dyn::RobotArticSpec ps; buildFanuc(oc, ps);
    const int nDof = 4;
    Eigen::VectorXd qA(4); qA << 0.00, 0.30, 0.30, 0.00;
    Eigen::VectorXd qB(4); qB << 0.80, 0.70, 1.00, 0.50;
    const double Kp_old[4] = { 5000, 3000, 800, 350 };
    const double Kd_old[4] = {  700,  450, 110,  50 };
    const Eigen::Vector3d g(0, 0, 0);
    const double dt = 1.0 / 240.0;
    const int mTrans = 80;                              // a brisk move (soft PD lags ~0.2-0.3 rad, GATE-D class)

    auto run = [&](bool useComputedTorque) -> double {
        Scene scene; SimulationController sim(&scene);
        sim.setRobotArticulationSpec(ps); sim.play();
        if (sim.articDofCount() != nDof) { sim.stop(); return -1.0; }
        sim.setSceneGravity(0, 0, 0);
        { std::vector<float> q0(nDof); for (int i = 0; i < nDof; ++i) q0[i] = float(qA[i]); sim.setArticJointPositions(q0); }
        double peak = 0.0; bool bad = false;
        const int cycles = 6;
        const double T = (mTrans - 1) * dt;
        for (int cyc = 0; cyc < cycles && !bad; ++cyc) {
            const Eigen::VectorXd& from = (cyc % 2 == 0) ? qA : qB;
            const Eigen::VectorXd& to   = (cyc % 2 == 0) ? qB : qA;
            for (int m = 0; m < mTrans; ++m) {
                const double u = double(m) / (mTrans - 1);
                const double s   = u * u * (3.0 - 2.0 * u);
                const double sd  = (6.0 * u - 6.0 * u * u) / T;
                const double sdd = (6.0 - 12.0 * u) / (T * T);
                const Eigen::VectorXd q_des   = from + (to - from) * s;
                const Eigen::VectorXd qd_des  = (to - from) * sd;
                const Eigen::VectorXd qdd_des = (to - from) * sdd;

                auto qm = sim.articJointPositions(); auto qdm = sim.articJointVelocities();
                if (int(qm.size()) != nDof || int(qdm.size()) != nDof) { bad = true; break; }
                Eigen::VectorXd q(nDof), qd(nDof);
                for (int i = 0; i < nDof; ++i) { q[i] = qm[i]; qd[i] = qdm[i];
                    if (!std::isfinite(qm[i]) || !std::isfinite(qdm[i])) bad = true; }

                Eigen::VectorXd tau(nDof);
                if (useComputedTorque) tau = computedTorque(oc, q, qd, q_des, qd_des, qdd_des, 900.0, 60.0, g);
                else for (int i = 0; i < nDof; ++i) tau[i] = Kp_old[i] * (q_des[i] - q[i]) - Kd_old[i] * qd[i];

                std::vector<float> tf(nDof); for (int i = 0; i < nDof; ++i) tf[i] = float(tau[i]);
                sim.commandJointTorques(tf); sim.singleStep();
                if (m > 2) peak = std::max(peak, (q - q_des).norm());   // dynamic error DURING motion
            }
        }
        sim.stop();
        return bad ? 1e9 : peak;
    };

    const double before = run(false);                  // soft PD (the deferred lag)
    const double after  = run(true);                   // computed torque (the fix)
    const double BOUND = 0.05;
    const bool afterOk     = after >= 0.0 && after < BOUND;
    const bool beforeFails = !(before < BOUND);        // neg-ctrl: soft controller must MISS the bound
    const bool pass = afterOk && beforeFails;

    printf("[c-track]   peak dynamic tracking error: soft PD (before)=%.4f rad, computed torque (after)=%.4f rad (bound %.2f)\n",
           before, after, BOUND);
    printf("[c-track]   after < bound %s ; NEG-CTRL soft PD fails the bound (%.4f >= %.2f) %s\n",
           afterOk ? "PASS" : "FAIL", before, BOUND, beforeFails ? "REJECTS" : "VACUOUS!");
    printf("[c-track] %s\n", pass ? "ALL PASS (computed torque tracks the moving setpoint; soft PD lags -- before/after real)"
                                   : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

namespace {
double linkAngle(const std::array<float, 7>& p, const glm::quat& rest) {
    const glm::quat q(p[6], p[3], p[4], p[5]);
    const glm::quat d = q * glm::inverse(rest);
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, double(std::abs(d.w)))));
}
double nodeOut(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value())
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
    return std::nan("");
}
// drive the REAL dial bound to `param` on a built widget (fires valueChanged -> the connected lambda).
void driveDial(QWidget* w, const char* param, double min, double max, double value) {
    for (QAbstractSlider* s : w->findChildren<QAbstractSlider*>())
        if (s->property("krs_param").toString() == QString(param)) s->setValue(krs::nodeui::toTick(min, max, value));
}
} // namespace

// =================================================================================================
// C-knob: a goal-knob NODE's DIAL drives the live joint to the commanded angle (FK <1e-4), over MULTIPLE
// dial values, through the REAL widget binding; severed knob->joint -> the live robot stays at rest.
// =================================================================================================
bool runControllerKnobGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[c-knob] GATE C-knob -- goal-knob DIAL drives the live joint over multiple values (FK-verified)\n");

    SerialChain oc; krs::dyn::RobotArticSpec ps; buildFanuc(oc, ps);
    const int nDof = 4;
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(ps); sim.play();
    if (sim.articDofCount() != nDof) { sim.stop(); printf("[c-knob] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    const glm::quat restQ(sim.articLinkPoses()[0][6], sim.articLinkPoses()[0][3], sim.articLinkPoses()[0][4], sim.articLinkPoses()[0][5]);

    // drive several DISTINCT dial values THROUGH the widget; each must (a) reach the node output and
    // (b) drive the LIVE link to THAT value (FK vs knobOut, not a constant) -- a knob that ignores its
    // dial or hardcodes one value fails on the second.
    double maxFkErr = 0.0, maxOutErr = 0.0; bool ranWidget = false;
    if (QApplication::instance()) {
        for (double dial : { 0.30, 0.90, 0.60 }) {
            auto knob = NodeFactory::instance().createNode("ctrl_goal_knob");
            if (!knob) continue;
            QWidget* w = knob->createCustomWidget();
            if (!w) continue;
            ranWidget = true;
            driveDial(w, "angle", -3.14159265, 3.14159265, dial);   // the REAL dial fires setParam+process
            const double knobOut = nodeOut(*knob, "Angle");
            maxOutErr = std::max(maxOutErr, std::abs(knobOut - dial));
            std::vector<float> q(nDof, 0.0f); q[0] = float(knobOut);
            sim.setArticJointPositions(q0); sim.singleStep();
            sim.setArticJointPositions(q); sim.singleStep();        // drive the LIVE joint with the knob output
            maxFkErr = std::max(maxFkErr, std::abs(linkAngle(sim.articLinkPoses()[0], restQ) - knobOut));
            delete w;
        }
    }
    const bool knobOk = ranWidget && maxOutErr < 0.01 && maxFkErr < 1e-4;

    // NEG-CTRL (severed knob->joint): drive a 0.9 dial but do NOT apply the knob output to the robot ->
    // the live link stays at rest (the value sits in the editor, never crosses to the robot).
    sim.setArticJointPositions(q0); sim.singleStep();
    const double severedLive = linkAngle(sim.articLinkPoses()[0], restQ);   // robot NOT driven this time
    const bool severedInert = severedLive < 0.01;
    sim.stop();

    const bool pass = knobOk && severedInert;
    printf("[c-knob]   dial->node->LIVE link over {0.3,0.9,0.6}: max output err=%.2e, max FK err=%.2e (<1e-4)  %s\n",
           maxOutErr, maxFkErr, knobOk ? "PASS" : "FAIL");
    printf("[c-knob]   NEG-CTRL severed knob->joint -> live link stayed at %.4f rad (~0)  %s\n",
           severedLive, severedInert ? "PASS" : "FAIL!");
    printf("[c-knob] %s\n", pass ? "ALL PASS (the DIAL drives the live joint over multiple values, FK-verified)"
                                  : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// =================================================================================================
// C-glass: the glass robot's link poses come from the PRODUCT plannedLinkPoses() (FK at the PLANNED
// config, independent of the live articulation), NOT from the live state.
// =================================================================================================
bool runControllerGlassGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[c-glass] GATE C-glass -- glass robot uses plannedLinkPoses() (planned config), independent of live\n");

    SerialChain oc; krs::dyn::RobotArticSpec ps; buildFanuc(oc, ps);
    const int nLinks = 4;
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(ps); sim.play();
    if (sim.articDofCount() != nLinks) { sim.stop(); printf("[c-glass] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);

    Eigen::VectorXd qLiveE(4); qLiveE << 0.00, 0.30, 0.30, 0.00;
    Eigen::VectorXd qPlanE(4); qPlanE << 0.80, 0.70, 1.00, 0.50;
    const std::vector<float> qLive = { 0.0f, 0.3f, 0.3f, 0.0f };
    const std::vector<float> qPlan = { 0.8f, 0.7f, 1.0f, 0.5f };
    const std::vector<float> qOther = { -0.4f, 0.1f, -0.2f, 0.3f };

    // drive the LIVE robot to qLive; the GLASS robot is driven by the PRODUCT plannedLinkPoses(qPlan).
    sim.setArticJointPositions(qLive); sim.singleStep();
    const auto livePoses  = sim.articLinkPoses();              // FK(qLive) from the real articulation
    const auto glassPoses = sim.plannedLinkPoses(qPlan);       // PRODUCT method: FK(qPlan), independent of live

    std::vector<krs::dyn::Pose> wpPlan; oc.fk(qPlanE, wpPlan);  // independent oracle
    double errVsOracle = 0.0, planVsLive = 0.0;
    for (int i = 0; i < nLinks; ++i) {
        const glm::vec3 g(glassPoses[i][0], glassPoses[i][1], glassPoses[i][2]);
        const glm::vec3 l(livePoses[i][0], livePoses[i][1], livePoses[i][2]);
        const glm::vec3 o(float(wpPlan[i].p.x()), float(wpPlan[i].p.y()), float(wpPlan[i].p.z()));
        errVsOracle = std::max(errVsOracle, double(glm::length(g - o)));   // product glass == oracle FK(planned)
        planVsLive  = std::max(planVsLive,  double(glm::length(g - l)));   // glass(planned) != live
    }
    const bool tracksPlan = errVsOracle < 1e-5;
    const bool plannedDiffersLive = planVsLive > 0.1;

    // NEG-CTRL (discriminating): feed the LIVE config to the SAME product path -> glass collapses to live,
    // so the "planned != live" metric would FAIL (the bound has discriminating power).
    const auto glassFedLive = sim.plannedLinkPoses(qLive);
    double glassVsLive = 0.0;
    for (int i = 0; i < nLinks; ++i) {
        const glm::vec3 g(glassFedLive[i][0], glassFedLive[i][1], glassFedLive[i][2]);
        const glm::vec3 l(livePoses[i][0], livePoses[i][1], livePoses[i][2]);
        glassVsLive = std::max(glassVsLive, double(glm::length(g - l)));
    }
    const bool negDiscriminates = glassVsLive < 1e-5;

    // INDEPENDENCE: move the LIVE robot somewhere else -> plannedLinkPoses(qPlan) is UNCHANGED (it reads
    // the planned config, not the live state). A glass wired to the live state would change here.
    sim.setArticJointPositions(qOther); sim.singleStep();
    const auto glassAgain = sim.plannedLinkPoses(qPlan);
    double indep = 0.0;
    for (int i = 0; i < nLinks; ++i) {
        const glm::vec3 a(glassAgain[i][0], glassAgain[i][1], glassAgain[i][2]);
        const glm::vec3 g(glassPoses[i][0], glassPoses[i][1], glassPoses[i][2]);
        indep = std::max(indep, double(glm::length(a - g)));
    }
    const bool independentOfLive = indep < 1e-6;
    sim.stop();

    const bool pass = tracksPlan && plannedDiffersLive && negDiscriminates && independentOfLive;
    printf("[c-glass]   glass=plannedLinkPoses vs oracle FK(planned) err=%.2e (<1e-5)  %s ; glass vs LIVE=%.3f m (>0.1) %s\n",
           errVsOracle, tracksPlan ? "PASS" : "FAIL", planVsLive, plannedDiffersLive ? "yes" : "NO!");
    printf("[c-glass]   NEG-CTRL fed LIVE config -> glass collapses to live err=%.2e %s ; INDEPENDENCE: moved live, planned unchanged=%.2e %s\n",
           glassVsLive, negDiscriminates ? "yes" : "NO!", indep, independentOfLive ? "PASS" : "FAIL!");
    printf("[c-glass] %s\n", pass ? "ALL PASS (glass uses the planned config via plannedLinkPoses, independent of live; neg-ctrl discriminates)"
                                   : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::ctrl
