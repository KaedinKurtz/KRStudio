// SelfCollisionGate.cpp -- SELF-COLLISION MATRIX gates (Phase 1). The matrix is the
// one genuinely-computed MoveIt setup step, so the gate checks it against BRUTE-FORCE
// ground truth (an independent, much larger sample), asserts the SAFETY-critical
// invariant (a SOMETIMES-colliding pair is NEVER disabled), proves density-monotonicity,
// and shows the matrix feeds the planner's validity check (krs::plan::CollisionWorld::
// valid, which MotionPlanner's isStateValid calls) -- skipping disabled pairs but still
// catching a kept pair's real self-collision.

#include "SelfCollisionMatrix.hpp"
#include "MotionPlanner.hpp"

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>

namespace krs::plan {
namespace {

constexpr double kPi = 3.14159265358979323846;
Eigen::Matrix3d eye3() { return Eigen::Matrix3d::Identity(); }

// A 4-body robot with a KNOWN mix of pair classes:
//   cap0 base post (on the Z axis), cap1 upper arm, cap2 fore arm (folds back to the
//   post -> SOMETIMES vs cap0), cap3 a far bracket at radius 3.0 the arm cannot reach
//   (-> NEVER vs cap1/cap2). Adjacent: (0,1),(1,2),(0,3).
void buildGateRobot(krs::dyn::SerialChain& chain, std::vector<LinkCapsule>& caps, JointLimits& lim) {
    using namespace krs::dyn;
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye3();

    DynJoint j0; j0.type = JType::Revolute; j0.parent = -1; j0.Rtree = eye3();
    j0.ptree = Eigen::Vector3d(0, 0, 0); j0.axis = Eigen::Vector3d(0, 0, 1);   // base yaw
    const int b0 = chain.addBody(j0, body);

    DynJoint j1; j1.type = JType::Revolute; j1.parent = b0; j1.Rtree = eye3();
    j1.ptree = Eigen::Vector3d(0, 0, 0.6); j1.axis = Eigen::Vector3d(0, 1, 0);  // shoulder pitch
    const int b1 = chain.addBody(j1, body);

    DynJoint j2; j2.type = JType::Revolute; j2.parent = b1; j2.Rtree = eye3();
    j2.ptree = Eigen::Vector3d(0.5, 0, 0); j2.axis = Eigen::Vector3d(0, 1, 0);  // elbow pitch
    const int b2 = chain.addBody(j2, body);

    DynJoint j3; j3.type = JType::Revolute; j3.parent = b0; j3.Rtree = eye3();
    j3.ptree = Eigen::Vector3d(0, 0, 0); j3.axis = Eigen::Vector3d(0, 0, 1);    // a ~fixed bracket
    const int b3 = chain.addBody(j3, body);

    caps.clear();
    caps.push_back({ b0, Eigen::Vector3d(0,0,0),   Eigen::Vector3d(0,0,0.6), 0.08 });  // 0 base post
    caps.push_back({ b1, Eigen::Vector3d(0,0,0),   Eigen::Vector3d(0.5,0,0), 0.05 });  // 1 upper arm
    caps.push_back({ b2, Eigen::Vector3d(0,0,0),   Eigen::Vector3d(0.5,0,0), 0.05 });  // 2 fore arm
    caps.push_back({ b3, Eigen::Vector3d(3.0,0,0), Eigen::Vector3d(3.0,0,0.3), 0.05 }); // 3 far bracket

    lim.qLower.resize(4); lim.qUpper.resize(4); lim.vMax.resize(4);
    lim.qLower << -kPi, -1.4, -2.6, 0.0;
    lim.qUpper <<  kPi,  1.4,  2.6, 0.01;     // bracket joint ~fixed
    lim.vMax   << 2.0, 2.0, 2.0, 2.0;
}

// A tiny robot with a non-adjacent ALWAYS-colliding pair: a fat base post (r=0.3) and a
// short arm pinned near it (tiny joint ranges) so cap2 is always within r0+r2 of cap0.
void buildAlwaysRobot(krs::dyn::SerialChain& chain, std::vector<LinkCapsule>& caps, JointLimits& lim) {
    using namespace krs::dyn;
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye3();
    DynJoint j0; j0.type = JType::Revolute; j0.parent = -1; j0.axis = Eigen::Vector3d(0,0,1);
    const int b0 = chain.addBody(j0, body);
    DynJoint j1; j1.type = JType::Revolute; j1.parent = b0; j1.ptree = Eigen::Vector3d(0,0,0.2);
    j1.axis = Eigen::Vector3d(0,1,0); const int b1 = chain.addBody(j1, body);
    DynJoint j2; j2.type = JType::Revolute; j2.parent = b1; j2.ptree = Eigen::Vector3d(0.1,0,0);
    j2.axis = Eigen::Vector3d(0,1,0); const int b2 = chain.addBody(j2, body);
    caps.clear();
    caps.push_back({ b0, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0,0,0.4), 0.30 });   // fat post
    caps.push_back({ b1, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.1,0,0), 0.05 });
    caps.push_back({ b2, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.1,0,0), 0.05 });   // pinned near post
    lim.qLower.resize(3); lim.qUpper.resize(3); lim.vMax.resize(3);
    lim.qLower << -0.02, -0.02, -0.02;
    lim.qUpper <<  0.02,  0.02,  0.02;
    lim.vMax   << 1.0, 1.0, 1.0;
}

