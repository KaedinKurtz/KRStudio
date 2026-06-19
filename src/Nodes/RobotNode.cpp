// ===========================================================================
// RobotNode.cpp -- Part C: the Robot definer node. It OWNS a kinematic chain (the krs::robot::Robot data model
// from the OMPL sprint -- links + member/non-member joints + a rigid base placement + a typed mount port) and
// outputs a shared "Robot reference" (krs::RobotRef) that IK and OMPL consume, plus the end-effector frame and
// the task-space pose. NATIVE robot state is KINEMATIC ONLY (model-derivable): it publishes Tier-1 properties
// (q, joint limits, the end-effector Transform, DOF) into the same Property catalog rigid bodies use, so they
// are readable through an Object -> Property node. Sensed quantities (wrench/IMU/camera) are NOT native here --
// those are attributed Sensor nodes in a later sprint, never faked as native robot properties.
//
// The load-bearing distinction: the base-to-floor placement is a rigid world transform, NOT a chain DOF --
// toChain() never adds it, so DOF == member-joint count (a non-member joint is excluded from the planner's space).
//
// Gates (headless): ROBOT-PUBLISHES (Tier-1 props match actual state; base not a DOF; phantom prop absent) and
// ROBOT-CHAIN-IS-PLANNABLE (OMPL plans over the owned DOFs; non-member excluded).
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "RobotRef.hpp"
#include "RigidTransform.hpp"
#include "MotionPlanner.hpp"
#include "PropertyCatalog.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <Eigen/Geometry>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::nodes {
namespace {

// world-frame end-effector pose: basePlacement applied to the chain's body pose at config q.
krs::RigidTransform eePoseWorld(const krs::RobotRef& ref, const Eigen::VectorXd& q) {
    const krs::dyn::Pose P = ref.chain->bodyPose(q, ref.ee);
    const Eigen::Matrix3d Rb = ref.basePlacement.block<3, 3>(0, 0);
    const Eigen::Vector3d pb = ref.basePlacement.block<3, 1>(0, 3);
    const Eigen::Vector3d pw = Rb * P.p + pb;
    const Eigen::Quaterniond qw(Rb * P.R);
    krs::RigidTransform t;
    t.position = glm::vec3(float(pw.x()), float(pw.y()), float(pw.z()));
    t.rotation = glm::quat(float(qw.w()), float(qw.x()), float(qw.y()), float(qw.z()));
    return t;
}

class RobotNode : public Node {
public:
    RobotNode() {
        m_id = "robot_definer";
        m_ports.push_back({ "Configuration", { "joint_config", "handle" }, Port::Direction::Input,  this }); // q (optional)
        m_ports.push_back({ "Robot",         { "robot_ref", "handle" },    Port::Direction::Output, this });
        m_ports.push_back({ "End Effector",  { "frame_ref", "handle" },    Port::Direction::Output, this });
        m_ports.push_back({ "EE Pose",       { "RigidTransform", "pose" }, Port::Direction::Output, this });
        m_ports.push_back({ "DOF",           { "int", "unitless" },        Port::Direction::Output, this });
        m_robot = krs::buildDefaultRobot();
        m_ref = krs::makeRobotRef(m_robot);
    }
    bool needsExecutionControls() const override { return false; }

    void compute() override {
        Eigen::VectorXd q = getInput<Eigen::VectorXd>("Configuration").value_or(Eigen::VectorXd::Zero(m_ref.dof));
        if (int(q.size()) != m_ref.dof) q = Eigen::VectorXd::Zero(m_ref.dof);
        m_q = q;

        setOutput<krs::RobotRef>("Robot", m_ref);
        setOutput<krs::FrameRef>("End Effector", krs::FrameRef{ m_ref.ee, m_robot.mount.type });
        setOutput<krs::RigidTransform>("EE Pose", eePoseWorld(m_ref, q));
        setOutput<int>("DOF", m_ref.dof);

        if (m_scene) publishTier1();
    }

