// Redundancy.cpp -- see Redundancy.hpp. Task-priority null-space redundancy resolution.
#include "Redundancy.hpp"
#include <cstdio>
#include <cmath>

namespace krs::redun {

using krs::dyn::SerialChain;
using krs::dyn::Pose;

Eigen::MatrixXd nullspaceProjector(const SerialChain& chain, const Eigen::VectorXd& q,
                                   int eeBody, double lambda) {
    const int n = chain.nq();
    const Eigen::MatrixXd J = chain.jacobian(q, eeBody);      // 6 x n (rows [linear; angular])
    // Damped right pseudo-inverse J+ = J^T (J J^T + lambda^2 I)^-1.
    const Eigen::MatrixXd JT = J.transpose();
    const Eigen::MatrixXd JJt = J * JT + lambda * lambda * Eigen::MatrixXd::Identity(J.rows(), J.rows());
    const Eigen::MatrixXd Jpinv = JT * JJt.inverse();        // n x 6
    return Eigen::MatrixXd::Identity(n, n) - Jpinv * J;      // N = I - J+ J
}

namespace {
// Small-angle pose error [pos(3); rot(3)] of `body` at q vs a target pose (rot = axis*angle of
// R_err). Used to measure + correct EE drift.
Eigen::Matrix<double, 6, 1> poseError(const SerialChain& chain, const Eigen::VectorXd& q,
                                      int body, const Pose& target) {
    const Pose cur = chain.bodyPose(q, body);
    Eigen::Matrix<double, 6, 1> e;
    e.head<3>() = target.p - cur.p;
    const Eigen::Matrix3d Re = target.R * cur.R.transpose();
    const Eigen::AngleAxisd aa(Re);
    e.tail<3>() = aa.angle() * aa.axis();
    return e;
}
double eeErrNorm(const Eigen::Matrix<double, 6, 1>& e) {
    return e.head<3>().norm() + e.tail<3>().norm();
}
void clampToLimits(const SerialChain& chain, Eigen::VectorXd& q) {
    for (int b = 0; b < chain.nbody(); ++b) {
        const int d = chain.dofOf(b);
        if (d < 0 || d >= int(q.size())) continue;
        q[d] = std::min(chain.joint(b).qUpper, std::max(chain.joint(b).qLower, q[d]));
    }
}
} // namespace

RedundancyResult resolveInNullspace(const SerialChain& chain, const Eigen::VectorXd& q0,
                                    int eeBody, const Secondary& secondary,
                                    int steps, double stepScale, double eeLambda, double eeTol) {
    RedundancyResult out;
    const int n = chain.nq();
    if (n <= 0 || q0.size() != n || !secondary) return out;
    Eigen::VectorXd q = q0;
    const Pose eeTarget = chain.bodyPose(q0, eeBody);   // HOLD the initial EE pose

    for (int s = 0; s < steps; ++s) {
        const Eigen::MatrixXd N = nullspaceProjector(chain, q, eeBody, 1e-3);
        Eigen::VectorXd g = secondary(q);
        if (g.size() != n) break;
        Eigen::VectorXd dq = stepScale * (N * g);
        // step-size clamp so a big gradient can't fling the arm past a null-space arc.
        const double m = dq.cwiseAbs().maxCoeff();
        if (m > 0.15) dq *= 0.15 / m;
        q += dq;
        clampToLimits(chain, q);
        // DLS correction: a few small primary steps snap the EE back onto the held pose.
        for (int c = 0; c < 6; ++c) {
            const Eigen::Matrix<double, 6, 1> e = poseError(chain, q, eeBody, eeTarget);
            if (eeErrNorm(e) < eeTol) break;
            const Eigen::MatrixXd J = chain.jacobian(q, eeBody);
            const Eigen::MatrixXd JT = J.transpose();
            const Eigen::MatrixXd JJt = J * JT + eeLambda * eeLambda * Eigen::MatrixXd::Identity(6, 6);
            q += JT * JJt.inverse() * e;
            clampToLimits(chain, q);
        }
    }
    out.q = q;
    out.eeDrift = eeErrNorm(poseError(chain, q, eeBody, eeTarget));
    out.ok = true;
    return out;
}

// ---- secondary builders ----------------------------------------------------------------------
Secondary jointBias(int j, double target, double weight) {
    return [j, target, weight](const Eigen::VectorXd& q) {
        Eigen::VectorXd g = Eigen::VectorXd::Zero(q.size());
        if (j >= 0 && j < q.size()) g[j] = weight * (target - q[j]);
        return g;
    };
}
Secondary posture(const Eigen::VectorXd& qref, double weight) {
    return [qref, weight](const Eigen::VectorXd& q) {
        return (qref.size() == q.size()) ? Eigen::VectorXd(weight * (qref - q))
                                         : Eigen::VectorXd(Eigen::VectorXd::Zero(q.size()));
    };
}
Secondary limitAvoidance(const SerialChain& chain, double weight) {
    return [&chain, weight](const Eigen::VectorXd& q) {
        Eigen::VectorXd g = Eigen::VectorXd::Zero(q.size());
        for (int b = 0; b < chain.nbody(); ++b) {
            const int d = chain.dofOf(b);
            if (d < 0 || d >= int(q.size())) continue;
            const double lo = chain.joint(b).qLower, hi = chain.joint(b).qUpper;
            if (lo <= -1e29 || hi >= 1e29) continue;      // unbounded -> no preference
            g[d] = weight * (0.5 * (lo + hi) - q[d]);      // toward mid-range
        }
        return g;
    };
}
Secondary linkToPoint(const SerialChain& chain, int linkBody, const Eigen::Vector3d& worldTarget, double weight) {
    return [&chain, linkBody, worldTarget, weight](const Eigen::VectorXd& q) {
        // gradient of 0.5*||p(link) - target||^2 in joint space = Jp^T (target - p) (position rows).
        const Pose P = chain.bodyPose(q, linkBody);
        const Eigen::MatrixXd J = chain.jacobian(q, linkBody);   // 6 x n, rows [linear; angular]
        const Eigen::Matrix<double, 3, Eigen::Dynamic> Jp = J.topRows<3>();
        return Eigen::VectorXd(weight * (Jp.transpose() * (worldTarget - P.p)));
    };
}

// ================================================================================================
// GATE REDUNDANCY
// ================================================================================================
bool runRedundancyGate() {
    using std::printf;
    using namespace krs::dyn;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[redun] GATE REDUNDANCY -- null-space secondary motion holds the EE pose; redundant arm has a null space\n");

    auto eye3 = [] { return Eigen::Matrix3d::Identity(); };
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye3();

    // A 7-DoF arm (alternating yaw/pitch), links along +x. 7 DoF > 6 task -> a 1-D null space.
    auto build = [&](int ndof) {
        SerialChain c;
        int parent = -1;
        for (int i = 0; i < ndof; ++i) {
            DynJoint j; j.type = JType::Revolute; j.parent = parent;
            j.ptree = Eigen::Vector3d((i == 0) ? 0.0 : 0.3, 0, 0);
            j.axis = (i % 2 == 0) ? Eigen::Vector3d(0, 0, 1) : Eigen::Vector3d(0, 1, 0);
            j.qLower = -3.0; j.qUpper = 3.0;
            parent = c.addBody(j, body);
        }
        return c;
    };
    SerialChain c7 = build(7);
    const int ee = c7.nbody() - 1;

    // Null-space dimension: rank(N) should be nq - 6 = 1 for a non-singular config.
    Eigen::VectorXd q(7); q << 0.2, 0.5, -0.3, 0.7, 0.1, -0.4, 0.2;
    const Eigen::MatrixXd N7 = nullspaceProjector(c7, q, ee);
    Eigen::JacobiSVD<Eigen::MatrixXd> svd7(N7);
    int nsdim = 0; for (int i = 0; i < svd7.singularValues().size(); ++i) if (svd7.singularValues()[i] > 0.5) ++nsdim;
    const bool nsDim = (nsdim == 1);

    // TWIST a mid-joint (secondary) while holding the EE pose. The EE must stay put; the joint must
    // move appreciably along the 1-D null-space arc.
    const int twistJoint = 2;
    const double jBefore = q[twistJoint];
    RedundancyResult r = resolveInNullspace(c7, q, ee, jointBias(twistJoint, q[twistJoint] + 2.5, 1.0), 150, 0.06);
    const double twist = std::abs(r.q[twistJoint] - jBefore);
    const bool eeHeld = r.ok && r.eeDrift < 5e-3;      // EE held (pos+rot error, metres+rad)
    const bool twisted = twist > 0.1;                   // the redundant DoF genuinely moved
    printf("[redun]   7-DoF: null-space dim=%d(want 1) ; joint%d twisted %.3f rad while EE held (drift=%.2e m+rad)  %s\n",
           nsdim, twistJoint, twist, r.eeDrift, (nsDim && eeHeld && twisted) ? "PASS" : "FAIL");

    // NEG-CTRL: a 6-DoF arm has ~no null space for a 6-D EE task -> the same twist request barely
    // moves joint 0 (any motion would drag the EE, which the correction undoes).
    SerialChain c6 = build(6);
    const int ee6 = c6.nbody() - 1;
    Eigen::VectorXd q6(6); q6 << 0.2, 0.5, -0.3, 0.7, 0.1, -0.4;
    const Eigen::MatrixXd N6 = nullspaceProjector(c6, q6, ee6);
    Eigen::JacobiSVD<Eigen::MatrixXd> svd6(N6);
    int nsdim6 = 0; for (int i = 0; i < svd6.singularValues().size(); ++i) if (svd6.singularValues()[i] > 0.5) ++nsdim6;
    RedundancyResult r6 = resolveInNullspace(c6, q6, ee6, jointBias(2, q6[2] + 2.5, 1.0), 150, 0.06);
    const double twist6 = std::abs(r6.q[2] - q6[2]);
    const bool negOk = nsdim6 == 0 && twist6 < twist * 0.3;   // no redundancy -> joint locked by the EE task
    printf("[redun]   NEG-CTRL 6-DoF: null-space dim=%d(want 0) ; twist request moved joint2 only %.3f rad (vs %.3f redundant)  %s\n",
           nsdim6, twist6, twist, negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    // ELBOW placement: move an intermediate link toward a point while holding EE -> the mid link
    // origin gets closer to the target, EE still held.
    const int midLink = 3;
    const Eigen::Vector3d mid0 = c7.bodyPose(q, midLink).p;
    const Eigen::Vector3d tgt = mid0 + Eigen::Vector3d(0, 0.3, 0.2);
    RedundancyResult re = resolveInNullspace(c7, q, ee, linkToPoint(c7, midLink, tgt, 1.0), 80, 0.05);
    const double dBefore = (mid0 - tgt).norm();
    const double dAfter = (c7.bodyPose(re.q, midLink).p - tgt).norm();
    const bool elbowOk = re.ok && re.eeDrift < 5e-3 && dAfter < dBefore - 1e-3;
    printf("[redun]   elbow-to-point: mid-link dist %.3f -> %.3f while EE held (drift=%.2e)  %s\n",
           dBefore, dAfter, re.eeDrift, elbowOk ? "PASS" : "FAIL");

    const bool pass = nsDim && eeHeld && twisted && negOk && elbowOk;
    printf("[redun] %s\n", pass ? "ALL PASS (redundant arm twists a joint / places a link in the null space while the EE pose stays fixed; 6-DoF neg-ctrl has no redundancy)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::redun