// brute-force ground-truth collide count for a pair over an independent big sample.
int gtCollideCount(const krs::dyn::SerialChain& chain, const CollisionWorld& world,
                   const JointLimits& lim, int i, int j, int bigN, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<std::uniform_real_distribution<double>> dist;
    for (int d = 0; d < chain.nq(); ++d) dist.emplace_back(lim.qLower[d], lim.qUpper[d]);
    int cnt = 0;
    for (int s = 0; s < bigN; ++s) {
        Eigen::VectorXd q(chain.nq());
        for (int d = 0; d < chain.nq(); ++d) q[d] = dist[d](rng);
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
        Eigen::Vector3d Ai, Bi, Aj, Bj;
        world.worldCapsule(poses, i, Ai, Bi); world.worldCapsule(poses, j, Aj, Bj);
        if (segSegDist(Ai, Bi, Aj, Bj) - world.capsules[i].radius - world.capsules[j].radius < 0.0) ++cnt;
    }
    return cnt;
}

// a config where the fore-arm (cap2) folds back and self-collides with the base post (cap0).
bool findKeptCollidingConfig(const krs::dyn::SerialChain& chain, const CollisionWorld& world,
                             const JointLimits& lim, Eigen::VectorXd& outQ) {
    std::mt19937 rng(777u);
    std::vector<std::uniform_real_distribution<double>> dist;
    for (int d = 0; d < chain.nq(); ++d) dist.emplace_back(lim.qLower[d], lim.qUpper[d]);
    for (int s = 0; s < 20000; ++s) {
        Eigen::VectorXd q(chain.nq());
        for (int d = 0; d < chain.nq(); ++d) q[d] = dist[d](rng);
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
        Eigen::Vector3d A0, B0, A2, B2;
        world.worldCapsule(poses, 0, A0, B0); world.worldCapsule(poses, 2, A2, B2);
        if (segSegDist(A0, B0, A2, B2) - world.capsules[0].radius - world.capsules[2].radius < 0.0) { outQ = q; return true; }
    }
    return false;
}

} // namespace

