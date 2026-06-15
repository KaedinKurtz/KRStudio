#pragma once
// ComputedTorque.hpp -- SSOT trajectory-tracking controller. The soft PD (tau = Kp*e - Kd*qd) trails a
// MOVING setpoint because it has no model + no feedforward: it only reacts to the instantaneous error,
// so a fast command outruns it (the deferred ~0.23 rad GATE-D dynamic lag). Computed torque
// (feedback-linearizing inverse dynamics) cancels the arm's own dynamics and feeds the desired
// acceleration + velocity forward, so the closed-loop error obeys e'' + Kd e' + Kp e = 0 and tracks the
// trajectory tightly. Shared engine code (krs::ctrl) used by the controller gate AND any live tracker.

#include "RobotDynamics.hpp"
#include <Eigen/Dense>

namespace krs::ctrl {

// tau = M(q)[ qdd_des + Kd(qd_des - qd) + Kp(q_des - q) ] + biasForces(q,qd,g)
//      = inertia-shaped (PD on the error + acceleration feedforward) + gravity/Coriolis feedforward.
inline Eigen::VectorXd computedTorque(const krs::dyn::SerialChain& chain,
                                      const Eigen::VectorXd& q,  const Eigen::VectorXd& qd,
                                      const Eigen::VectorXd& q_des, const Eigen::VectorXd& qd_des,
                                      const Eigen::VectorXd& qdd_des,
                                      double kp, double kd, const Eigen::Vector3d& gravity)
{
    const Eigen::MatrixXd M = chain.massMatrix(q);
    const Eigen::VectorXd b = chain.biasForces(q, qd, gravity);
    const Eigen::VectorXd a = qdd_des + kd * (qd_des - qd) + kp * (q_des - q);
    return M * a + b;
}

} // namespace krs::ctrl
