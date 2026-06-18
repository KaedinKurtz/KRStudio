// OMPLPlannerNode.cpp -- Phase 4: a physically LARGE motion-planning node wrapping krs::plan::MotionPlanner.
//
// TWO DISTINCT STAGES, each its own trigger:
//   * PLAN    (rising edge on the "Plan" input, or the big blue PLAN button) -> runs the sampling planner from
//             the current Start config to the Goal config (e.g. an IK Target's Goal output) and FREEZES the
//             resulting collision-checked path. It does NOT move the robot -- the Command output stays at Start.
//   * EXECUTE (rising edge on the "Execute" input, or the big green EXECUTE button) -> drives the Command
//             output along the planned path, one step per eval tick, until it reaches the Goal. EXECUTE with no
//             prior successful plan does nothing (graceful).
//
// The planner parameters are IN-NODE widgets (the Phase-1 literal-seed path): a "Planner" enum combo
// (RRT-Connect / RRT*), a "Max Iters" spin box (the planning budget), and a "Waypoints" spin box (path
// resolution). Each is also a wireable input port. The node uses the default Async policy and detects the Plan
// and Execute edges itself (the framework's single-Trigger Triggered policy can't carry two triggers); the base
// "Trigger" hold input is left low so compute() runs every tick (needed to step the execution).
//
// (The big PLAN/EXECUTE buttons + the Command driving the live robot are OPERATOR VISUAL-CONFIRM; the
// two-stage separation, the in-node-param effect, and the path validity are gated headlessly below.)
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "NodePlannerArm.hpp"
#include "MotionPlanner.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

namespace krs::nodes {
namespace {

class OMPLPlannerNode : public Node {
public:
    OMPLPlannerNode() {
        m_id = "ompl_planner";                     // base ctor already added the "Trigger" (hold) input -- left low
        // --- inputs ---
        m_ports.push_back({ "Start", { "joint_config", "handle" }, Port::Direction::Input, this });  // robot's current config (wire)
        m_ports.push_back({ "Goal",  { "joint_config", "handle" }, Port::Direction::Input, this });  // target config (from IK)
        m_ports.push_back({ "Plan",    { "bool", "unitless" }, Port::Direction::Input, this });       // PLAN trigger
        m_ports.push_back({ "Execute", { "bool", "unitless" }, Port::Direction::Input, this });       // EXECUTE trigger
        addEnumInputPort("Planner", { "RRT-Connect", "RRT*" });                                       // in-node combo
        m_ports.push_back({ "Max Iters",  { "int", "unitless" }, Port::Direction::Input, this });     // planning budget spin
        m_ports.push_back({ "Waypoints",  { "int", "unitless" }, Port::Direction::Input, this });     // path resolution spin
        setPortLiteral<int>("Max Iters", 6000);                                                       // default budget
        setPortLiteral<int>("Waypoints", 64);                                                         // default resolution
        // --- outputs ---
        m_ports.push_back({ "Command",    { "joint_config", "handle" }, Port::Direction::Output, this }); // drives the robot
        m_ports.push_back({ "Planned",    { "bool", "unitless" },       Port::Direction::Output, this });
        m_ports.push_back({ "Executing",  { "bool", "unitless" },       Port::Direction::Output, this });
        m_ports.push_back({ "Progress",   { "double", "unitless" },     Port::Direction::Output, this });
        m_ports.push_back({ "PathLen",    { "double", "m" },            Port::Direction::Output, this });
        m_ports.push_back({ "Iterations", { "double", "unitless" },     Port::Direction::Output, this });

        m_ee = buildDefaultArm(m_chain, m_lim, m_world);
        m_start = Eigen::VectorXd::Zero(m_chain.nq());
        m_command = m_start;
    }
    bool needsExecutionControls() const override { return false; } // it has its own PLAN/EXECUTE buttons
    bool isPureInputFunction() const override { return false; }    // stateful: triggers + execution progress

