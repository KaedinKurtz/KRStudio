// Retarget.cpp -- see Retarget.hpp. Fit a robot onto a reference skeleton clip via per-frame IK.
#include "Retarget.hpp"
#include <cstdio>
#include <cmath>

namespace krs::retarget {

using krs::dyn::SerialChain;
using krs::dyn::Pose;

RetargetResult retargetPositions(const SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                                 const krs::anim::SkeletonClip& clip, int eeSkelJoint, int eeBody,
                                 const Eigen::Matrix4d& skelToBase, double scale,
                                 const Eigen::VectorXd& q0,
                                 const krs::redun::Secondary* secondary) {
    RetargetResult out;
    if (chain.nq() <= 0 || clip.frameCount <= 0 || q0.size() != chain.nq()) return out;
    if (eeSkelJoint < 0 || eeSkelJoint >= int(clip.joints.size())) return out;

    const Eigen::Matrix3d Rb = basePlacement.block<3, 3>(0, 0);
    const Eigen::Vector3d pb = basePlacement.block<3, 1>(0, 3);
    Eigen::VectorXd q = q0;
    out.jointFrames.reserve(clip.frameCount);
    out.posErr.reserve(clip.frameCount);

    for (int fr = 0; fr < clip.frameCount; ++fr) {
        // Skeleton world pos -> robot base-frame target.
        const Eigen::Vector3d sw = clip.jointWorldPos(fr, eeSkelJoint);
        const Eigen::Vector4d mapped = skelToBase * Eigen::Vector4d(scale * sw.x(), scale * sw.y(), scale * sw.z(), 1.0);
        const Eigen::Vector3d tgtWorld = mapped.head<3>();
        // world -> chain frame (IK is in the chain frame).
        const Eigen::Vector3d tgtChain = Rb.transpose() * (tgtWorld - pb);

        // Position-dominant IK: hold the current EE orientation as the target R (so orientation is
        // "already satisfied") and reach the target position.
        Pose target;
        target.R = chain.bodyPose(q, eeBody).R;
        target.p = tgtChain;
        SerialChain::IKResult r = chain.ik(target, eeBody, q, /*lambda*/ 0.05, /*maxIters*/ 150,
                                           /*tol*/ 1e-6, nullptr, 0.0, /*rotWeight*/ 0.02);
        q = r.bestQ;
        if (r.clampedToReach) out.anyUnreachable = true;

        if (secondary) {
            krs::redun::RedundancyResult rr = krs::redun::resolveInNullspace(
                chain, q, eeBody, *secondary, 6, 0.04);
            if (rr.ok && rr.eeDrift < 1e-3) q = rr.q;
        }

        const double e = (chain.bodyPose(q, eeBody).p - tgtChain).norm();
        out.posErr.push_back(e);
        out.maxPosErr = std::max(out.maxPosErr, e);
        out.jointFrames.push_back(q);
    }
    out.ok = true;
    return out;
}

// ================================================================================================
// GATE RETARGET
// ================================================================================================
bool runRetargetGate() {
    using std::printf;
    using namespace krs::dyn;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[retarget] GATE RETARGET -- robot EE fits a reference skeleton joint's motion per frame\n");

    // Robot: 6-DoF arm, ~1.8 m reach.
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = Eigen::Matrix3d::Identity();
    SerialChain chain; int parent = -1;
    for (int i = 0; i < 6; ++i) {
        DynJoint j; j.type = JType::Revolute; j.parent = parent;
        j.ptree = Eigen::Vector3d((i == 0) ? 0.0 : 0.3, 0, 0);
        j.axis = (i % 2 == 0) ? Eigen::Vector3d(0, 0, 1) : Eigen::Vector3d(0, 1, 0);
        j.qLower = -3.1; j.qUpper = 3.1;
        parent = chain.addBody(j, body);
    }
    const int ee = chain.nbody() - 1;
    const Eigen::Matrix4d base = Eigen::Matrix4d::Identity();
    Eigen::VectorXd q0(6); q0 << 0.1, 0.4, -0.2, 0.5, 0.1, -0.3;

    // Hand-build a 2-joint skeleton whose END joint traces a short line the robot can reach. Root at
    // origin; child offset animates via a translating root over frames (simplest deterministic path).
    krs::anim::SkeletonClip clip;
    krs::anim::SkeletonJoint root; root.name = "root"; root.parent = -1; root.hasPosition = true;
    krs::anim::SkeletonJoint hand; hand.name = "hand"; hand.parent = 0; hand.offset = Eigen::Vector3d(0.2, 0, 0);
    clip.joints = { root, hand };
    clip.frameCount = 10; clip.frameTime = 1.0 / 30.0;
    // The reachable target line in ROBOT base frame (front of the base, sweeping in z).
    const Eigen::Vector3d start(0.9, 0.0, 0.2), end(0.9, 0.0, 0.8);
    clip.localXforms.resize(clip.frameCount);
    for (int fr = 0; fr < clip.frameCount; ++fr) {
        const double a = double(fr) / double(clip.frameCount - 1);
        const Eigen::Vector3d handTarget = start + a * (end - start);
        // We want jointWorldPos(fr, hand) == handTarget. hand world = root world * hand local; root
        // has an identity R and animates its position; hand local = [I | offset]. So set root
        // position so root.p + offset == handTarget -> rootPos = handTarget - offset.
        Eigen::Matrix4d rootL = Eigen::Matrix4d::Identity();
        rootL.block<3, 1>(0, 3) = handTarget - hand.offset;
        Eigen::Matrix4d handL = Eigen::Matrix4d::Identity();
        handL.block<3, 1>(0, 3) = hand.offset;
        clip.localXforms[fr] = { rootL, handL };
    }
    // sanity: the clip's hand joint really traces the intended line.
    const bool clipOk = (clip.jointWorldPos(0, 1) - start).norm() < 1e-9
                     && (clip.jointWorldPos(clip.frameCount - 1, 1) - end).norm() < 1e-9;

    // RETARGET hand -> robot EE. Skeleton space == robot base space here (identity map, scale 1).
    RetargetResult r = retargetPositions(chain, base, clip, /*eeSkelJoint*/ 1, ee,
                                         Eigen::Matrix4d::Identity(), 1.0, q0, nullptr);
    const bool reaches = r.ok && !r.anyUnreachable && r.maxPosErr < 5e-3
                      && int(r.jointFrames.size()) == clip.frameCount;
    // continuity: no large frame-to-frame config jump.
    double maxJump = 0.0;
    for (size_t i = 1; i < r.jointFrames.size(); ++i)
        maxJump = std::max(maxJump, (r.jointFrames[i] - r.jointFrames[i - 1]).cwiseAbs().maxCoeff());
    const bool continuous = maxJump < 1.0;   // < ~57 deg/frame
    printf("[retarget]   clip traces line=%s ; EE fits all %d frames maxErr=%.2e (want <5e-3) ; continuous(jump=%.3f)=%s  %s\n",
           clipOk ? "yes" : "no", clip.frameCount, r.maxPosErr, maxJump, continuous ? "yes" : "no",
           (clipOk && reaches && continuous) ? "PASS" : "FAIL");

    // NEG-CTRL: scale the skeleton far out of reach -> unreachable flagged.
    RetargetResult rn = retargetPositions(chain, base, clip, 1, ee, Eigen::Matrix4d::Identity(), 40.0, q0, nullptr);
    const bool negOk = rn.ok && rn.anyUnreachable;
    printf("[retarget]   NEG-CTRL scaled x40 out of reach -> unreachable=%s  %s\n",
           rn.anyUnreachable ? "yes" : "NO", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = clipOk && reaches && continuous && negOk;
    printf("[retarget] %s\n", pass ? "ALL PASS (robot EE fits the reference skeleton joint per frame, continuous seed trajectory; out-of-reach flagged)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::retarget
