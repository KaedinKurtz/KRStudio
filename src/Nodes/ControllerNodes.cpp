// ControllerNodes.cpp -- Phase 4.1: controller "knob" nodes. A goal-joint-angle digital potentiometer
// (dial) sets a commanded joint angle; a velocity setpoint knob sets a commanded joint velocity. The
// knob's dial value is a PARAM read by compute() and emitted on the output -- which the canvas wires to
// a joint command (the live articulation, FK-verified in GATE C-knob). Built on the NodeWidgets dial
// framework, so the dial provably drives the output.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeWidgets.hpp"

#include <QWidget>
#include <memory>
#include <string>

namespace NodeLibrary {

// goal joint-angle knob: emits the commanded angle (rad) its dial is set to.
class GoalJointKnobNode : public Node {
public:
    GoalJointKnobNode() {
        m_id = "ctrl_goal_knob";
        m_ports.push_back({ "Angle", {"double","rad"}, Port::Direction::Output, this });
        setParam<double>("angle", 0.0);
    }
    QWidget* createCustomWidget() override {
        using krs::nodeui::ControlSpec;
        return krs::nodeui::buildControlWidget(this, {
            { ControlSpec::Dial,   "Angle", "angle", -3.14159265, 3.14159265, 0.001, 0.0 } });
    }
    void compute() override { setOutput<double>("Angle", getParam<double>("angle", 0.0)); }
};

// velocity setpoint knob: emits the commanded joint velocity (rad/s).
class VelocitySetpointKnobNode : public Node {
public:
    VelocitySetpointKnobNode() {
        m_id = "ctrl_vel_knob";
        m_ports.push_back({ "Velocity", {"double","rad/s"}, Port::Direction::Output, this });
        setParam<double>("velocity", 0.0);
    }
    QWidget* createCustomWidget() override {
        using krs::nodeui::ControlSpec;
        return krs::nodeui::buildControlWidget(this, {
            { ControlSpec::Slider, "Vel", "velocity", -5.0, 5.0, 0.01, 0.0 } });
    }
    void compute() override { setOutput<double>("Velocity", getParam<double>("velocity", 0.0)); }
};

namespace {
struct ControllerNodeRegistrar {
    ControllerNodeRegistrar() {
        NodeFactory::instance().registerNodeType("ctrl_goal_knob",
            NodeDescriptor{ "Goal Angle Knob", "Control/Setpoint", "digital potentiometer -> commanded joint angle" },
            []() { return std::make_unique<GoalJointKnobNode>(); });
        NodeFactory::instance().registerNodeType("ctrl_vel_knob",
            NodeDescriptor{ "Velocity Knob", "Control/Setpoint", "commanded joint velocity setpoint" },
            []() { return std::make_unique<VelocitySetpointKnobNode>(); });
    }
};
static ControllerNodeRegistrar g_controllerNodeRegistrar;
} // namespace

} // namespace NodeLibrary