    // big, distinct PLAN (blue) + EXECUTE (green) buttons -> one-shot param pulses folded into the edge below.
    QWidget* createCustomWidget() override {
        auto* w = new QWidget();
        auto* v = new QVBoxLayout(w); v->setSpacing(6);
        auto* title = new QLabel(QStringLiteral("OMPL MOTION PLANNER"));
        title->setStyleSheet(QStringLiteral("color:#ecf0f1;font-weight:bold;font-size:13px;"));
        auto* planBtn = new QPushButton(QStringLiteral("PLAN"));
        planBtn->setMinimumSize(190, 56);
        planBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2980b9;color:white;font-weight:bold;font-size:17px;border-radius:9px;}"
            "QPushButton:pressed{background:#3498db;}"));
        auto* execBtn = new QPushButton(QStringLiteral("EXECUTE"));
        execBtn->setMinimumSize(190, 56);
        execBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#27ae60;color:white;font-weight:bold;font-size:17px;border-radius:9px;}"
            "QPushButton:pressed{background:#2ecc71;}"));
        QObject::connect(planBtn, &QPushButton::clicked, [this] { setParam<bool>("planPulse", true); });
        QObject::connect(execBtn, &QPushButton::clicked, [this] { setParam<bool>("execPulse", true); });
        v->addWidget(title); v->addWidget(planBtn); v->addWidget(execBtn);
        return w;
    }

    void compute() override {
        const int nq = m_chain.nq();
        const bool plan = getInput<bool>("Plan").value_or(false);
        const bool exec = getInput<bool>("Execute").value_or(false);
        const bool planPulse = getParam<bool>("planPulse", false);   // from the PLAN button (one-shot)
        const bool execPulse = getParam<bool>("execPulse", false);   // from the EXECUTE button (one-shot)
        const bool planEdge = (plan && !m_lastPlan) || planPulse;
        const bool execEdge = (exec && !m_lastExecute) || execPulse;
        if (planPulse) setParam<bool>("planPulse", false);           // consume
        if (execPulse) setParam<bool>("execPulse", false);

        Eigen::VectorXd start = getInput<Eigen::VectorXd>("Start").value_or(Eigen::VectorXd::Zero(nq));
        if (start.size() != nq) start = Eigen::VectorXd::Zero(nq);
        Eigen::VectorXd goal = getInput<Eigen::VectorXd>("Goal").value_or(start);
        if (goal.size() != nq) goal = start;

        // With no successful plan yet, the Command tracks the live Start config (the robot sits where it is).
        // After a plan exists it is managed below: held at Start until EXECUTE, then along the path, then at Goal.
        if (!m_haveplan) m_command = start;

        // ---- STAGE 1: PLAN -- compute & freeze a path; the robot does NOT move. ----
        if (planEdge) {
            m_start = start; m_command = start;          // command holds at the start config after planning
            krs::plan::PlanRequest req;
            req.start = start; req.goal = goal;
            req.kind = (getInput<int>("Planner").value_or(0) == 1) ? krs::plan::PlannerKind::RRTstar
                                                                   : krs::plan::PlannerKind::RRTConnect;
            req.seed = 7;                                 // deterministic
            req.maxIterations = std::uint32_t(std::max(1, getInput<int>("Max Iters").value_or(6000)));
            req.denseWaypoints = unsigned(std::max(2, getInput<int>("Waypoints").value_or(64)));
            m_plan = m_planner.plan(req);
            m_haveplan = m_plan.solved && m_plan.waypoints.size() > 1;
            m_executing = false; m_execIdx = 0;
        }

        // ---- STAGE 2: EXECUTE -- only after a successful plan; step the command along the frozen path. ----
        if (execEdge && m_haveplan) m_executing = true;  // graceful: an EXECUTE with no plan is ignored
        if (m_executing && m_haveplan) {
            const int last = int(m_plan.waypoints.size()) - 1;
            const int step = std::max(1, int(m_plan.waypoints.size()) / 24);
            m_execIdx += step;
            if (m_execIdx >= last) { m_execIdx = last; m_executing = false; }
            m_command = m_plan.waypoints[m_execIdx];
        }

        // ---- outputs ----
        setOutput<Eigen::VectorXd>("Command", m_command.size() == nq ? m_command : start);
        setOutput<bool>("Planned", m_haveplan);
        setOutput<bool>("Executing", m_executing);
        const double prog = (m_haveplan && m_plan.waypoints.size() > 1)
                          ? double(m_execIdx) / double(m_plan.waypoints.size() - 1) : 0.0;
        setOutput<double>("Progress", prog);
        setOutput<double>("PathLen", m_plan.pathLength);
        setOutput<double>("Iterations", double(m_plan.iterations));

        m_lastPlan = plan; m_lastExecute = exec;
    }

    // accessors for the gate (independent re-check against the same world the node planned in)
    const krs::dyn::SerialChain&  chain() const { return m_chain; }
    const krs::plan::CollisionWorld& world() const { return m_world; }
    const krs::plan::JointLimits&    lim()   const { return m_lim; }
    const krs::plan::PlanResult&     lastPlan() const { return m_plan; }