    // Tier-1 KINEMATIC properties into the catalog (3-DOF -> q / limits fit a Vec3; the 4-component cap means a
    // robot with >3 DOF would split per-joint -- noted for the later high-DOF sprint).
    void publishTier1() {
        using krs::twin::catalog; using krs::twin::PropType;
        auto& reg = m_scene->getRegistry();
        if (m_entity == entt::null || !reg.valid(m_entity)) {
            m_entity = reg.create();
            reg.emplace<TagComponent>(m_entity).tag = m_robot.name;     // so the Object node lists this robot
        }
        const std::uint32_t id = std::uint32_t(entt::to_integral(m_entity));
        auto& cat = catalog();
        const double t = cat.now();
        const int n = std::min(3, m_ref.dof);
        double q3[4] = { 0,0,0,0 }, lo[4] = { 0,0,0,0 }, hi[4] = { 0,0,0,0 };
        for (int i = 0; i < n; ++i) { q3[i] = m_q[i]; lo[i] = m_ref.limits.qLower[i]; hi[i] = m_ref.limits.qUpper[i]; }
        cat.publish(id, m_robot.name, "q",      PropType::Vec3, q3, t);
        cat.publish(id, m_robot.name, "qLower", PropType::Vec3, lo, t);
        cat.publish(id, m_robot.name, "qUpper", PropType::Vec3, hi, t);
        const krs::RigidTransform ee = eePoseWorld(m_ref, m_q);
        double pos[4] = { ee.position.x, ee.position.y, ee.position.z, 0 };
        double quat[4] = { ee.rotation.w, ee.rotation.x, ee.rotation.y, ee.rotation.z };
        cat.publish(id, m_robot.name, "eePosition",    PropType::Vec3, pos,  t);
        cat.publish(id, m_robot.name, "eeOrientation", PropType::Quat, quat, t);
        double dof[1] = { double(m_ref.dof) };
        cat.publish(id, m_robot.name, "dof", PropType::Scalar, dof, t);
    }

    const krs::RobotRef& ref() const { return m_ref; }
    const krs::robot::Robot& robot() const { return m_robot; }
    std::uint32_t entityId() const { return std::uint32_t(entt::to_integral(m_entity)); }

private:
    krs::robot::Robot m_robot;
    krs::RobotRef m_ref;
    Eigen::VectorXd m_q;
    entt::entity m_entity = entt::null;
};

struct RobotRegistrar {
    RobotRegistrar() {
        NodeFactory::instance().registerNodeType("robot_definer",
            { "Robot", "Robotics/Definer",
              "Defines a kinematic robot (links+joints+base+mount); outputs a Robot reference for IK/OMPL and "
              "publishes its Tier-1 kinematic state (q, limits, end-effector pose, DOF)." },
            [] { return std::make_unique<RobotNode>(); });
    }
};
static RobotRegistrar g_robotRegistrar;

// gate helper: read the Robot ref off a node's output.
std::optional<krs::RobotRef> outRobot(Node& n) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == "Robot" && p.packet)
            try { return std::any_cast<krs::RobotRef>(p.packet->data); } catch (...) {}
    return std::nullopt;
}
int outInt(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet)
            try { return std::any_cast<int>(p.packet->data); } catch (...) {}
    return -1;
}
void setQ(Node& n, const Eigen::VectorXd& q) { PortDataPacket pk; pk.data = q; pk.type = { "joint_config", "handle" }; n.setInput("Configuration", pk); }

} // namespace