// ===========================================================================
bool runSelfCollisionMatrixGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selfcol] GATE SELFCOLLISION-MATRIX -- classify link pairs vs brute-force GT; never disable a real risk; density-monotone\n");

    krs::dyn::SerialChain chain; std::vector<LinkCapsule> caps; JointLimits lim;
    buildGateRobot(chain, caps, lim);
    CollisionWorld world; world.capsules = caps;

    const int density = 4000;
    const SelfCollisionMatrix M = generateSelfCollisionMatrix(chain, world, lim, density, 12345u);

    // brute-force ground truth (independent seed, 5x samples).
    const int bigN = 20000;
    int nNeverGT = 0, nSomeGT = 0, neverDisabledOk = 0, someKeptOk = 0, adjDisabled = 0, adjTotal = 0, dangerousDisable = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            const bool adj = CollisionWorld::adjacent(chain, caps[i].body, caps[j].body);
            if (adj) { ++adjTotal; if (M.isDisabled(i, j)) ++adjDisabled; continue; }
            const int gt = gtCollideCount(chain, world, lim, i, j, bigN, 99u);
            const bool gtNever = (gt == 0);
            const bool gtSometimes = (gt > 0 && gt < bigN);
            if (gtNever) { ++nNeverGT; if (M.isDisabled(i, j)) ++neverDisabledOk; }
            if (gtSometimes) {
                ++nSomeGT;
                if (!M.isDisabled(i, j)) ++someKeptOk;       // SAFETY: a real risk must be KEPT
                else ++dangerousDisable;                     // DANGER: disabled a reachable-colliding pair
            }
            printf("[selfcol]   pair(%d,%d) GT=%d/%d -> %s ; matrix=%s [%s]\n",
                   i, j, gt, bigN, gtNever ? "NEVER" : gtSometimes ? "SOMETIMES" : "ALWAYS",
                   M.isDisabled(i, j) ? "disabled" : "KEPT", pairClassName(M.classOf(i, j)));
        }

    // a non-adjacent ALWAYS-colliding pair (tiny pinned robot) must be disabled.
    krs::dyn::SerialChain cA; std::vector<LinkCapsule> capA; JointLimits limA;
    buildAlwaysRobot(cA, capA, limA);
    CollisionWorld wA; wA.capsules = capA;
    const SelfCollisionMatrix MA = generateSelfCollisionMatrix(cA, wA, limA, 1000, 5u);
    const bool alwaysDisabled = MA.isDisabled(0, 2) && MA.classOf(0, 2) == PairClass::Always;

    // DENSITY-MONOTONE: keep(highN) superset of keep(lowN) (superset sampling).
    const SelfCollisionMatrix Mlo = generateSelfCollisionMatrix(chain, world, lim, 500,  12345u);
    const SelfCollisionMatrix Mhi = generateSelfCollisionMatrix(chain, world, lim, 8000, 12345u);
    const auto keepLo = Mlo.keptPairSet(), keepHi = Mhi.keptPairSet();
    bool monotone = true;
    for (const auto& p : keepLo) if (!keepHi.count(p)) monotone = false;   // every low-density keep stays kept

    const bool adjOk = adjTotal > 0 && adjDisabled == adjTotal;
    const bool neverOk = nNeverGT > 0 && neverDisabledOk == nNeverGT;
    const bool someOk = nSomeGT > 0 && someKeptOk == nSomeGT && dangerousDisable == 0;
    const bool pass = adjOk && neverOk && someOk && alwaysDisabled && monotone;

    printf("[selfcol]   ADJACENT pairs disabled: %d/%d  %s\n", adjDisabled, adjTotal, adjOk ? "PASS" : "FAIL");
    printf("[selfcol]   GT-NEVER pairs disabled: %d/%d ; GT-SOMETIMES pairs KEPT: %d/%d (dangerous disables=%d)  %s\n",
           neverDisabledOk, nNeverGT, someKeptOk, nSomeGT, dangerousDisable, (neverOk && someOk) ? "PASS" : "FAIL");
    printf("[selfcol]   non-adjacent ALWAYS pair disabled: %s  %s\n", alwaysDisabled ? "yes" : "no", alwaysDisabled ? "PASS" : "FAIL");
    printf("[selfcol]   density-monotone keep(500) subset-of keep(8000): %s (|keep| %zu->%zu)  %s\n",
           monotone ? "yes" : "no", keepLo.size(), keepHi.size(), monotone ? "PASS" : "FAIL");
    printf("[selfcol] %s\n", pass ? "ALL PASS (correct classification; SOMETIMES pairs never disabled; ALWAYS/NEVER disabled; density-monotone)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runSelfCollisionFeedsPlannerGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selfcol] GATE SELFCOLLISION-FEEDS-PLANNER -- validity skips disabled pairs but STILL catches a kept pair's collision\n");

    krs::dyn::SerialChain chain; std::vector<LinkCapsule> caps; JointLimits lim;
    buildGateRobot(chain, caps, lim);
    CollisionWorld world; world.capsules = caps;   // no obstacles -> self-collision only
    const SelfCollisionMatrix M = generateSelfCollisionMatrix(chain, world, lim, 4000, 12345u);

    // a config where the KEPT pair (0,2) self-collides.
    Eigen::VectorXd qHit;
    const bool found = findKeptCollidingConfig(chain, world, lim, qHit);

    // (A) the REAL matrix keeps (0,2) -> the colliding config is INVALID (collision caught).
    CollisionWorld wReal = world; wReal.disabledSelfPairs = M.disabledPairSet();
    const bool keptStillChecked = found && !wReal.valid(chain, qHit);

    // (B) the matrix SKIPS disabled pairs -> the ALWAYS-contact robot becomes plannable.
    krs::dyn::SerialChain cA; std::vector<LinkCapsule> capA; JointLimits limA;
    buildAlwaysRobot(cA, capA, limA);
    CollisionWorld wA; wA.capsules = capA;
    const Eigen::VectorXd qA = Eigen::VectorXd::Zero(cA.nq());
    const bool blockedNoMatrix = !wA.valid(cA, qA);                 // permanent contact blocks planning
    CollisionWorld wAm = wA; wAm.disabledSelfPairs = generateSelfCollisionMatrix(cA, wA, limA, 1000, 5u).disabledPairSet();
    const bool plannableWithMatrix = wAm.valid(cA, qA);             // disabled -> contact skipped -> valid
    const bool skipsDisabled = blockedNoMatrix && plannableWithMatrix;

    // (C) NEG-CTRL (DANGEROUS): a buggy matrix that DISABLES the kept (0,2) pair lets the
    // self-colliding config pass as VALID -- a missed real self-collision.
    SelfCollisionMatrix bad = M; bad.override_(0, 2, /*disable=*/true);
    CollisionWorld wBad = world; wBad.disabledSelfPairs = bad.disabledPairSet();
    const bool dangerousMisses = found && wBad.valid(chain, qHit);  // real collision now PASSES (bad)

    const bool pass = keptStillChecked && skipsDisabled && dangerousMisses;

    printf("[selfcol]   kept pair (0,2) colliding config still INVALID with matrix: %s  %s\n",
           keptStillChecked ? "yes" : "no", keptStillChecked ? "PASS" : "FAIL");
    printf("[selfcol]   matrix skips disabled pair -> ALWAYS-contact config plannable: blocked=%s plannable=%s  %s\n",
           blockedNoMatrix ? "yes" : "no", plannableWithMatrix ? "yes" : "no", skipsDisabled ? "PASS" : "FAIL");
    printf("[selfcol]   NEG-CTRL buggy matrix disabling the kept pair lets the self-collision PASS: %s  %s\n",
           dangerousMisses ? "yes" : "no", dangerousMisses ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selfcol] %s\n", pass ? "ALL PASS (matrix feeds the validity check: disabled skipped, kept still caught; disabling a real pair is a detectable miss)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::plan
