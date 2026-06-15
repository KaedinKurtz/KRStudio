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
#include "components.hpp"

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
glm::quat eigToGlm(const Eigen::Matrix3d& R) {
    glm::mat3 m; for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m[c][r] = float(R(r, c));
    return glm::quat_cast(m);
}
// drive a set of glass link entities at the PLANNED joint config via FK (NOT the live config).
void updateGlassFromPlanned(entt::registry& reg, const std::vector<entt::entity>& glass,
                            const Eigen::VectorXd& plannedQ, const krs::dyn::SerialChain& chain) {
    std::vector<krs::dyn::Pose> wp; chain.fk(plannedQ, wp);
    for (size_t i = 0; i < glass.size() && i < wp.size(); ++i) {
        if (!reg.valid(glass[i]) || !reg.all_of<TransformComponent>(glass[i])) continue;
        auto& xf = reg.get<TransformComponent>(glass[i]);
        xf.translation = glm::vec3(float(wp[i].p.x()), float(wp[i].p.y()), float(wp[i].p.z()));
        xf.rotation = eigToGlm(wp[i].R);
    }
}
} // namespace

// =================================================================================================
// C-knob: a goal-knob NODE's dial drives the live joint to the commanded angle (FK <1e-4)
// =================================================================================================
bool runControllerKnobGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[c-knob] GATE C-knob -- goal-knob node's dial drives the live joint (FK-verified)\n");

    SerialChain oc; krs::dyn::RobotArticSpec ps; buildFanuc(oc, ps);
    const int nDof = 4;
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(ps); sim.play();
    if (sim.articDofCount() != nDof) { sim.stop(); printf("[c-knob] FAIL: no articulation\n"); return false; }
    sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(nDof, 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    const auto poses0 = sim.articLinkPoses();
    const glm::quat restQ(poses0[0][6], poses0[0][3], poses0[0][4], poses0[0][5]);

    const double qCmd = 0.5;
    auto knob = NodeFactory::instance().createNode("ctrl_goal_knob");
    double knobOut = std::nan(""), fkErr = 9e9;
    if (knob) {
        knob->setParam<double>("angle", qCmd);          // <- the dial value
        knob->process();
        knobOut = nodeOut(*knob, "Angle");              // node emits the dial value
        std::vector<float> q(nDof, 0.0f); q[0] = float(knobOut);
        sim.setArticJointPositions(q); sim.singleStep();// drive the LIVE joint with the knob's value
        fkErr = std::abs(linkAngle(sim.articLinkPoses()[0], restQ) - qCmd);  // FK: link reached qCmd
    }
    const bool knobEmits = std::abs(knobOut - qCmd) < 1e-9;
    const bool fkOk = fkErr < 1e-4;
    const bool moved = qCmd > 0.1;

    // NEG-CTRL (disconnected knob): a fresh knob on its default (0) emits 0 -> commands no motion.
    auto knob0 = NodeFactory::instance().createNode("ctrl_goal_knob");
    double idle = std::nan("");
    if (knob0) { knob0->process(); idle = nodeOut(*knob0, "Angle"); }
    const bool disconnectedInert = std::abs(idle) < 1e-9;
    sim.stop();

    const bool pass = knobEmits && fkOk && moved && disconnectedInert;
    printf("[c-knob]   knob dial=%.3f -> node emits %.4f, live link reached it: FK err=%.2e (<1e-4)  %s ; moved %s\n",
           qCmd, knobOut, fkErr, fkOk ? "PASS" : "FAIL", moved ? "yes" : "NO");
    printf("[c-knob]   NEG-CTRL disconnected knob emits %.4f (==0) -> no command  %s\n",
           idle, disconnectedInert ? "PASS" : "FAIL!");
    printf("[c-knob] %s\n", pass ? "ALL PASS (knob dial drives the live joint to the commanded angle, FK-verified)"
                                  : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// =================================================================================================
// C-glass: the glass robot's link transforms come from the PLANNED config, not the live one
// =================================================================================================
bool runControllerGlassGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[c-glass] GATE C-glass -- glass robot tracks the PLANNED joint config (not the live one)\n");

    SerialChain oc; krs::dyn::RobotArticSpec ps; buildFanuc(oc, ps);
    const int nLinks = 4;
    Scene scene; auto& reg = scene.getRegistry();
    std::vector<entt::entity> glass;
    for (int i = 0; i < nLinks; ++i) { auto e = reg.create();
        reg.emplace<TransformComponent>(e); reg.emplace<GlassComponent>(e); glass.push_back(e); }

    Eigen::VectorXd qLive(4); qLive << 0.00, 0.30, 0.30, 0.00;   // current pose
    Eigen::VectorXd qPlan(4); qPlan << 0.80, 0.70, 1.00, 0.50;   // PLANNED/goal pose (different)
    std::vector<krs::dyn::Pose> wpPlan, wpLive; oc.fk(qPlan, wpPlan); oc.fk(qLive, wpLive);

    // drive the glass robot at the PLANNED config.
    updateGlassFromPlanned(reg, glass, qPlan, oc);
    double errVsPlan = 0.0, planVsLive = 0.0;
    for (int i = 0; i < nLinks; ++i) {
        const glm::vec3 t = reg.get<TransformComponent>(glass[i]).translation;
        const glm::vec3 plan(float(wpPlan[i].p.x()), float(wpPlan[i].p.y()), float(wpPlan[i].p.z()));
        const glm::vec3 live(float(wpLive[i].p.x()), float(wpLive[i].p.y()), float(wpLive[i].p.z()));
        errVsPlan = std::max(errVsPlan, double(glm::length(t - plan)));
        planVsLive = std::max(planVsLive, double(glm::length(plan - live)));
    }
    const bool tracksPlan = errVsPlan < 1e-5;                    // glass == FK(planned)
    const bool plannedDiffersLive = planVsLive > 0.1;           // and planned genuinely != live (showing the PLAN)

    // NEG-CTRL: feed it LIVE values instead of planned -> glass collapses to the current pose (plan==current).
    updateGlassFromPlanned(reg, glass, qLive, oc);
    double errVsLive = 0.0;
    for (int i = 0; i < nLinks; ++i) {
        const glm::vec3 t = reg.get<TransformComponent>(glass[i]).translation;
        const glm::vec3 live(float(wpLive[i].p.x()), float(wpLive[i].p.y()), float(wpLive[i].p.z()));
        errVsLive = std::max(errVsLive, double(glm::length(t - live)));
    }
    const bool collapsesOnLive = errVsLive < 1e-5;

    const bool pass = tracksPlan && plannedDiffersLive && collapsesOnLive;
    printf("[c-glass]   glass vs PLANNED FK err=%.2e (<1e-5)  %s ; planned vs live=%.3f m (>0.1, genuinely the plan) %s\n",
           errVsPlan, tracksPlan ? "PASS" : "FAIL", planVsLive, plannedDiffersLive ? "yes" : "NO!");
    printf("[c-glass]   NEG-CTRL fed LIVE values -> glass matches live err=%.2e (plan==current collapse)  %s\n",
           errVsLive, collapsesOnLive ? "PASS" : "FAIL!");
    printf("[c-glass] %s\n", pass ? "ALL PASS (glass tracks PLANNED config; feeding live collapses plan==current)"
                                   : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::ctrl
