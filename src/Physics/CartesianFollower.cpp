// CartesianFollower.cpp -- see CartesianFollower.hpp. Sweep an EE along a CartesianPath, resolving a
// secondary objective in the null space; emit a joint-space path for krs::traj to time.
#include "CartesianFollower.hpp"
#include <cstdio>
#include <cmath>

namespace krs::follow {

using krs::dyn::SerialChain;
using krs::dyn::Pose;

namespace {
// World pose -> chain-base-frame pose (IK works in the chain frame; the path yields world poses).
Pose worldToChain(const Eigen::Matrix4d& basePlacement, const Pose& w) {
    const Eigen::Matrix3d Rb = basePlacement.block<3, 3>(0, 0);
    const Eigen::Vector3d pb = basePlacement.block<3, 1>(0, 3);
    Pose c;
    c.R = Rb.transpose() * w.R;
    c.p = Rb.transpose() * (w.p - pb);
    return c;
}
double poseErr(const SerialChain& chain, const Eigen::VectorXd& q, int body, const Pose& targetChain) {
    const Pose cur = chain.bodyPose(q, body);
    const double pe = (targetChain.p - cur.p).norm();
    const Eigen::AngleAxisd aa(targetChain.R * cur.R.transpose());
    return pe + std::abs(aa.angle());
}
} // namespace

FollowResult followPath(const SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                        int eeBody, const krs::path::CartesianPath& path, const Eigen::VectorXd& q0,
                        int samples, const krs::redun::Secondary* secondary) {
    FollowResult out;
    if (chain.nq() <= 0 || path.empty() || samples < 2 || q0.size() != chain.nq()) return out;

    // Sample by ARC LENGTH for ~constant Cartesian speed (build the table if the caller didn't).
    krs::path::CartesianPath p = path;
    p.arcLengthReparam(512);

    Eigen::VectorXd q = q0;
    out.jointPath.reserve(samples);
    out.eeErr.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double u = double(i) / double(samples - 1);
        const Pose chainTgt = worldToChain(basePlacement, p.poseAtArc(u));

        // Primary: robust built-in DLS IK reaches the EE pose (warm-started for continuity).
        SerialChain::IKResult r = chain.ik(chainTgt, eeBody, q, /*lambda*/ 0.05, /*maxIters*/ 120,
                                           /*tol*/ 1e-6, nullptr, 0.0, /*rotWeight*/ 1.0);
        q = r.bestQ;
        if (r.clampedToReach) out.anyUnreachable = true;

        // Secondary in the null space: resolveInNullspace holds the EXACT EE pose IK reached (per-step
        // DLS correction) while advancing the objective along the null-space arc. Warm-starting the
        // next sample from this q makes the redundancy choice persist along the sweep. The objective
        // may move some joints more than others (the null-space direction's per-joint components), so
        // this shapes the WHOLE-arm posture along the path, EE held.
        if (secondary) {
            krs::redun::RedundancyResult rr = krs::redun::resolveInNullspace(
                chain, q, eeBody, *secondary, /*steps*/ 40, /*stepScale*/ 0.07);
            if (rr.ok && rr.eeDrift < 5e-4) q = rr.q;
        }

        const double e = poseErr(chain, q, eeBody, chainTgt);
        out.eeErr.push_back(e);
        out.maxEeErr = std::max(out.maxEeErr, e);
        out.jointPath.push_back(q);
    }
    out.ok = true;
    return out;
}

// ================================================================================================
// GATE FOLLOWER
// ================================================================================================
bool runFollowerGate() {
    using std::printf;
    using namespace krs::dyn;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[follower] GATE FOLLOWER -- EE sweeps a Cartesian path; secondary held in the null space\n");

    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = Eigen::Matrix3d::Identity();
    // 7-DoF arm (alternating yaw/pitch), links along +x, ~2.1 m reach.
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

    // A reachable arc: centre in front of the base, small radius, in the x-z plane.
    const Pose ee0 = chain.bodyPose(q0, ee);
    const Eigen::Vector3d center = ee0.p - Eigen::Vector3d(0.2, 0, 0);
    krs::path::CartesianPath arc = krs::path::CartesianPath::arc(
        center, 0.2, Eigen::Vector3d(0, 1, 0), 0.0, 1.2, 48);

    // FOLLOW (no secondary): every sample's EE must track the path.
    FollowResult f = followPath(chain, base, ee, arc, q0, 48, nullptr);
    const bool tracks = f.ok && !f.anyUnreachable && f.maxEeErr < 5e-3;
    printf("[follower]   sweep arc: %zu samples, maxEEerr=%.2e (want <5e-3), reachable=%s  %s\n",
           f.jointPath.size(), f.maxEeErr, f.anyUnreachable ? "NO" : "yes", tracks ? "PASS" : "FAIL");

    // FOLLOW with a posture secondary: the WHOLE-arm configuration should differ measurably from the
    // no-secondary sweep (the null-space redundancy reshapes the posture), while BOTH track the SAME
    // EE path. Measured as the config-space deviation of the final sample (the null-space direction's
    // per-joint components vary, so the total deviation is the honest "redundancy was used" metric).
    Eigen::VectorXd elbowOut(7); elbowOut << 0, 1.2, 0, -1.2, 0, 1.2, 0;   // an "elbow-out" posture
    krs::redun::Secondary sec = krs::redun::posture(elbowOut, 1.0);
    FollowResult ft = followPath(chain, base, ee, arc, q0, 48, &sec);
    double configDev = 0.0;
    if (!f.jointPath.empty() && ft.jointPath.size() == f.jointPath.size())
        configDev = (ft.jointPath.back() - f.jointPath.back()).norm();
    const bool secondaryMoved = configDev > 0.1;          // posture genuinely reshaped
    const bool stillTracks = ft.ok && ft.maxEeErr < 5e-3; // EE path still tracked
    printf("[follower]   with posture secondary: final-config deviation=%.3f (want >0.1, redundancy reshaped) ; EE still tracks maxerr=%.2e  %s\n",
           configDev, ft.maxEeErr, (secondaryMoved && stillTracks) ? "PASS" : "FAIL");

    // NEG-CTRL: a path far outside reach -> anyUnreachable flagged.
    krs::path::CartesianPath farArc = krs::path::CartesianPath::arc(
        Eigen::Vector3d(50, 0, 0), 0.2, Eigen::Vector3d(0, 1, 0), 0.0, 1.0, 16);
    FollowResult fn = followPath(chain, base, ee, farArc, q0, 16, nullptr);
    const bool negOk = fn.ok && fn.anyUnreachable;
    printf("[follower]   NEG-CTRL far-out-of-reach path flags unreachable=%s  %s\n",
           fn.anyUnreachable ? "yes" : "NO", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = tracks && secondaryMoved && stillTracks && negOk;
    printf("[follower] %s\n", pass ? "ALL PASS (EE sweeps the Cartesian path; a null-space secondary moves while EE tracking holds; unreachable flagged)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::follow