private:
    krs::dyn::SerialChain    m_chain;
    krs::plan::JointLimits   m_lim;
    krs::plan::CollisionWorld m_world;
    krs::plan::MotionPlanner m_planner{ m_chain, m_world, m_lim };  // refs bind to the members above
    int m_ee = 0;

    krs::plan::PlanResult m_plan;
    bool m_haveplan = false, m_executing = false;
    int  m_execIdx = 0;
    Eigen::VectorXd m_start, m_command;
    bool m_lastPlan = false, m_lastExecute = false;
};

struct OMPLRegistrar {
    OMPLRegistrar() {
        NodeFactory::instance().registerNodeType("ompl_planner",
            { "OMPL Planner", "Robotics/Planning",
              "Two-stage motion planner: PLAN freezes a collision-free path to the Goal (robot stays put); "
              "EXECUTE drives the robot along it. In-node planner/iteration/resolution widgets." },
            []() { return std::make_unique<OMPLPlannerNode>(); });
    }
};
static OMPLRegistrar g_omplRegistrar;

// --- gate I/O helpers ------------------------------------------------------
void setJointIn(Node& n, const char* p, const Eigen::VectorXd& q) {
    PortDataPacket pk; pk.data = q; pk.type = { "joint_config", "handle" }; n.setInput(p, pk);
}
void setBoolIn(Node& n, const char* p, bool b) {
    PortDataPacket pk; pk.data = b; pk.type = { "bool", "unitless" }; n.setInput(p, pk);
}
bool rOutB(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<bool>(x.packet->data); } catch (...) {}
    return false;
}
double rOutD(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<double>(x.packet->data); } catch (...) {}
    return std::nan("");
}
Eigen::VectorXd rOutCmd(Node& n) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == "Command" && x.packet)
        try { return std::any_cast<Eigen::VectorXd>(x.packet->data); } catch (...) {}
    return {};
}
// fire a clean rising edge on a trigger input port (low tick, then high tick).
void pulse(OMPLPlannerNode& n, const char* trig) {
    setBoolIn(n, "Plan", false); setBoolIn(n, "Execute", false); n.process();
    setBoolIn(n, trig, true); n.process();
    setBoolIn(n, trig, false);
}
double pathMaxPen(const krs::dyn::SerialChain& chain, const krs::plan::CollisionWorld& w,
                  const std::vector<Eigen::VectorXd>& path) {
    double m = 0.0; for (const auto& q : path) m = std::max(m, w.maxPenetration(chain, q)); return m;
}
std::vector<Eigen::VectorXd> straightLine(const Eigen::VectorXd& a, const Eigen::VectorXd& b, int n) {
    std::vector<Eigen::VectorXd> p; p.reserve(n);
    for (int k = 0; k < n; ++k) { const double t = double(k) / (n - 1); p.push_back(a + t * (b - a)); }
    return p;
}
// build a node, drive its in-node param widgets (literal path), set Start/Goal, fire PLAN, return its frozen plan.
krs::plan::PlanResult planWith(const Eigen::VectorXd& qA, const Eigen::VectorXd& qB,
                               int plannerIdx, int maxIters, int waypoints) {
    OMPLPlannerNode m;
    m.setPortLiteral<int>("Planner", plannerIdx);   // the combo selection (Phase-1 widget literal)
    m.setPortLiteral<int>("Max Iters", maxIters);   // the spin box value
    m.setPortLiteral<int>("Waypoints", waypoints);
    setJointIn(m, "Start", qA); setJointIn(m, "Goal", qB);
    pulse(m, "Plan");
    return m.lastPlan();
}

} // namespace

