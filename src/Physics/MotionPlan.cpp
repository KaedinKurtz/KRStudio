// MotionPlan.cpp -- see MotionPlan.hpp. Compose path -> follower -> timing into an executable motion.
#include "MotionPlan.hpp"
#include <cstdio>
#include <cmath>

namespace krs::motion {

using krs::dyn::SerialChain;
using krs::dyn::Pose;

MotionPlan planCartesianMotion(const SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                               int eeBody, const krs::path::CartesianPath& path,
                               const Eigen::VectorXd& q0, const krs::traj::TimingLimits& limits,
                               int samples, double dt, const krs::redun::Secondary* secondary) {
    MotionPlan out;
    out.follow = krs::follow::followPath(chain, basePlacement, eeBody, path, q0, samples, secondary);
    if (!out.follow.ok) return out;
    out.timed = krs::traj::timeParameterize(out.follow.jointPath, limits, dt);
    out.ok = out.timed.ok;
    return out;
}

// ================================================================================================
// GATE MOTION-PLAN
// ================================================================================================
bool runMotionPlanGate() {
    using std::printf;
    using namespace krs::dyn;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[motion] GATE MOTION-PLAN -- path -> follower -> timing = executable, actuator-limited motion\n");

    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = Eigen::Matrix3d::Identity();
    SerialChain chain; int parent = -1;
    for (int i = 0; i < 7; ++i) {
        DynJoint j; j.type = JType::Revolute; j.parent = parent;
        j.ptree = Eigen::Vector3d((i == 0) ? 0.0 : 0.3, 0, 0);
        j.axis = (i % 2 == 0) ? Eigen::Vector3d(0, 0, 1) : Eigen::Vector3d(0, 1, 0);
        j.qLower = -3.0; j.qUpper = 3.0;
        parent = chain.addBody(j, body);
    }
    const int ee = chain.nbody() - 1;
    const Eigen::Matrix4d base = Eigen::Matrix4d::Identity();
    Eigen::VectorXd q0(7); q0 << 0.1, 0.4, -0.2, 0.5, 0.1, -0.3, 0.2;

    const Pose ee0 = chain.bodyPose(q0, ee);
    krs::path::CartesianPath arc = krs::path::CartesianPath::arc(
        ee0.p - Eigen::Vector3d(0.2, 0, 0), 0.2, Eigen::Vector3d(0, 1, 0), 0.0, 1.2, 48);

    krs::traj::TimingLimits lim;
    lim.vMax = Eigen::VectorXd::Constant(7, 1.5);
    lim.aMax = Eigen::VectorXd::Constant(7, 3.0);

    MotionPlan plan = planCartesianMotion(chain, base, ee, arc, q0, lim, 48, 0.01, nullptr);
    const bool planned = plan.ok && plan.follow.ok && !plan.follow.anyUnreachable && plan.timed.ok;
    const bool tracks = plan.follow.maxEeErr < 5e-3;

    // Timing respects the limits (finite-difference the sampled trajectory).
    bool vOk = true, aOk = true;
    for (size_t k = 1; k < plan.timed.q.size(); ++k) {
        const double h = plan.timed.t[k] - plan.timed.t[k - 1];
        if (h <= 1e-9) continue;
        const Eigen::VectorXd v = (plan.timed.q[k] - plan.timed.q[k - 1]) / h;
        for (int d = 0; d < v.size(); ++d) if (std::abs(v[d]) > lim.vMax[d] * 1.05 + 1e-6) vOk = false;
    }
    for (int d = 0; d < 7; ++d)
        for (const auto& a : plan.timed.qdd)
            if (d < a.size() && std::abs(a[d]) > lim.aMax[d] * 1.10 + 1e-6) aOk = false;

    // The timed trajectory ends at the follower's final config, and its EE lands on the path end.
    const bool endsAtFollow = !plan.timed.q.empty() && !plan.follow.jointPath.empty()
        && (plan.timed.q.back() - plan.follow.jointPath.back()).cwiseAbs().maxCoeff() < 1e-9;
    const Pose pathEndWorld = arc.poseAt(1.0);
    const Eigen::Vector3d eeEndWorld = base.block<3,3>(0,0) * chain.bodyPose(plan.timed.q.back(), ee).p + base.block<3,1>(0,3);
    const bool eeAtEnd = (eeEndWorld - pathEndWorld.p).norm() < 5e-3;

    printf("[motion]   plan: follower tracks(maxEE=%.2e)=%s ; timing feasible v/a=%s/%s ; ends-at-follow=%s ; EE at path end(err=%.2e)=%s  %s\n",
           plan.follow.maxEeErr, tracks?"yes":"no", vOk?"yes":"no", aOk?"yes":"no",
           endsAtFollow?"yes":"no", (eeEndWorld - pathEndWorld.p).norm(), eeAtEnd?"yes":"no",
           (planned && tracks && vOk && aOk && endsAtFollow && eeAtEnd) ? "PASS" : "FAIL");
    printf("[motion]   trajectory: %zu samples over %.3f s\n", plan.timed.q.size(), plan.timed.duration);

    // NEG-CTRL: out-of-reach path -> follower flags unreachable (plan still returns, honestly).
    krs::path::CartesianPath farArc = krs::path::CartesianPath::arc(
        Eigen::Vector3d(50, 0, 0), 0.2, Eigen::Vector3d(0, 1, 0), 0.0, 1.0, 16);
    MotionPlan planFar = planCartesianMotion(chain, base, ee, farArc, q0, lim, 16, 0.01, nullptr);
    const bool negOk = planFar.follow.anyUnreachable;
    printf("[motion]   NEG-CTRL out-of-reach path flags unreachable=%s  %s\n",
           negOk?"yes":"NO", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = planned && tracks && vOk && aOk && endsAtFollow && eeAtEnd && negOk;
    printf("[motion] %s\n", pass ? "ALL PASS (path->follower->timing pipeline yields an executable, limit-respecting trajectory that tracks the Cartesian path end to end)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::motion
