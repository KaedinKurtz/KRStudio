// ControlNodes.cpp -- see ControlNodes.hpp. PID + scalar Kalman + moving-average, all closed-form and
// gated against independent references in ControlLibGate.

#include "ControlNodes.hpp"
#include <algorithm>
#include <memory>

namespace NodeLibrary {

// ---- PID ----
PidControllerNode::PidControllerNode() {
    m_id = "control_pid";
    m_ports.push_back({ "Setpoint",    {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Measurement", {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Kp",          {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Ki",          {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Kd",          {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "dt",          {"double", "s"},        Port::Direction::Input,  this });
    m_ports.push_back({ "Control",     {"double", "unitless"}, Port::Direction::Output, this });
}
void PidControllerNode::compute() {
    const double sp = getInputD("Setpoint", 0.0);
    const double meas = getInputD("Measurement", 0.0);
    const double kp = getInputD("Kp", 0.0), ki = getInputD("Ki", 0.0), kd = getInputD("Kd", 0.0);
    const double dt = getInputD("dt", 0.0);
    const double err = sp - meas;
    double u;
    if (dt > 0.0) {
        m_integral += err * dt;
        const double deriv = (err - m_prevErr) / dt;
        m_prevErr = err;
        u = kp * err + ki * m_integral + kd * deriv;
    } else {
        u = kp * err;                          // no timestep -> proportional only
    }
    setOutput<double>("Control", u);
}

// ---- scalar Kalman ----
Kalman1DNode::Kalman1DNode() {
    m_id = "filter_kalman_1d";
    m_ports.push_back({ "Measurement", {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Q",           {"double", "variance"}, Port::Direction::Input,  this });
    m_ports.push_back({ "R",           {"double", "variance"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Estimate",    {"double", "unitless"}, Port::Direction::Output, this });
}
void Kalman1DNode::compute() {
    auto zOpt = getInput<double>("Measurement");
    if (!zOpt) { auto zf = getInput<float>("Measurement"); if (zf) zOpt = double(*zf); }
    if (!zOpt) return;                          // disconnected -> no update
    const double z = *zOpt;
    const double q = getInputD("Q", 1e-4), r = getInputD("R", 1.0);
    if (!m_init) { m_x = z; m_P = r; m_init = true; }
    else {
        m_P += q;                              // predict (constant model)
        const double K = m_P / (m_P + r);      // update
        m_x += K * (z - m_x);
        m_P *= (1.0 - K);
    }
    setOutput<double>("Estimate", m_x);
}

// ---- moving average ----
MovingAverageNode::MovingAverageNode() {
    m_id = "filter_moving_average";
    m_ports.push_back({ "Input",  {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Window", {"int", "count"},       Port::Direction::Input,  this });
    m_ports.push_back({ "Output", {"double", "unitless"}, Port::Direction::Output, this });
}
void MovingAverageNode::compute() {
    auto inOpt = getInput<double>("Input");
    if (!inOpt) { auto f = getInput<float>("Input"); if (f) inOpt = double(*f); }
    if (!inOpt) return;
    const int window = std::max(1, getInput<int>("Window").value_or(1));
    m_buf.push_back(*inOpt);
    while (int(m_buf.size()) > window) m_buf.pop_front();
    double sum = 0.0; for (double v : m_buf) sum += v;
    setOutput<double>("Output", sum / double(m_buf.size()));
}

namespace {
struct ControlNodesRegistrar {
    ControlNodesRegistrar() {
        NodeFactory::instance().registerNodeType("control_pid",
            NodeDescriptor{ "PID Controller", "Control/Controllers",
                "u = Kp*e + Ki*integral(e) + Kd*de/dt, e = Setpoint - Measurement." },
            []() { return std::make_unique<PidControllerNode>(); });
        NodeFactory::instance().registerNodeType("filter_kalman_1d",
            NodeDescriptor{ "Kalman Filter (1D)", "Control/Estimation",
                "Scalar Kalman estimate of a noisy near-constant signal (Q process, R measurement noise)." },
            []() { return std::make_unique<Kalman1DNode>(); });
        NodeFactory::instance().registerNodeType("filter_moving_average",
            NodeDescriptor{ "Moving Average", "Control/Filters",
                "Boxcar average of the last Window samples." },
            []() { return std::make_unique<MovingAverageNode>(); });
    }
};
static ControlNodesRegistrar g_controlNodesRegistrar;
} // namespace

} // namespace NodeLibrary
