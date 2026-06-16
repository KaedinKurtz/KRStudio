#pragma once
// ControlNodes.hpp -- the (A) control toolkit (node-ecosystem sprint). Closed-form, gateable controllers
// and estimators: a PID controller, a scalar (1D) Kalman filter, and a moving-average filter. Each is
// verified against an INDEPENDENT reference in ControlLibGate (GATE PID / GATE FILTER), not just "runs".
#include <QWidget>
#include "Node.hpp"
#include "NodeFactory.hpp"
#include <deque>

namespace NodeLibrary {

// PID controller: u = Kp*e + Ki*integral(e) + Kd*de/dt, e = Setpoint - Measurement. Integral + previous
// error are internal state advanced once per compute(). dt<=0 falls back to proportional-only.
class PidControllerNode : public Node {
public:
    PidControllerNode();
    void compute() override;
private:
    double m_integral = 0.0, m_prevErr = 0.0;
};

// Scalar Kalman filter for a (near-)constant value under measurement noise. Predict (constant model):
// P += Q. Update: K = P/(P+R); x += K(z-x); P *= (1-K). Inputs Measurement/Q/R; output Estimate.
class Kalman1DNode : public Node {
public:
    Kalman1DNode();
    void compute() override;
private:
    bool m_init = false; double m_x = 0.0, m_P = 1.0;
};

// Moving-average (boxcar) filter over the last Window samples. Inputs Input/Window; output Output.
class MovingAverageNode : public Node {
public:
    MovingAverageNode();
    void compute() override;
private:
    std::deque<double> m_buf;
};

} // namespace NodeLibrary
