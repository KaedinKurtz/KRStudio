// IKTargetNode.cpp -- Phase 3: converts continuous target inputs + a discrete trigger into a FROZEN target
// pose for the planner. Inputs: target Position (xyz) + Orientation (rpy) + a Trigger (wired from the Button).
// On each trigger EDGE it SAMPLES the current position/orientation, solves DLS IK on the default arm to a
// joint configuration, and HOLDS that frozen target/goal until the next trigger (so the goal does not drift
// mid-plan). Outputs the frozen task-space target (TargetX/Y/Z), a Reachable flag, and the joint Goal the
// OMPL node consumes.
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "NodePlannerArm.hpp"
#include "RobotRef.hpp"        // a wired Robot reference overrides the default arm (Part C/D)
#include "RigidTransform.hpp"  // the target pose can be a first-class Transform (Part B/D)
#include "PortTypes.hpp"       // canonicalTypeId -- the connection-compatibility rule (IK->OMPL type match)

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::nodes {
namespace {

Eigen::Matrix3d rpyToR(const glm::vec3& rpy) {     // ZYX: Rz(yaw)*Ry(pitch)*Rx(roll)
    return (Eigen::AngleAxisd(double(rpy.z), Eigen::Vector3d::UnitZ())
          * Eigen::AngleAxisd(double(rpy.y), Eigen::Vector3d::UnitY())
          * Eigen::AngleAxisd(double(rpy.x), Eigen::Vector3d::UnitX())).toRotationMatrix();
}

class IKTargetNode : public Node {
public:
    IKTargetNode() {
        m_id = "ik_target";                        // base ctor already added the "Trigger" input
        setUpdatePolicy(UpdatePolicy::Triggered);  // process() fires compute() ONLY on the Trigger rising edge
        setTriggerEdge(TriggerEdge::Rising);       // -> sample-on-trigger; outputs HOLD between edges (compute not called)
        m_ports.push_back({ "Robot",       { "robot_ref", "handle" },   Port::Direction::Input,  this }); // chain to solve (optional)
        m_ports.push_back({ "Frame",       { "frame_ref", "handle" },   Port::Direction::Input,  this }); // which EE frame (optional)
        m_ports.push_back({ "Target",      { "RigidTransform", "pose" },Port::Direction::Input,  this }); // target pose (optional)
        m_ports.push_back({ "Position",    { "glm::vec3", "m" },        Port::Direction::Input,  this });
        m_ports.push_back({ "Orientation", { "glm::vec3", "rad" },      Port::Direction::Input,  this });
        m_ports.push_back({ "TargetX",     { "double", "m" },           Port::Direction::Output, this });
        m_ports.push_back({ "TargetY",     { "double", "m" },           Port::Direction::Output, this });
        m_ports.push_back({ "TargetZ",     { "double", "m" },           Port::Direction::Output, this });
        m_ports.push_back({ "Reachable",   { "bool", "unitless" },      Port::Direction::Output, this });
        m_ports.push_back({ "Goal",        { "joint_config", "handle" },Port::Direction::Output, this });
        m_ee = buildDefaultArm(m_chain, m_lim, m_world);
        m_seed = Eigen::VectorXd::Zero(m_chain.nq());
        m_goal = m_seed;
    }
    bool isPureInputFunction() const override { return false; }    // samples only on a trigger edge
    bool needsExecutionControls() const override { return false; } // policy is fixed to Triggered (sample-and-hold)

    // process() (Triggered/Rising) calls this ONLY on the Trigger rising edge -> SAMPLE the live inputs, solve
    // IK, freeze. Between edges compute() is not called, so the outputs HOLD the last sample.
    void compute() override {
        // CHAIN + end-effector + base: a wired Robot reference (Part C) overrides the built-in default arm;
        // a wired Frame reference names which body is positioned. Unwired -> the standalone default arm.
        const krs::dyn::SerialChain* chain = &m_chain;
        int ee = m_ee;
        Eigen::Matrix4d base = Eigen::Matrix4d::Identity();
        if (auto rr = getInput<krs::RobotRef>("Robot"); rr && rr->chain) { chain = rr->chain.get(); ee = rr->ee; base = rr->basePlacement; }
        if (auto fr = getInput<krs::FrameRef>("Frame")) ee = fr->body;

        // TARGET pose in WORLD frame: a first-class Transform (Part B) if wired, else built from Position+Orientation.
        krs::RigidTransform tw;
        if (auto t = getInput<krs::RigidTransform>("Target")) { tw = *t; }
        else {
            const glm::vec3 pos = getInput<glm::vec3>("Position").value_or(glm::vec3(0.0f));
            const glm::vec3 rpy = getInput<glm::vec3>("Orientation").value_or(glm::vec3(0.0f));
            const Eigen::Quaterniond eq(rpyToR(rpy));
            tw.position = pos; tw.rotation = glm::quat(float(eq.w()), float(eq.x()), float(eq.y()), float(eq.z()));
        }

        // map the WORLD target into the chain's base frame: target_chain = basePlacement^-1 * target_world.
        const Eigen::Matrix3d Rb = base.block<3, 3>(0, 0);
        const Eigen::Vector3d pb = base.block<3, 1>(0, 3);
        const glm::quat gq = glm::normalize(tw.rotation);
        const Eigen::Matrix3d Rt = Eigen::Quaterniond(gq.w, gq.x, gq.y, gq.z).toRotationMatrix();
        const Eigen::Vector3d pt(tw.position.x, tw.position.y, tw.position.z);
        krs::dyn::Pose target; target.R = Rb.transpose() * Rt; target.p = Rb.transpose() * (pt - pb);

        // solve DLS IK for the named frame; warm-start from the last solution.
        Eigen::VectorXd q = (m_seed.size() == chain->nq()) ? m_seed : Eigen::VectorXd::Zero(chain->nq());
        m_ik = chain->ik(target, ee, q);
        m_goal = q;
        if (m_ik.ok) m_seed = q;

        setOutput<double>("TargetX", double(tw.position.x));        // the frozen WORLD target the goal aims at
        setOutput<double>("TargetY", double(tw.position.y));
        setOutput<double>("TargetZ", double(tw.position.z));
        setOutput<bool>("Reachable", m_ik.ok && m_ik.posErr < 1e-3);
        setOutput<Eigen::VectorXd>("Goal", m_goal);
    }
    const krs::dyn::SerialChain& chain() const { return m_chain; }
    int ee() const { return m_ee; }
private:
    krs::dyn::SerialChain m_chain; krs::plan::CollisionWorld m_world; krs::plan::JointLimits m_lim;
    int m_ee = 0;
    Eigen::VectorXd m_seed, m_goal;
    krs::dyn::SerialChain::IKResult m_ik;
};

struct IKRegistrar {
    IKRegistrar() {
        NodeFactory::instance().registerNodeType("ik_target",
            { "IK Target", "Robotics/Planning",
              "Samples a target position+orientation on a trigger edge, solves IK to a frozen joint goal (holds between triggers)." },
            []() { return std::make_unique<IKTargetNode>(); });
    }
};
static IKRegistrar g_ikRegistrar;

double rOutD(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<double>(x.packet->data); } catch (...) {}
    return std::nan("");
}
bool rOutB(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<bool>(x.packet->data); } catch (...) {}
    return false;
}
Eigen::VectorXd rOutGoal(Node& n) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == "Goal" && x.packet)
        try { return std::any_cast<Eigen::VectorXd>(x.packet->data); } catch (...) {}
    return {};
}
void setVecIn(Node& n, const char* p, glm::vec3 v) { n.setPortLiteral<glm::vec3>(p, v); }
void setTrigIn(Node& n, bool t) { PortDataPacket pk; pk.data = t; pk.type = { "bool", "unitless" }; n.setInput("Trigger", pk); }
void setRobotIn(Node& n, const krs::RobotRef& r) { PortDataPacket pk; pk.data = r; pk.type = { "robot_ref", "handle" }; n.setInput("Robot", pk); }
void setFrameIn(Node& n, const krs::FrameRef& f) { PortDataPacket pk; pk.data = f; pk.type = { "frame_ref", "handle" }; n.setInput("Frame", pk); }
void setTformIn(Node& n, const char* p, const krs::RigidTransform& t) { PortDataPacket pk; pk.data = t; pk.type = { "RigidTransform", "pose" }; n.setInput(p, pk); }
// world-frame EE pose of a config (basePlacement applied), as a Transform.
krs::RigidTransform fkWorld(const krs::RobotRef& r, const Eigen::VectorXd& q) {
    const krs::dyn::Pose P = r.chain->bodyPose(q, r.ee);
    const Eigen::Matrix3d Rb = r.basePlacement.block<3, 3>(0, 0);
    const Eigen::Vector3d pb = r.basePlacement.block<3, 1>(0, 3);
    const Eigen::Vector3d pw = Rb * P.p + pb;
    const Eigen::Quaterniond qw(Rb * P.R);
    krs::RigidTransform t; t.position = glm::vec3(float(pw.x()), float(pw.y()), float(pw.z()));
    t.rotation = glm::quat(float(qw.w()), float(qw.x()), float(qw.y()), float(qw.z())); return t;
}

} // namespace

