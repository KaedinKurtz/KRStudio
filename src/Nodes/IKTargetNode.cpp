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

#include <Eigen/Dense>
#include <glm/glm.hpp>

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
        const glm::vec3 pos = getInput<glm::vec3>("Position").value_or(glm::vec3(0.0f));
        const glm::vec3 rpy = getInput<glm::vec3>("Orientation").value_or(glm::vec3(0.0f));
        krs::dyn::Pose target; target.p = Eigen::Vector3d(pos.x, pos.y, pos.z); target.R = rpyToR(rpy);
        Eigen::VectorXd q = m_seed;
        m_ik = m_chain.ik(target, m_ee, q);
        m_goal = q; m_sampled = pos;
        if (m_ik.ok) m_seed = q;                                   // warm-start the next solve
        setOutput<double>("TargetX", double(m_sampled.x));
        setOutput<double>("TargetY", double(m_sampled.y));
        setOutput<double>("TargetZ", double(m_sampled.z));
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
    glm::vec3 m_sampled{ 0.0f };
    bool m_haveSample = false, m_lastTrigger = false;
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

} // namespace krs::nodes