bool runOmplPlannerGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ompl] GATE OMPL (two-stage PLAN/EXECUTE; in-node planner params; collision-free plan)\n");

    OMPLPlannerNode probe;
    const krs::dyn::SerialChain& chain = probe.chain();
    const krs::plan::CollisionWorld& world = probe.world();
    const krs::plan::JointLimits& lim = probe.lim();

    Eigen::VectorXd qA(3); qA << -1.4, 0.0, 0.0;     // yawed left, clear of the box
    Eigen::VectorXd qB(3); qB << +1.4, 0.0, 0.0;     // yawed right, clear of the box
    const double penA = world.maxPenetration(chain, qA), penB = world.maxPenetration(chain, qB);
    const bool endpointsOk = penA < 1e-6 && penB < 1e-6;   // else the gate would be vacuous (in-collision endpoints)

    // ===================== OMPL-TWO-STAGE =====================
    OMPLPlannerNode n;
    n.setPortLiteral<int>("Planner", 0); n.setPortLiteral<int>("Max Iters", 6000); n.setPortLiteral<int>("Waypoints", 64);
    setJointIn(n, "Start", qA); setJointIn(n, "Goal", qB);

    // STAGE 1: PLAN only -- the robot must NOT move.
    pulse(n, "Plan");
    for (int i = 0; i < 6; ++i) { setBoolIn(n, "Plan", false); setBoolIn(n, "Execute", false); n.process(); } // idle, no execute
    const bool planned        = rOutB(n, "Planned");
    const Eigen::VectorXd cmdAfterPlan = rOutCmd(n);
    const bool noMoveOnPlan   = cmdAfterPlan.size() == qA.size() && (cmdAfterPlan - qA).norm() < 1e-9;
    // the frozen path really goes somewhere -> "stayed at qA on PLAN" is a non-vacuous constraint.
    const auto& wps = n.lastPlan().waypoints;
    const Eigen::VectorXd planMid = wps.empty() ? qA : wps[wps.size() / 2];
    const double planMidMove = (planMid - qA).norm();           // a plan-and-execute-in-one model would output this, != qA

    // STAGE 2: EXECUTE -- now drive the command along the path to the goal.
    setBoolIn(n, "Execute", false); n.process();
    setBoolIn(n, "Execute", true);  n.process();                // execute rising edge (latches m_executing)
    setBoolIn(n, "Execute", false);
    for (int i = 0; i < 120; ++i) { n.process(); if (!rOutB(n, "Executing") && rOutD(n, "Progress") > 0.999) break; }
    const Eigen::VectorXd cmdAfterExec = rOutCmd(n);
    const double execProg     = rOutD(n, "Progress");
    const bool executedToGoal = cmdAfterExec.size() == qB.size() && (cmdAfterExec - qB).norm() < 1e-6 && execProg > 0.999;

    // STAGE 3: EXECUTE with no prior plan -> graceful no-op (robot stays at start).
    OMPLPlannerNode n2;
    setJointIn(n2, "Start", qA); setJointIn(n2, "Goal", qB);
    setBoolIn(n2, "Execute", false); n2.process();
    setBoolIn(n2, "Execute", true);  n2.process();
    setBoolIn(n2, "Execute", false);
    for (int i = 0; i < 10; ++i) n2.process();
    const Eigen::VectorXd cmdNoPlan = rOutCmd(n2);
    const bool gracefulNoPlan = !rOutB(n2, "Planned")
                             && cmdNoPlan.size() == qA.size() && (cmdNoPlan - qA).norm() < 1e-9;

    // NEG-CTRL: a SINGLE-trigger "plan-and-immediately-execute" model moves on PLAN (cmdAfterPlan == planMid != qA).
    // We proved noMoveOnPlan (== qA) while the path genuinely advances (planMidMove large) -> the two stages are
    // distinct, not a vacuous "nothing happens".
    const bool twoStageNeg = planMidMove > 1e-2;
    const bool twoStageOk  = endpointsOk && planned && noMoveOnPlan && executedToGoal && gracefulNoPlan && twoStageNeg;

    // ===================== OMPL-PARAMS-IN-NODE =====================
    // (a) the Planner combo is READ: RRT-Connect vs RRT* give different paths (deterministic, same seed).
    const auto rc = planWith(qA, qB, 0, 6000, 64);     // RRT-Connect
    const auto rs = planWith(qA, qB, 1, 6000, 64);     // RRT*
    const bool plannerRead = rc.solved && rs.solved
                          && std::abs(rc.pathLength - rs.pathLength) > 1e-3   // the combo selection changes the plan
                          && rs.pathLength <= rc.pathLength * 1.25;           // the optimizing planner is no worse
    // (b) the Waypoints spin is READ: 24 vs 160 changes the path resolution deterministically.
    const auto wLo = planWith(qA, qB, 0, 6000, 24);
    const auto wHi = planWith(qA, qB, 0, 6000, 160);
    const bool waypointsRead = wLo.solved && wHi.solved
                            && wLo.waypoints.size() >= 24 && wHi.waypoints.size() >= 160
                            && wHi.waypoints.size() > wLo.waypoints.size() + 50;
    // (c) the Max Iters spin is READ: RRT* with a larger budget refines the path (different length, same seed).
    const auto bLo = planWith(qA, qB, 1, 1500, 64);
    const auto bHi = planWith(qA, qB, 1, 9000, 64);
    const bool maxItersRead = bLo.solved && bHi.solved && std::abs(bLo.pathLength - bHi.pathLength) > 1e-3;
    // NEG-CTRL: a model that ignored these widgets would return IDENTICAL plans -> every "differ" check above fails.
    const bool paramsOk = plannerRead && waypointsRead && maxItersRead;

    // ===================== OMPL-PLAN-VALID =====================
    const auto plan = planWith(qA, qB, 0, 6000, 128);   // RRT-Connect
    const double plannedPen = plan.solved ? pathMaxPen(chain, world, plan.waypoints) : 1e30;
    const double posMargin  = plan.solved ? krs::plan::positionMargin(plan.waypoints, lim) : -1e30;
    const std::vector<Eigen::VectorXd> naive = straightLine(qA, qB, 128);
    const double naivePen = pathMaxPen(chain, world, naive);     // the box obstacle -> the straight line collides
    const bool planValidOk = plan.solved && plannedPen < 1e-6 && posMargin >= -1e-9 && naivePen > 1e-3;

    const bool pass = twoStageOk && paramsOk && planValidOk;

    printf("[ompl]   TWO-STAGE: planned=%d; PLAN->no-move(cmd==start):%d (path advances %.3f rad, so non-vacuous:%d); "
           "EXECUTE->goal prog=%.3f reached:%d; EXEC-no-plan graceful:%d  %s\n",
           int(planned), int(noMoveOnPlan), planMidMove, int(twoStageNeg), execProg, int(executedToGoal),
           int(gracefulNoPlan), twoStageOk ? "PASS" : "FAIL");
    printf("[ompl]   PARAMS-IN-NODE: Planner combo len RRTConnect=%.4f vs RRT*=%.4f (read:%d); "
           "Waypoints 24->%zu vs 160->%zu (read:%d); MaxIters RRT* 1500=%.4f vs 9000=%.4f (read:%d)  %s\n",
           rc.pathLength, rs.pathLength, int(plannerRead), wLo.waypoints.size(), wHi.waypoints.size(),
           int(waypointsRead), bLo.pathLength, bHi.pathLength, int(maxItersRead), paramsOk ? "PASS" : "FAIL");
    printf("[ompl]   PLAN-VALID: planned-path pen=%.6f (<1e-6:%d) posMargin=%.4f (>=0); "
           "NEG straight-line-through-box pen=%.4f (>1e-3:%d)  %s\n",
           plannedPen, int(plannedPen < 1e-6), posMargin, naivePen, int(naivePen > 1e-3), planValidOk ? "PASS" : "FAIL");
    printf("[ompl] %s\n", pass ? "ALL PASS (PLAN freezes a path without moving; EXECUTE drives to goal; no-plan graceful; "
                                 "in-node params change the plan; planned path collision-free, straight-line collides)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