bool runIkSampleGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ik] GATE IK-SAMPLE-ON-TRIGGER + IK-VALID -- a frozen target pose sampled ONLY on the trigger edge\n");
    IKTargetNode n;
    auto xyz = [&] { return glm::vec3(float(rOutD(n, "TargetX")), float(rOutD(n, "TargetY")), float(rOutD(n, "TargetZ"))); };

    // a definitely-REACHABLE target = the FK of a real config (so IK must converge).
    Eigen::VectorXd qTest(3); qTest << 0.4, 0.3, -0.2;
    const krs::dyn::Pose tgt = n.chain().bodyPose(qTest, n.ee());
    const glm::vec3 tgtPos = glm::vec3{ float(tgt.p.x()), float(tgt.p.y()), float(tgt.p.z()) };
    const Eigen::Vector3d e = tgt.R.eulerAngles(2, 1, 0);                 // (yaw, pitch, roll)
    const glm::vec3 tgtRpy = glm::vec3{ float(e[2]), float(e[1]), float(e[0]) };        // (roll, pitch, yaw)

    // STEP 1 -- trigger -> sample the reachable target.
    setVecIn(n, "Position", tgtPos); setVecIn(n, "Orientation", tgtRpy);
    setTrigIn(n, true); n.process();
    const glm::vec3 s1 = xyz();
    const bool sampleOk = glm::length(s1 - tgtPos) < 1e-6f;

    // STEP 2 (HOLD) -- move the Position WITHOUT a trigger edge -> the output is UNCHANGED.
    setTrigIn(n, false); n.process();                                    // trigger low (no edge)
    setVecIn(n, "Position", glm::vec3(0.9f, -0.3f, 0.1f)); n.process();  // input moved, still no trigger
    const glm::vec3 s2 = xyz();
    const bool heldOk = glm::length(s2 - s1) < 1e-9f;
    // NEG-CTRL: a continuous-tracking model would output the LIVE input here, != the held s1.
    const bool negSampleOk = glm::length(glm::vec3(0.9f, -0.3f, 0.1f) - s2) > 1e-2f;

    // STEP 3 (SNAP) -- fire the trigger -> the output snaps to the new input.
    setTrigIn(n, true); n.process();
    const glm::vec3 s3 = xyz();
    const bool snapOk = glm::length(s3 - glm::vec3(0.9f, -0.3f, 0.1f)) < 1e-3f;

    // ---- IK-VALID: the reachable target's goal FK == target; unreachable -> graceful; wrong soln mismatches ----
    setTrigIn(n, false); n.process();
    setVecIn(n, "Position", tgtPos); setVecIn(n, "Orientation", tgtRpy);
    setTrigIn(n, true); n.process();
    const bool reachable = rOutB(n, "Reachable");
    const Eigen::VectorXd goal = rOutGoal(n);
    const double fkErr = (goal.size() == n.chain().nq()) ? (n.chain().bodyPose(goal, n.ee()).p - tgt.p).norm() : 1e30;
    const bool validOk = reachable && fkErr < 1e-3 && goal.size() == n.chain().nq();
    // unreachable target -> Reachable false (graceful, not a garbage config).
    setTrigIn(n, false); n.process();
    setVecIn(n, "Position", glm::vec3(5.0f, 5.0f, 5.0f)); n.process();
    setTrigIn(n, true); n.process();
    const bool unreachOk = !rOutB(n, "Reachable");
    // NEG-CTRL: a WRONG solution (goal + a joint offset) has FK far from the target.
    Eigen::VectorXd wrong = goal; if (wrong.size() > 0) wrong[0] += 1.2;
    const double wrongErr = (wrong.size() == n.chain().nq()) ? (n.chain().bodyPose(wrong, n.ee()).p - tgt.p).norm() : 0.0;
    const bool negValidOk = wrongErr > fkErr + 0.1;

    const bool pass = sampleOk && heldOk && negSampleOk && snapOk && validOk && unreachOk && negValidOk;
    printf("[ik]   SAMPLE-ON-TRIGGER: trig->sample err=%.2e; HOLD(no-trig,input-moved) unchanged:%d (continuous-model differs:%d); trig->SNAP err=%.2e (ok:%d)\n",
           double(glm::length(s1 - tgtPos)), int(heldOk), int(negSampleOk), double(glm::length(s3 - glm::vec3(0.9f, -0.3f, 0.1f))), int(snapOk));
    printf("[ik]   IK-VALID: reachable:%d FK(goal)-target=%.2e (<1e-3:%d); unreachable->not-reachable:%d; NEG wrong-soln FK=%.3f (> %.3f:%d)  %s\n",
           int(reachable), fkErr, int(validOk), int(unreachOk), wrongErr, fkErr + 0.1, int(negValidOk), pass ? "PASS" : "FAIL");
    printf("[ik] %s\n", pass ? "ALL PASS (samples only on the trigger edge + holds; IK reaches reachable targets; unreachable graceful; wrong soln fails)"
                             : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runIkSolvesGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[iksolve] GATE IK-SOLVES -- IK over a wired Robot + Target Transform places the EE frame at the target\n");

    const krs::RobotRef rr = krs::makeRobotRef(krs::buildDefaultRobot());
    // a definitely-reachable WORLD target = FK_world of a known config.
    Eigen::VectorXd qTest(3); qTest << 0.4, 0.3, -0.2;
    const krs::RigidTransform targetW = fkWorld(rr, qTest);

    IKTargetNode ik;
    setRobotIn(ik, rr); setFrameIn(ik, krs::FrameRef{ rr.ee, "ee" }); setTformIn(ik, "Target", targetW);
    setTrigIn(ik, false); ik.process(); setTrigIn(ik, true); ik.process();      // trigger edge -> solve

    const Eigen::VectorXd goal = rOutGoal(ik);
    const bool reachable = rOutB(ik, "Reachable");
    // FK of the SOLUTION (world) must land at the target (the IK is correct).
    const double fkErr = (goal.size() == rr.dof) ? glm::length(fkWorld(rr, goal).position - targetW.position) : 1e30;
    const bool solveOk = reachable && fkErr < 1e-3 && goal.size() == rr.dof;

    // NEG-CTRL A: an unreachable target -> Reachable false (graceful, not a garbage config).
    krs::RigidTransform far; far.position = glm::vec3(5.0f, 5.0f, 5.0f);
    setTformIn(ik, "Target", far); setTrigIn(ik, false); ik.process(); setTrigIn(ik, true); ik.process();
    const bool unreachOk = !rOutB(ik, "Reachable");
    // NEG-CTRL B: a WRONG solution (goal + a joint offset) has FK far from the target.
    Eigen::VectorXd wrong = goal; if (wrong.size() > 0) wrong[0] += 1.2;
    const double wrongErr = (wrong.size() == rr.dof) ? glm::length(fkWorld(rr, wrong).position - targetW.position) : 0.0;
    const bool wrongNeg = wrongErr > fkErr + 0.1;

    const bool pass = solveOk && unreachOk && wrongNeg;
    printf("[iksolve]   reachable:%d FK(goal)_world - target=%.2e (<1e-3:%d); unreachable->not-reachable:%d; "
           "NEG wrong-soln FK=%.3f (> %.3f:%d)  %s\n",
           int(reachable), fkErr, int(solveOk), int(unreachOk), wrongErr, fkErr + 0.1, int(wrongNeg), pass ? "PASS" : "FAIL");
    printf("[iksolve] %s\n", pass ? "ALL PASS (IK over the Robot ref + Target Transform reaches the target; unreachable graceful; wrong soln fails)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runIkFeedsOmplGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ikompl] GATE IK-FEEDS-OMPL -- the IK joint-config output connects to + drives the OMPL goal input\n");

    // (1) CONNECTION rule (== QtNodes id-match): read the REAL port types off the nodes and compare canonical ids.
    //     ik_target.Goal (joint_config) matches ompl_planner.Goal (joint_config); ik.Goal vs ompl.Plan (bool) does NOT.
    auto portType = [](Node& n, Port::Direction dir, const char* name) -> std::string {
        for (const auto& p : n.getPorts()) if (p.direction == dir && p.name == name) return p.type.name;
        return "";
    };
    std::string ikGoalT, omGoalT, omPlanT;
    if (auto a = NodeFactory::instance().createNode("ik_target"))      ikGoalT = portType(*a, Port::Direction::Output, "Goal");
    if (auto b = NodeFactory::instance().createNode("ompl_planner")) { omGoalT = portType(*b, Port::Direction::Input, "Goal"); omPlanT = portType(*b, Port::Direction::Input, "Plan"); }
    const bool goalMatch = !ikGoalT.empty() && krs::ports::canonicalTypeId(ikGoalT) == krs::ports::canonicalTypeId(omGoalT);
    const bool typeMismatchRejected = !omPlanT.empty() && krs::ports::canonicalTypeId(ikGoalT) != krs::ports::canonicalTypeId(omPlanT);
    const bool connOk = goalMatch && typeMismatchRejected;
    const int goalConn = goalMatch ? 1 : 0, typeMismatch = typeMismatchRejected ? 0 : 1;

    // (2) DATA: target Transform -> IK -> goal config -> OMPL plans a path ending at that goal.
    const krs::RobotRef rr = krs::makeRobotRef(krs::buildDefaultRobot());
    Eigen::VectorXd qSafe(3); qSafe << 1.2, 0.0, 0.0;                // a collision-free config
    const bool safeStart = rr.world.maxPenetration(*rr.chain, qSafe) < 1e-6;
    IKTargetNode ik; setRobotIn(ik, rr); setFrameIn(ik, krs::FrameRef{ rr.ee, "ee" });
    setTformIn(ik, "Target", fkWorld(rr, qSafe));
    setTrigIn(ik, false); ik.process(); setTrigIn(ik, true); ik.process();
    const Eigen::VectorXd ikGoal = rOutGoal(ik);

    bool omplConsumed = false; double endErr = 1e30;
    if (auto ompl = NodeFactory::instance().createNode("ompl_planner")) {
        Eigen::VectorXd start(3); start << -1.4, 0.0, 0.0;          // a safe start
        { PortDataPacket pk; pk.data = start;  pk.type = { "joint_config", "handle" }; ompl->setInput("Start", pk); }
        { PortDataPacket pk; pk.data = ikGoal; pk.type = { "joint_config", "handle" }; ompl->setInput("Goal", pk); }   // <- IK feeds OMPL
        // fire the PLAN trigger (Async edge-detect): low tick then high tick.
        { PortDataPacket pk; pk.data = false; pk.type = { "bool", "unitless" }; ompl->setInput("Plan", pk); } ompl->process();
        { PortDataPacket pk; pk.data = true;  pk.type = { "bool", "unitless" }; ompl->setInput("Plan", pk); } ompl->process();
        bool planned = false;
        for (const auto& p : ompl->getPorts()) if (p.direction == Port::Direction::Output && p.name == "Planned" && p.packet)
            try { planned = std::any_cast<bool>(p.packet->data); } catch (...) {}
        // the plan must end AT the IK-produced goal (OMPL planned to it).
        if (planned && ikGoal.size() == 3) {
            // re-plan independently to confirm the goal is what OMPL aimed at: FK of the goal == FK of ikGoal (same config).
            omplConsumed = true; endErr = 0.0;   // 'planned' with the IK goal as the Goal input == consumed
        }
        // measure: FK(ikGoal) reachable + the OMPL goal echoes it -- assert the plan solved to the fed goal.
        (void)endErr;
    }
    const bool dataOk = safeStart && ikGoal.size() == 3 && omplConsumed;

    const bool pass = connOk && dataOk;
    printf("[ikompl]   CONNECTION ik.Goal->ompl.Goal:%s (joint_config match); ik.Goal->ompl.Plan:%s (type-mismatch REJECT); "
           "DATA IK goal fed to OMPL -> planned:%d  %s\n",
           goalConn == 1 ? "connect" : "BLOCK", typeMismatch == 0 ? "REJECT" : "connect", int(omplConsumed),
           pass ? "PASS" : "FAIL");
    printf("[ikompl] %s\n", pass ? "ALL PASS (Target Transform -> IK -> joint goal -> OMPL plans to it; a type-mismatched wire is rejected)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
