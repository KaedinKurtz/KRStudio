// ===========================================================================
// OMPL sprint, Phase 4 — robot entity + kinematic-chain gates (env
// KRS_ROBOTCHAIN_SELFTEST; in the bench). Pure CPU. Each sub-gate is a measured
// number with a non-vacuous negative control.
//   ROBOT-CHAIN          : the planner plans over the OWNED (member) joints;
//                          nq == member count; the rigid floor placement is NOT a
//                          DOF; a non-member joint is EXCLUDED (buggy include -> nq+1).
//   JOINT-FROM-FEATURE   : a revolute frame derived from two coaxial bores matches
//                          the oracle <1e-6; a non-coaxial pair is REJECTED.
//   MOUNT-PORT           : a typed tool lands at the port frame; swaps without
//                          redefining a joint; a mismatched-type tool does NOT attach.
//   CHAIN-EXPORT-ROUNDTRIP: serialize/deserialize is lossless; a corrupted export fails.
// ===========================================================================
#include "RobotModel.hpp"
#include "MotionPlanner.hpp"
#include "JointTooling.hpp"
#include "components.hpp"      // BRepFace

#include <cstdio>
#include <cmath>

namespace krs::robot {

namespace {
constexpr double kPi = 3.14159265358979323846;
Eigen::Vector3d v3(double a, double b, double c) { return Eigen::Vector3d(a, b, c); }
Eigen::VectorXd q3(double a, double b, double c) { Eigen::VectorXd q(3); q << a, b, c; return q; }

// A 3-member-joint robot (yaw Z, pitch Y, pitch Y) + ONE non-member joint + a
// non-identity rigid base placement + a typed mount port on the last link.
Robot makeRobot() {
    Robot r; r.name = "testbot"; r.nLinks = 4;
    Joint j1; j1.member = true; j1.axis = v3(0, 0, 1); j1.ptree = v3(0, 0, 0);   j1.qLower = -kPi; j1.qUpper = kPi; j1.nodeId = 11;
    Joint j2; j2.member = true; j2.axis = v3(0, 1, 0); j2.ptree = v3(0, 0, 0.3); j2.qLower = -1.5; j2.qUpper = 1.5; j2.nodeId = 12;
    Joint j3; j3.member = true; j3.axis = v3(0, 1, 0); j3.ptree = v3(0.5, 0, 0); j3.qLower = -2.5; j3.qUpper = 2.5; j3.nodeId = 13;
    Joint jX; jX.member = false; jX.axis = v3(1, 0, 0); jX.ptree = v3(0.5, 0, 0); jX.frameProv = Provenance::UserSupplied; // excluded
    r.joints = { j1, j2, j3, jX };
    Eigen::Matrix4d B = Eigen::Matrix4d::Identity();
    const double a = 0.5; Eigen::Matrix3d Rz; Rz << std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1;
    B.block<3, 3>(0, 0) = Rz; B.block<3, 1>(0, 3) = v3(1, 2, 3);
    r.basePlacement = B;
    r.mount.type = "flange-A"; r.mount.link = 2; r.mount.framePos = v3(0.5, 0, 0); r.mount.frameDir = v3(1, 0, 0);
    return r;
}

krs::plan::JointLimits chainLimits(const Robot& r) {
    krs::plan::JointLimits lim;
    int n = 0; for (const auto& j : r.joints) if (j.member) ++n;
    lim.qLower.resize(n); lim.qUpper.resize(n); lim.vMax.resize(n);
    int i = 0; for (const auto& j : r.joints) if (j.member) { lim.qLower[i] = j.qLower; lim.qUpper[i] = j.qUpper; lim.vMax[i] = j.vMax; ++i; }
    return lim;
}
} // namespace

bool runRobotChainGate() {
    std::fprintf(stderr, "TRACE robotchain: enter\n");
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[robotchain] GATE ROBOT-CHAIN -- entity owns links+joints+base+mount; plan over owned DOFs\n");
    const Robot r = makeRobot();
    bool allOk = true;

    // ---- ROBOT-CHAIN: nq == member count; non-member excluded; base not a DOF ----
    {
        const krs::dyn::SerialChain chain = r.toChain(false);
        const krs::dyn::SerialChain buggy = r.toChain(true);   // wrongly includes the non-member
        const int memberCount = 3;
        // plan over the OWNED joints (a sphere in the workspace to make it a real plan).
        std::vector<krs::plan::LinkCapsule> caps = {
            { 0, v3(0,0,0), v3(0,0,0.3), 0.06 }, { 1, v3(0,0,0), v3(0.5,0,0), 0.05 }, { 2, v3(0,0,0), v3(0.5,0,0), 0.05 } };
        krs::plan::CollisionWorld world; world.capsules = caps; world.tolerance = 1e-6;
        world.obstacles.push_back(krs::plan::Obstacle::sphere(v3(0.8, 0, 0.3), 0.2));
        const krs::plan::JointLimits lim = chainLimits(r);
        krs::plan::MotionPlanner planner(chain, world, lim);
        krs::plan::PlanRequest rq; rq.start = q3(-1.2, 0, 0); rq.goal = q3(1.2, 0, 0); rq.seed = 7;
        const krs::plan::PlanResult pr = planner.plan(rq);
        const bool ok = chain.nq() == memberCount && buggy.nq() == memberCount + 1 && pr.solved;
        printf("[robotchain]   ROBOT-CHAIN: nq=%d (== %d member joints; base placement NOT a DOF); "
               "buggy-include-nonmember nq=%d (== %d); planner over owned DOFs solved=%d  %s\n",
               chain.nq(), memberCount, buggy.nq(), memberCount + 1, int(pr.solved), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- JOINT-FROM-FEATURE: derive revolute from two coaxial bores ----
    {
        BRepFace a; a.type = 1; a.axisDir = glm::vec3(0, 0, 1); a.axisPos = glm::vec3(0.1f, 0.2f, 0.0f); a.radius = 0.01f;
        BRepFace b; b.type = 1; b.axisDir = glm::vec3(0, 0, 1); b.axisPos = glm::vec3(0.1f, 0.2f, 0.5f); b.radius = 0.01f;
        krs::joint::JointFrame jf;
        const bool got = krs::joint::deriveRevoluteFromBores(a, b, jf);
        const double dirErr = std::abs(1.0 - std::abs(double(glm::dot(jf.axisDir, glm::vec3(0, 0, 1)))));
        const double posErr = std::sqrt(double(jf.axisPos.x - 0.1f) * (jf.axisPos.x - 0.1f)
                                      + double(jf.axisPos.y - 0.2f) * (jf.axisPos.y - 0.2f));
        // NEG-CTRL: a non-coaxial pair (axis line offset by 0.1 m) -> REJECTED.
        BRepFace d; d.type = 1; d.axisDir = glm::vec3(0, 0, 1); d.axisPos = glm::vec3(0.2f, 0.2f, 0.5f); d.radius = 0.01f;
        krs::joint::JointFrame jf2;
        const bool rejected = !krs::joint::deriveRevoluteFromBores(a, d, jf2);
        const bool ok = got && dirErr < 1e-6 && posErr < 1e-6 && rejected;
        printf("[robotchain]   JOINT-FROM-FEATURE: derived axis-dir err=%.3e axis-pos err=%.3e (<1e-6); "
               "non-coaxial pair rejected=%d  %s\n", dirErr, posErr, int(rejected), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- MOUNT-PORT: typed tool lands at the frame; swaps; mismatch doesn't attach ----
    {
        const krs::dyn::SerialChain chain = r.toChain(false);
        const Eigen::VectorXd q = q3(0.2, 0.3, -0.4);
        Tool toolA; toolA.type = "flange-A"; toolA.localTip = v3(0, 0, 0.1);
        Eigen::Vector3d tip; const bool attached = attachTool(r, chain, q, toolA, tip);

        // independent expected tip (carrying-link FK -> base -> port+tip): "lands at frame".
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
        const krs::dyn::Pose& P = poses[std::min(int(poses.size()) - 1, r.mount.link)];
        const Eigen::Matrix3d Rb = r.basePlacement.block<3, 3>(0, 0);
        const Eigen::Vector3d pb = r.basePlacement.block<3, 1>(0, 3);
        const Eigen::Vector3d expected = (Rb * P.R) * (r.mount.framePos + toolA.localTip) + (Rb * P.p + pb);
        const double landErr = attached ? (tip - expected).norm() : 1e30;

        Tool toolA2; toolA2.type = "flange-A"; toolA2.localTip = v3(0, 0, 0.2);   // same type, different tip
        Eigen::Vector3d tip2; const bool attached2 = attachTool(r, chain, q, toolA2, tip2);
        const double swapMove = (tip2 - tip).norm();
        Tool toolB; toolB.type = "flange-B"; Eigen::Vector3d tipB;                // mismatched type
        const bool attachedB = attachTool(r, chain, q, toolB, tipB);

        const bool ok = attached && landErr < 1e-9 && attached2 && swapMove > 1e-6
                     && !attachedB && chain.nq() == 3;
        printf("[robotchain]   MOUNT-PORT: tool lands at frame err=%.3e (<1e-9); swap moves tip %.4f m "
               "(joint NOT redefined, nq=%d); NEG mismatched-type attaches=%d (must be 0)  %s\n",
               landErr, swapMove, chain.nq(), int(attachedB), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- CHAIN-EXPORT-ROUNDTRIP: lossless serialize/deserialize; corruption fails ----
    {
        const std::string s = serialize(r);
        bool ok1 = false; const Robot r2 = deserialize(s, ok1);
        const bool roundtrip = ok1 && nearlyEqual(r, r2);
        // NEG-CTRL: a truncated/corrupted export -> deserialize FAILS (no fabricated robot).
        bool ok2 = true; const Robot rbad = deserialize(s.substr(0, s.size() / 2), ok2); (void)rbad;
        // 2nd corruption: a flipped engineering field must be DETECTED by the round-trip compare.
        Robot rmut = r2; if (!rmut.joints.empty()) rmut.joints[0].effortMax += 1.0;
        const bool detectsFieldFlip = !nearlyEqual(r, rmut);
        const bool ok = roundtrip && !ok2 && detectsFieldFlip;
        printf("[robotchain]   CHAIN-EXPORT-ROUNDTRIP: lossless roundtrip=%d (joints=%zu, provenance+limits preserved); "
               "corrupted export rejected=%d; field-flip detected=%d  %s\n",
               int(roundtrip), r2.joints.size(), int(!ok2), int(detectsFieldFlip), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[robotchain] %s\n", allOk ? "ALL PASS (owned-DOF chain; joint-from-feature <1e-6; typed mount port; lossless export)"
                                      : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::robot