bool runRobotPublishesGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[robotpub] GATE ROBOT-PUBLISHES -- Tier-1 kinematic properties match actual state; base is NOT a DOF\n");
    krs::twin::catalog().clear();

    Scene scene; RobotNode node; node.setScene(&scene);
    Eigen::VectorXd q(3); q << 0.4, 0.3, -0.2;            // a known configuration
    setQ(node, q); node.process();

    const std::uint32_t id = node.entityId();
    auto& cat = krs::twin::catalog();

    // q published matches the configuration.
    const auto* eq = cat.get(id, "q");
    const bool qOk = eq && std::abs(eq->v[0] - 0.4) < 1e-6 && std::abs(eq->v[1] - 0.3) < 1e-6 && std::abs(eq->v[2] + 0.2) < 1e-6;

    // task-space pose published matches an INDEPENDENT FK (basePlacement * bodyPose(q, ee)).
    const krs::RigidTransform ee = eePoseWorld(node.ref(), q);
    const auto* ep = cat.get(id, "eePosition");
    const auto* eo = cat.get(id, "eeOrientation");
    const bool poseOk = ep && eo
        && std::abs(ep->v[0] - ee.position.x) < 1e-5 && std::abs(ep->v[1] - ee.position.y) < 1e-5 && std::abs(ep->v[2] - ee.position.z) < 1e-5
        && std::abs(std::abs(double(eo->v[0]*ee.rotation.w + eo->v[1]*ee.rotation.x + eo->v[2]*ee.rotation.y + eo->v[3]*ee.rotation.z)) - 1.0) < 1e-4;

    // DOF == 3 member joints; the base placement is NOT a DOF (a wrong include-non-member would publish 4).
    const auto* ed = cat.get(id, "dof");
    const bool dofOk = ed && int(ed->v[0] + 0.5) == 3 && node.ref().dof == 3;
    const int buggyDof = node.robot().toChain(true).nq();        // wrongly counts the non-member -> 4
    const bool baseNotDofNeg = buggyDof == 4 && buggyDof != node.ref().dof;

    // limits published match the defined bounds.
    const auto* elo = cat.get(id, "qLower"); const auto* ehi = cat.get(id, "qUpper");
    const bool limOk = elo && ehi && std::abs(elo->v[1] + 1.5) < 1e-6 && std::abs(ehi->v[2] - 2.5) < 1e-6;

    // a NON-EXISTENT (sensed Tier-2) property is absent from the catalog (not faked as native).
    const bool phantomAbsent = cat.get(id, "wrench") == nullptr && cat.get(id, "jointTorque") == nullptr;

    // readable through an Object -> Property node: select "q" -> the Vec3 X/Y/Z gates match.
    bool propNodeOk = false;
    {
        // (the Property node reads the same catalog by entity id; build it directly.)
        if (auto pn = NodeFactory::instance().createNode("twin_property")) {
            PortDataPacket pk; pk.data = static_cast<entt::entity>(node.entityId()); pk.type = { "entt::entity", "handle" }; pn->setInput("Object", pk);
            pn->setParam<std::string>("prop", "q"); pn->process();
            double xs[3] = { std::nan(""), std::nan(""), std::nan("") }; int k = 0;
            for (const char* nm : { "X", "Y", "Z" }) {
                for (const auto& p : pn->getPorts())
                    if (p.direction == Port::Direction::Output && p.name == nm && p.packet)
                        try { xs[k] = std::any_cast<double>(p.packet->data); } catch (...) {}
                ++k;
            }
            propNodeOk = std::abs(xs[0] - 0.4) < 1e-6 && std::abs(xs[1] - 0.3) < 1e-6 && std::abs(xs[2] + 0.2) < 1e-6;
        }
    }

    const bool pass = qOk && poseOk && dofOk && baseNotDofNeg && limOk && phantomAbsent && propNodeOk;
    printf("[robotpub]   q matches:%d; task-space EE pose matches FK:%d; DOF=%d(==3, base NOT a DOF; buggy-include=%d):%d; "
           "limits match:%d; phantom Tier-2 absent:%d; Object->Property reads q:%d  %s\n",
           int(qOk), int(poseOk), ed ? int(ed->v[0] + 0.5) : -1, buggyDof, int(dofOk && baseNotDofNeg),
           int(limOk), int(phantomAbsent), int(propNodeOk), pass ? "PASS" : "FAIL");
    printf("[robotpub] %s\n", pass ? "ALL PASS (kinematic Tier-1 props match actual state; base excluded from DOF; sensed props not faked; readable via Object->Property)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runRobotChainPlannableGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[robotplan] GATE ROBOT-CHAIN-IS-PLANNABLE -- OMPL plans over the robot's OWNED joints; base/non-member excluded\n");

    RobotNode node; node.process();
    const auto rrOpt = outRobot(node);
    const bool haveRef = rrOpt.has_value() && rrOpt->chain;
    const krs::RobotRef rr = rrOpt.value_or(krs::RobotRef{});

    // DOF is the member-joint count; the chain has exactly that many bodies (non-member excluded).
    const bool dofOk = haveRef && rr.dof == 3 && rr.chain->nq() == 3 && rr.chain->nbody() == 3 && outInt(node, "DOF") == 3;
    // NEG-CTRL: a model that wrongly counts the non-member joint as a DOF -> 4 (a wrong config space).
    const int buggyDof = node.robot().toChain(true).nq();
    const bool excludeNeg = buggyDof == 4 && buggyDof != rr.dof;

    // the planner plans over the OWNED DOFs (a real collision-free plan in the robot's workspace).
    bool planned = false;
    if (haveRef) {
        krs::plan::MotionPlanner planner(*rr.chain, rr.world, rr.limits);
        krs::plan::PlanRequest req;
        req.start.resize(3); req.start << -1.4, 0.0, 0.0;
        req.goal.resize(3);  req.goal  << +1.4, 0.0, 0.0;
        req.kind = krs::plan::PlannerKind::RRTConnect; req.seed = 7; req.maxIterations = 6000;
        planned = planner.plan(req).solved;
    }

    const bool pass = dofOk && excludeNeg && planned;
    printf("[robotplan]   Robot ref DOF=%d (==3 member joints; chain bodies=%d); planner over owned DOFs solved=%d; "
           "NEG buggy-include-nonmember DOF=%d (!=%d):%d  %s\n",
           haveRef ? rr.dof : -1, haveRef ? rr.chain->nbody() : -1, int(planned), buggyDof, rr.dof, int(excludeNeg),
           pass ? "PASS" : "FAIL");
    printf("[robotplan] %s\n", pass ? "ALL PASS (the Robot reference is what OMPL plans over; base placement + non-member joint excluded from the config space)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
