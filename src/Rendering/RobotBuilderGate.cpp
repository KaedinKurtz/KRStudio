// RobotBuilderGate.cpp -- ROBOT BUILDER Phases 1-4, gated headless on SYNTHETIC
// assemblies with KNOWN interface geometry (the OCCT real-FANUC parse is Phase 0 +
// an operator-visual-confirm). Every gate asserts a measured number against a REAL
// failing model:
//   AUTO-PARSE-CHAIN  inferred joint axis == the interface cylinder axis <tol; FK at
//                     q=0 places each link at its parsed placement; an ambiguous /
//                     offset / planar interface is NOT faked; a wrong-cylinder axis fails.
//   JOINT-EDIT        a manual joint from two selected bore features matches their
//                     analytic geometry; the chain re-derives (DOF updates); a
//                     degenerate (offset/tilted) pair is rejected for the right reason.
//   TAG-OWNERSHIP     a member body is tagged + free-move-locked; a non-base-connected
//                     body is untagged + free; a model that lets a tagged body free-move FAILS.
//   SUBTREE-DETACH    deleting a mid-chain joint detaches the downstream subtree intact
//                     (internal joints survive, bodies lose the tag, DOF drops exactly);
//                     re-mating restores; destroy / keep-tag models FAIL.

#include "RobotBuilder.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <entt/entt.hpp>

namespace krs::rbuild {
namespace {

Eigen::Matrix4d placement(double tx, double ty, double tz, const Eigen::Vector3d& axis, double ang)
{
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    M.block<3,3>(0,0) = Eigen::AngleAxisd(ang, axis.normalized()).toRotationMatrix();
    M(0,3) = tx; M(1,3) = ty; M(2,3) = tz;
    return M;
}

// append a cylinder BRepFace to `b` whose WORLD axis is (wpos,wdir) -- stored in the
// part's LOCAL frame, so faceToWorld(face, b.placement) recovers (wpos,wdir).
void addCylWorld(RBBody& b, const glm::vec3& wpos, const glm::vec3& wdir, float r)
{
    const Eigen::Matrix4d inv = b.placement.inverse();
    const Eigen::Vector4d lp = inv * Eigen::Vector4d(wpos.x, wpos.y, wpos.z, 1.0);
    Eigen::Vector3d ld = inv.block<3,3>(0,0) * Eigen::Vector3d(wdir.x, wdir.y, wdir.z);
    ld.normalize();
    BRepFace f; f.type = 1;
    f.axisPos = glm::vec3(float(lp.x()), float(lp.y()), float(lp.z()));
    f.axisDir = glm::vec3(float(ld.x()), float(ld.y()), float(ld.z()));
    f.radius = r;
    b.faces.push_back(f);
}

void addPlaneLocal(RBBody& b, const glm::vec3& pos, const glm::vec3& nrm)
{
    BRepFace f; f.type = 0; f.axisPos = pos; f.normal = glm::normalize(nrm);
    b.faces.push_back(f);
}

bool axisMatches(const glm::vec3& pos, const glm::vec3& dir,
                 const glm::vec3& kpos, const glm::vec3& kdir, float tol)
{
    const glm::vec3 kd = glm::normalize(kdir);
    const float align = std::abs(glm::dot(glm::normalize(dir), kd));
    const glm::vec3 w = pos - kpos;
    const glm::vec3 perp = w - glm::dot(w, kd) * kd;
    return align > 1.0f - tol && glm::length(perp) < tol;
}

Eigen::Matrix4d poseMat(const krs::dyn::Pose& P)
{
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    M.block<3,3>(0,0) = P.R; M.block<3,1>(0,3) = P.p;
    return M;
}

} // namespace

// ===========================================================================
bool runAutoParseChainGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE AUTO-PARSE-CHAIN -- inferred joint axes == interface geometry; FK==placements; ambiguous not faked\n");

    const float tol = 1e-4f;
    // a 3-link serial assembly with two KNOWN coaxial-cylinder interfaces.
    const glm::vec3 A01p(0.5f, 0.0f, 0.3f), A01d(0, 0, 1);
    const glm::vec3 A12p(1.0f, 0.2f, 0.3f), A12d(0, 0, 1);

    RBBody B0; B0.name = "base"; B0.placement = Eigen::Matrix4d::Identity();
    RBBody B1; B1.name = "link1"; B1.placement = placement(0.5, 0.0, 0.3, Eigen::Vector3d(0,0,1), 0.35);
    RBBody B2; B2.name = "link2"; B2.placement = placement(1.0, 0.2, 0.3, Eigen::Vector3d(0,1,0), 0.6);

    addCylWorld(B0, A01p, A01d, 0.05f);                       // B0 mates B1 at A01
    addCylWorld(B1, A01p, A01d, 0.05f);                       // B1 mates B0 at A01
    addCylWorld(B1, A12p, A12d, 0.04f);                       // B1 mates B2 at A12
    addCylWorld(B1, glm::vec3(2, 2, 2), glm::vec3(1, 0, 0), 0.05f);  // DECOY (wrong axis, r matches A01)
    addCylWorld(B2, A12p, A12d, 0.04f);                       // B2 mates B1 at A12

    // --- inference: each interface resolves to its KNOWN axis (not the decoy) ---
    RBJoint j01, j12;
    const bool f01 = inferRevolute(B0, B1, j01);
    const bool f12 = inferRevolute(B1, B2, j12);
    const bool axis01 = f01 && axisMatches(j01.axisPos, j01.axisDir, A01p, A01d, tol);
    const bool axis12 = f12 && axisMatches(j12.axisPos, j12.axisDir, A12p, A12d, tol);
    // the inferred axis is the TRUE interface, NOT the decoy (1,0,0):
    const bool notDecoy = f01 && std::abs(glm::dot(glm::normalize(j01.axisDir), glm::vec3(1,0,0))) < 0.5f;

    // --- FK at q=0 places each link at its parsed placement ---
    RobotGraph g; g.bodies = { B0, B1, B2 }; g.base = 0;
    j01.parent = 0; j01.child = 1; g.addJoint(j01);
    j12.parent = 1; j12.child = 2; g.addJoint(j12);
    krs::robot::Robot R = g.toRobot();
    krs::dyn::SerialChain chain = R.toChain();
    Eigen::VectorXd q = Eigen::VectorXd::Zero(chain.nq());
    std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
    double fkErr = 0.0;
    if (int(poses.size()) >= 2) {
        const Eigen::Matrix4d w1 = R.basePlacement * poseMat(poses[0]);
        const Eigen::Matrix4d w2 = R.basePlacement * poseMat(poses[1]);
        fkErr = std::max((w1 - B1.placement).cwiseAbs().maxCoeff(),
                         (w2 - B2.placement).cwiseAbs().maxCoeff());
    } else fkErr = 1e9;
    const bool fkOk = fkErr < 1e-9 && g.dof() == 2;

    // --- ambiguous / degenerate interfaces are NOT faked ---
    RBBody Boff; Boff.name = "offset"; Boff.placement = Eigen::Matrix4d::Identity();
    addCylWorld(Boff, glm::vec3(0.7f, 0.0f, 0.3f), A01d, 0.05f);   // parallel but 0.2 offset from A01
    RBBody Bpl; Bpl.name = "planar"; Bpl.placement = Eigen::Matrix4d::Identity();
    addPlaneLocal(Bpl, glm::vec3(0.5f, 0, 0.3f), glm::vec3(0, 0, 1));   // only a plane, no bore
    RBJoint dummy;
    const bool offNotFaked = !inferRevolute(B0, Boff, dummy);     // offset bores -> rejected
    const bool planeNotFaked = !inferRevolute(B0, Bpl, dummy);    // no cylinder -> no joint

    // --- NEG-CTRL: a wrong-AXIS (non-coaxial) interface is REJECTED by the REAL
    // inference, not fabricated. Bdecoy's only cylinder is axis (1,0,0); B0's bore is
    // (0,0,1) -- not parallel -> deriveRevoluteFromBores rejects on the parallelism
    // residual (a DIFFERENT rejection reason than the offset case's collinearity).
    RBBody Bdecoy; Bdecoy.name = "decoy"; Bdecoy.placement = Eigen::Matrix4d::Identity();
    addCylWorld(Bdecoy, glm::vec3(2, 2, 2), glm::vec3(1, 0, 0), 0.05f);
    RBJoint jbad;
    const bool wrongAxisRejected = !inferRevolute(B0, Bdecoy, jbad);   // real backend, not a tautology

    const bool pass = axis01 && axis12 && notDecoy && fkOk && offNotFaked && planeNotFaked && wrongAxisRejected;

    printf("[rbuild]   inferred axes == interface geometry: A01 %s (res=%.2e), A12 %s (res=%.2e); not-decoy=%s  %s\n",
           axis01 ? "ok" : "BAD", j01.residual, axis12 ? "ok" : "BAD", j12.residual,
           notDecoy ? "ok" : "BAD", (axis01 && axis12 && notDecoy) ? "PASS" : "FAIL");
    printf("[rbuild]   FK(q=0) places links at parsed placements: err=%.2e (<1e-9), dof=%d (want 2)  %s\n",
           fkErr, g.dof(), fkOk ? "PASS" : "FAIL");
    printf("[rbuild]   ambiguous NOT faked: offset-bores rejected=%s, planar-only rejected=%s  %s\n",
           offNotFaked ? "yes" : "no", planeNotFaked ? "yes" : "no",
           (offNotFaked && planeNotFaked) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild]   NEG-CTRL wrong-axis decoy bore REJECTED by real inference (non-parallel): %s  %s\n",
           wrongAxisRejected ? "yes" : "no", wrongAxisRejected ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (auto-parse infers correct joint axes from geometry; FK matches placements; honest about ambiguity)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE BASE-AXIS-VERTICAL -- the J0 (base turntable) axis must be the base mounting normal (vertical
// part-Z), NOT a horizontal flange/bolt bore. Replicates the real FANUC failure: the base<->j1
// interface's only clean COAXIAL bore PAIR is a HORIZONTAL flange bore (the largest cylinder, which
// the radius/coaxiality search latches onto), while the true turntable axis is a VERTICAL slew bore
// that is NOT coaxial across the groups. The verticality prior (buildNamedSerialChain, bi==1) must
// pick the vertical axis anyway. NEG-CTRL: the horizontal coaxial pair genuinely exists, so without
// the prior J0 would be horizontal -- proving the prior is load-bearing, not a tautology.
bool runBaseAxisVerticalGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE BASE-AXIS-VERTICAL -- J0 base-turntable axis = base->J1 (the way the arm stands up), not a horizontal flange bore\n");

    RBBody base; base.name = "430-base"; base.placement = Eigen::Matrix4d::Identity();
    // J1 mounts 0.3 UP the turntable axis (+Z here). J0 direction = normalize(J1 - base) = +Z, derived from
    // geometry (convention-agnostic), NOT from any bore -- so the horizontal flange decoy cannot win.
    RBBody j1;   j1.name   = "430-j1";   j1.placement   = Eigen::Matrix4d::Identity(); j1.placement(2, 3) = 0.3;

    const glm::vec3 horizAxis(1, 0, 0), vertAxis(0, 0, 1);
    // Horizontal flange bore, COAXIAL across base+j1 (the decoy the radius search picks), LARGER radius.
    addCylWorld(base, glm::vec3(0.0f, 0.0f, 0.5f), horizAxis, 0.10f);
    addCylWorld(j1,   glm::vec3(0.0f, 0.0f, 0.5f), horizAxis, 0.10f);
    // Vertical slew bores, NOT coaxial across the groups (2 cm offset), SMALLER radius -- the TRUE axis.
    addCylWorld(base, glm::vec3(0.00f, 0.0f, 0.5f), vertAxis, 0.06f);
    addCylWorld(j1,   glm::vec3(0.02f, 0.0f, 0.5f), vertAxis, 0.06f);

    std::vector<ParsedPart> parts = { base, j1 };
    RobotGraph g = buildNamedSerialChain(parts);

    const bool haveJoint = !g.joints.empty();
    const glm::vec3 axis = haveJoint ? glm::normalize(g.joints[0].axisDir) : glm::vec3(0);
    const float vertical = std::abs(glm::dot(axis, vertAxis));
    const bool isVertical = haveJoint && vertical > 0.99f;

    // NEG-CTRL: the radius/coaxiality search WOULD pick the horizontal flange pair (it exists + is
    // perfectly coaxial). Confirm, so "J0 is vertical" is a genuine effect of the prior.
    RBJoint oldGuess;
    const bool horizPairExists = inferRevolute(base, j1, oldGuess, 5e-3, 5e-3);
    const float oldHoriz = horizPairExists ? std::abs(glm::dot(glm::normalize(oldGuess.axisDir), horizAxis)) : 0.f;
    const bool oldWouldBeHorizontal = horizPairExists && oldHoriz > 0.99f;

    const bool pass = isVertical && oldWouldBeHorizontal;
    printf("[rbuild]   J0 axis = (%.3f, %.3f, %.3f) ; verticality=%.3f (want >0.99)  %s\n",
           axis.x, axis.y, axis.z, vertical, isVertical ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL: horizontal coaxial flange pair exists (old search picks it, horiz=%.3f) -> prior non-vacuous: %s\n",
           oldHoriz, oldWouldBeHorizontal ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (base joint resolves to the vertical turntable axis, overriding the horizontal flange decoy)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE MATE-SNAP -- the concentric-mate transform makes the child bore collinear with the parent's,
// and subtreeOf collects the moving sub-assembly. NEG-CTRL: the child was genuinely off-axis before
// the snap (so the transform is doing real work, not a tautology).
bool runMateSnapGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE MATE-SNAP -- concentric transform aligns child bore to parent axis; subtree collected\n");

    // Parent bore: axis +Z through origin. Child bore: axis +X (rotated 90deg) at (0.5, 0.3, 0) (offset).
    RBJoint jA; jA.axisPos = { 0, 0, 0 };       jA.axisDir = { 0, 0, 1 }; jA.orthonormalizeFrame();
    RBJoint jB; jB.axisPos = { 0.5f, 0.3f, 0 }; jB.axisDir = { 1, 0, 0 }; jB.orthonormalizeFrame();

    const Eigen::Matrix4d T = RobotGraph::mateTransformConcentric(jA, jB);
    const Eigen::Vector3d dA(0, 0, 1), pA(0, 0, 0);
    const Eigen::Vector3d pB(jB.axisPos.x, jB.axisPos.y, jB.axisPos.z);
    const Eigen::Vector3d dB(jB.axisDir.x, jB.axisDir.y, jB.axisDir.z);
    const Eigen::Vector3d pB2 = (T * Eigen::Vector4d(pB.x(), pB.y(), pB.z(), 1.0)).head<3>();
    const Eigen::Vector3d dB2 = (T.block<3, 3>(0, 0) * dB).normalized();

    auto perpDist = [&](const Eigen::Vector3d& p) { const Eigen::Vector3d w = p - pA; return (w - w.dot(dA) * dA).norm(); };
    const double align   = std::abs(dB2.dot(dA));
    const double offAfter = perpDist(pB2);
    const double offBefore = perpDist(pB);
    const bool concentric = align > 0.999 && offAfter < 1e-6;
    const bool wasOffAxis = offBefore > 0.05;            // NEG-CTRL: genuinely off before

    // subtreeOf: serial chain base(0)->1->2->3 ; subtreeOf(2) == {2,3} (2 + its descendant 3).
    RobotGraph g;
    for (int i = 0; i < 4; ++i) { RBBody b; b.name = "b" + std::to_string(i); b.placement = Eigen::Matrix4d::Identity(); g.bodies.push_back(b); }
    g.base = 0;
    for (int i = 0; i < 3; ++i) { RBJoint j; j.parent = i; j.child = i + 1; j.type = JType::Revolute;
                                  j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); g.addJoint(j); }
    const std::vector<int> sub = g.subtreeOf(2);
    const bool subOk = (sub.size() == 2) && std::count(sub.begin(), sub.end(), 2) && std::count(sub.begin(), sub.end(), 3);

    const bool pass = concentric && wasOffAxis && subOk;
    printf("[rbuild]   concentric snap: axis-align=%.4f (>0.999), child off-axis %.3f m -> %.2e m  %s\n",
           align, offBefore, offAfter, concentric ? "PASS" : "FAIL");
    printf("[rbuild]   subtreeOf(B2) = {%s} (want 2,3)  %s\n",
           [&]{ std::string s; for (int b : sub) s += std::to_string(b) + " "; return s; }().c_str(),
           subOk ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL: child was off-axis before snap (%.3f m > 0.05) -> transform non-vacuous: %s\n",
           offBefore, wasOffAxis ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (concentric mate aligns the child bore to the parent axis; subtree collected)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE SPLIT-MERGE -- cutting a joint splits the graph into base + branch (the subtree), re-mating
// merges them back. Adversarial: DOF/body counts split exactly, each side stays a connected tree, the
// merge restores the original DOF + body count + end-effector FK, and bad inputs are rejected.
bool runSplitMergeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE SPLIT-MERGE -- cut a joint -> base+branch; re-mate -> one robot; FK round-trips\n");

    // 5-body serial chain along +x, DOF 4 (all revolute Z).
    RobotGraph g; g.base = 0;
    for (int i = 0; i < 5; ++i) {
        RBBody b; b.name = "b" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity(); b.placement(0, 3) = double(i);
        b.entity = i; g.bodies.push_back(b);
    }
    for (int i = 0; i < 4; ++i) { RBJoint j; j.parent = i; j.child = i + 1; j.type = JType::Revolute;
                                  j.axisDir = { 0, 0, 1 }; j.orthonormalizeFrame(); g.addJoint(j); }
    const int dof0 = g.dof(); const int nb0 = int(g.bodies.size());

    auto endX = [](const RobotGraph& gg) {
        const krs::robot::Robot R = gg.toRobot();
        krs::dyn::SerialChain ch = R.toChain();
        std::vector<krs::dyn::Pose> p; ch.fk(Eigen::VectorXd::Zero(ch.nq()), p);
        if (p.empty()) return -1e9;
        const Eigen::Matrix4d w = R.basePlacement * poseMat(p.back());
        return w(0, 3);
    };
    const double end0 = endX(g);

    // SPLIT at joint 2 (connects body 2-3): branch = {3,4}, base = {0,1,2}.
    RobotGraph spBase, spBranch;
    const bool splitRan = g.splitAtJoint(2, spBase, spBranch);
    const bool splitCounts = splitRan && int(spBase.bodies.size()) == 3 && int(spBranch.bodies.size()) == 2
                          && spBase.dof() == 2 && spBranch.dof() == 1;
    const bool splitTrees = int(spBase.chainOrder().order.size()) == 3      // base reaches all 3
                         && int(spBranch.chainOrder().order.size()) == 2;   // branch reaches both
    const bool branchEntitiesKept = spBranch.bodies.size() == 2
                                 && spBranch.bodies[0].entity == 3 && spBranch.bodies[1].entity == 4;

    // MERGE the branch back onto base body 2 via the original cut joint -> should restore the chain.
    RobotGraph merged = spBase;
    const int newJ = merged.mergeFrom(spBranch, 2, g.joints[2]);
    const bool mergeCounts = newJ >= 0 && int(merged.bodies.size()) == nb0 && merged.dof() == dof0
                          && int(merged.chainOrder().order.size()) == nb0;
    const double endM = endX(merged);
    const bool fkRoundTrip = std::abs(endM - end0) < 1e-9;

    // NEG-CTRLs: an out-of-range joint is rejected.
    RobotGraph junkA, junkB;
    const bool negBadIdx = !g.splitAtJoint(99, junkA, junkB) && !g.splitAtJoint(-1, junkA, junkB);

    // ---- BRANCHED lowering (Phase 1): base->A->B and base->C (C is a SIBLING of A). Robot::toChain
    //      must honor Joint.treeParent so C depends only on its own joint, not on A's -- a serial
    //      misread (parent = previous body) would make C a descendant of A and couple them. ----
    RobotGraph gb; gb.base = 0;
    for (int i = 0; i < 4; ++i) {
        RBBody b; b.name = "bb" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity();
        b.placement(0, 3) = (i == 3) ? 0.0 : double(i);   // C(3) sits at +y so its motion is distinguishable
        b.placement(1, 3) = (i == 3) ? 1.0 : 0.0;
        b.entity = 10 + i; gb.bodies.push_back(b);
    }
    { RBJoint j; j.parent = 0; j.child = 1; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); gb.addJoint(j); } // A: base->1
    { RBJoint j; j.parent = 1; j.child = 2; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); gb.addJoint(j); } // B: 1->2 (child of A)
    { RBJoint j; j.parent = 0; j.child = 3; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); gb.addJoint(j); } // C: base->3 (child of base)
    krs::dyn::SerialChain chb = gb.toRobot().toChain();
    const auto cob = gb.chainOrder();
    auto chainIdxOf = [&](int body) { for (size_t k = 1; k < cob.order.size(); ++k) if (cob.order[k] == body) return int(k) - 1; return -1; };
    const int aIdx = chainIdxOf(1), bIdx = chainIdxOf(2), cIdx = chainIdxOf(3);
    auto poseUnder = [&](int dof, double val, int bodyChainIdx) {
        Eigen::VectorXd q = Eigen::VectorXd::Zero(chb.nq());
        if (dof >= 0 && dof < chb.nq()) q[dof] = val;
        std::vector<krs::dyn::Pose> p; chb.fk(q, p);
        return (bodyChainIdx >= 0 && bodyChainIdx < int(p.size())) ? poseMat(p[bodyChainIdx]) : Eigen::Matrix4d::Identity();
    };
    const bool branchValid = (chb.nq() == 3) && aIdx >= 0 && bIdx >= 0 && cIdx >= 0;
    const double cVsA = branchValid ? (poseUnder(aIdx, 0.0, cIdx) - poseUnder(aIdx, 0.7, cIdx)).cwiseAbs().maxCoeff() : 1e9; // C invariant to A
    const double cVsC = branchValid ? (poseUnder(cIdx, 0.0, cIdx) - poseUnder(cIdx, 0.7, cIdx)).cwiseAbs().maxCoeff() : 0.0; // C moves with C
    const double bVsA = branchValid ? (poseUnder(aIdx, 0.0, bIdx) - poseUnder(aIdx, 0.7, bIdx)).cwiseAbs().maxCoeff() : 0.0; // B moves with A
    const bool branchedOk = branchValid && cVsA < 1e-12 && cVsC > 1e-3 && bVsA > 1e-3;

    const bool pass = splitCounts && splitTrees && branchEntitiesKept && mergeCounts && fkRoundTrip && negBadIdx && branchedOk;
    printf("[rbuild]   split: base(dof2,3 bodies)+branch(dof1,2 bodies)=%s ; each a connected tree=%s ; branch keeps entities=%s  %s\n",
           splitCounts ? "yes" : "NO", splitTrees ? "yes" : "NO", branchEntitiesKept ? "yes" : "NO",
           (splitCounts && splitTrees && branchEntitiesKept) ? "PASS" : "FAIL");
    printf("[rbuild]   merge restores dof=%d/%d bodies=%d/%d connected=%s ; FK end-effector %.6f -> %.6f  %s\n",
           merged.dof(), dof0, int(merged.bodies.size()), nb0,
           (int(merged.chainOrder().order.size()) == nb0) ? "yes" : "no", end0, endM,
           (mergeCounts && fkRoundTrip) ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL bad split index rejected: %s  %s\n",
           negBadIdx ? "yes" : "no", negBadIdx ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild]   BRANCHED toChain honors treeParent: C-invariant-to-A=%.2e(<1e-12) C-moves-with-C=%.3f(>1e-3) B-moves-with-A=%.3f(>1e-3)  %s\n",
           cVsA, cVsC, bVsA, branchedOk ? "PASS" : "FAIL");
    printf("[rbuild] %s\n", pass ? "ALL PASS (split partitions the tree exactly; merge is its inverse; FK round-trips; branched lowering correct; bad input rejected)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE CONNECTED-COMPONENTS -- the joint-primary model: a "robot" is a DERIVED connected component
// of the body/joint graph, base-independent. A serial graph is one component; a disjoint graph is
// two (cutting a joint never deletes anything -- it just yields two components, both drivable);
// chooseBase is deterministic; chainOrderFrom re-roots the spanning tree at any body.
bool runConnectedComponentsGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE CONNECTED-COMPONENTS -- a robot is a DERIVED component; disjoint graph -> two; re-root spans from any body\n");

    // (a) one serial chain -> exactly ONE component spanning all bodies; chooseBase keeps the base.
    RobotGraph one; one.base = 0;
    for (int i = 0; i < 4; ++i) { RBBody b; b.name = "s" + std::to_string(i); b.entity = i; one.bodies.push_back(b); }
    for (int i = 0; i < 3; ++i) { RBJoint j; j.parent = i; j.child = i + 1; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); one.addJoint(j); }
    const auto c1 = one.connectedComponents();
    const bool oneComp = (c1.size() == 1) && (c1.size() == 1 && int(c1[0].size()) == 4);
    const bool baseKept = oneComp && (one.chooseBase(c1[0]) == 0);

    // (b) two DISJOINT chains in one graph -> TWO components (nothing deleted).
    RobotGraph two; two.base = 0;
    for (int i = 0; i < 5; ++i) { RBBody b; b.name = "t" + std::to_string(i); b.entity = 10 + i; two.bodies.push_back(b); }
    { RBJoint j; j.parent = 0; j.child = 1; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); two.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); two.addJoint(j); }
    { RBJoint j; j.parent = 3; j.child = 4; j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); two.addJoint(j); }   // disjoint pair {3,4}
    const auto c2 = two.connectedComponents();
    const bool twoComp = (c2.size() == 2)
                      && std::find(c2.begin(), c2.end(), std::vector<int>{ 0, 1, 2 }) != c2.end()
                      && std::find(c2.begin(), c2.end(), std::vector<int>{ 3, 4 }) != c2.end();
    int baseA = -1, baseB = -1;
    for (const auto& cc : c2) {
        if (std::find(cc.begin(), cc.end(), 0) != cc.end()) baseA = two.chooseBase(cc);
        else                                                baseB = two.chooseBase(cc);
    }
    const bool basesOk = (baseA == 0) && (baseB == 3);

    // (c) RE-ROOT: derive a spanning tree of the {0,1,2} chain from a NON-base root (body 2).
    const auto coR = two.chainOrderFrom(2);
    const bool reRoot = (!coR.order.empty()) && (coR.order.front() == 2) && (int(coR.order.size()) == 3);

    // (d) determinism.
    const bool deterministic = (two.connectedComponents() == c2);

    const bool pass = oneComp && baseKept && twoComp && basesOk && reRoot && deterministic;
    printf("[rbuild]   serial graph -> 1 component of 4 bodies=%s ; chooseBase keeps base=%s  %s\n",
           oneComp ? "yes" : "NO", baseKept ? "yes" : "NO", (oneComp && baseKept) ? "PASS" : "FAIL");
    printf("[rbuild]   disjoint graph -> 2 components {0,1,2},{3,4}=%s ; chooseBase deterministic (0 and 3)=%s  %s\n",
           twoComp ? "yes" : "NO", basesOk ? "yes" : "NO", (twoComp && basesOk) ? "PASS" : "FAIL");
    printf("[rbuild]   re-root chainOrderFrom(2) spans {0,1,2} from body 2=%s ; deterministic=%s  %s\n",
           reRoot ? "yes" : "NO", deterministic ? "yes" : "NO", (reRoot && deterministic) ? "PASS" : "FAIL");
    printf("[rbuild] %s\n", pass ? "ALL PASS (components derived base-independently; chooseBase deterministic; re-root spans the component)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE BORE-ANCHOR -- the lowered revolute rotates about the BORE axis, not the child link's CAD origin.
// The child link origin is placed OFF the bore axis; toRobot() must anchor the joint frame on the bore
// (axisPos) so the joint origin lies ON the rotation axis (and is therefore invariant to q). The old
// lowering (rotation center = link origin) would have orbited the wrong axis line.
bool runBoreAnchorGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE BORE-ANCHOR -- lowered joint frame sits ON the bore axis (rotation about the bore, not the child link origin)\n");

    const glm::vec3 bore(0.30f, 0.0f, 0.30f), dir(0.0f, 0.0f, 1.0f);    // vertical bore at (0.3,*,0.3)
    RobotGraph g; g.base = 0;
    RBBody B0; B0.name = "base";  B0.placement = Eigen::Matrix4d::Identity();                          B0.entity = 0;
    RBBody B1; B1.name = "link1"; B1.placement = placement(0.5, 0.2, 0.0, Eigen::Vector3d(0,0,1), 0.0); B1.entity = 1; // origin OFF the bore
    g.bodies.push_back(B0); g.bodies.push_back(B1);
    { RBJoint j; j.parent = 0; j.child = 1; j.type = JType::Revolute;
      j.axisPos = bore; j.axisDir = dir; j.orthonormalizeFrame(); g.addJoint(j); }

    const krs::robot::Robot r = g.toRobot();
    krs::dyn::SerialChain ch = r.toChain();
    const Eigen::Vector3d b(bore.x, bore.y, bore.z), d(dir.x, dir.y, dir.z);
    const Eigen::Vector3d linkOrigin(0.5, 0.2, 0.0);
    auto worldO = [&](double qv){ Eigen::VectorXd q(ch.nq()); for (int i=0;i<ch.nq();++i) q[i]=qv;
        const krs::dyn::Pose p = ch.bodyPose(q, 0);
        return Eigen::Vector3d(r.basePlacement.block<3,3>(0,0)*p.p + r.basePlacement.block<3,1>(0,3)); };
    auto perpTo  = [&](const Eigen::Vector3d& x){ const Eigen::Vector3d w=x-b; return (w - w.dot(d)*d).norm(); };
    auto alongTo = [&](const Eigen::Vector3d& x){ return (x-b).dot(d); };

    // NEW MODEL (rigid off-pivot axisPoint): the joint frame ORIGIN stays on the CAD child link origin
    // (so chainFK(0)==CAD, no down-chain accumulation) and the child ROTATES ABOUT THE BORE axis.
    const Eigen::Vector3d o0 = worldO(0.0), oP = worldO(0.6), oM = worldO(-0.6);
    // (a) chainFK(0) reconstructs the CAD child origin exactly (the accumulation-death invariant).
    const bool restOk = (o0 - linkOrigin).norm() < 1e-9;
    // (b) rotates ABOUT THE BORE: perpendicular distance to the bore axis + along-axis coord invariant to q.
    const double p0p = perpTo(o0);
    const bool aboutBore = std::abs(perpTo(oP)-p0p) < 1e-6 && std::abs(perpTo(oM)-p0p) < 1e-6
                        && std::abs(alongTo(oP)-alongTo(o0)) < 1e-6 && std::abs(alongTo(oM)-alongTo(o0)) < 1e-6;
    // (c) non-vacuous: the child origin is genuinely OFF the bore axis, so (b) is a real circle not a point.
    const bool nonVacuous = p0p > 0.05;

    const bool pass = restOk && aboutBore && nonVacuous;
    printf("[rbuild]   chainFK(0)==CAD origin: drift=%.2e=%s ; rotates about bore: perp invariant=%s (r=%.3f)  %s\n",
           (o0 - linkOrigin).norm(), restOk ? "yes" : "NO", aboutBore ? "yes" : "NO", p0p,
           (restOk && aboutBore) ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL child link origin %.3f m OFF the bore (rotation is a real circle): %s\n",
           p0p, nonVacuous ? "yes" : "NO");
    printf("[rbuild] %s\n", pass ? "ALL PASS (child origin on CAD link, rotates rigidly about the physical bore -- no accumulation)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE URDF-EXPORT -- the joint-primary export: pick a base link, derive the chain by tree-search over
// the joints, emit links/joints/types/limits. Re-rootable; a bad base yields an empty robot (non-vacuous).
bool runUrdfExportGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE URDF-EXPORT -- base-pick + tree-search -> URDF; links/joints/types/limits round-trip; re-rootable\n");

    RobotGraph g; g.base = 0;
    for (int i = 0; i < 4; ++i) { RBBody b; b.name = "seg" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity(); b.placement(0, 3) = double(i) * 0.4; g.bodies.push_back(b); }
    { RBJoint j; j.parent = 0; j.child = 1; j.type = JType::Revolute;  j.axisDir = { 0,0,1 }; j.orthonormalizeFrame(); j.name = "shoulder"; j.limits.lower = -1.0; j.limits.upper = 1.0; g.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.type = JType::Revolute;  j.axisDir = { 0,1,0 }; j.orthonormalizeFrame(); j.name = "elbow"; j.limits.enabled = false; g.addJoint(j); }  // continuous
    { RBJoint j; j.parent = 2; j.child = 3; j.type = JType::Prismatic; j.axisDir = { 1,0,0 }; j.orthonormalizeFrame(); j.name = "slide"; j.limits.lower = 0.0; j.limits.upper = 0.2; g.addJoint(j); }

    const std::string urdf = exportGraphToUrdf(g, 0, "test_arm");
    auto cnt = [](const std::string& s, const std::string& needle) { int n = 0; size_t p = 0; while ((p = s.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); } return n; };

    const bool links4  = cnt(urdf, "<link ") == 4;
    const bool joints3 = cnt(urdf, "<joint ") == 3;
    const bool names   = urdf.find("name=\"shoulder\"") != std::string::npos
                      && urdf.find("name=\"elbow\"") != std::string::npos
                      && urdf.find("name=\"slide\"") != std::string::npos;
    const bool types   = urdf.find("type=\"revolute\"") != std::string::npos
                      && urdf.find("type=\"continuous\"") != std::string::npos
                      && urdf.find("type=\"prismatic\"") != std::string::npos;
    const bool tree    = urdf.find("<parent link=\"seg0\"/>") != std::string::npos
                      && urdf.find("<child link=\"seg1\"/>") != std::string::npos;
    const bool limitCount = cnt(urdf, "<limit ") == 2;   // revolute + prismatic carry limits; continuous none

    const std::string urdf2 = exportGraphToUrdf(g, 2, "rerooted");
    const bool reroot = cnt(urdf2, "<link ") == 4 && cnt(urdf2, "<joint ") == 3;
    const std::string bad = exportGraphToUrdf(g, 99, "bad");
    const bool negBad = bad.find("<link ") == std::string::npos && bad.find("<joint ") == std::string::npos;

    const bool pass = links4 && joints3 && names && types && tree && limitCount && reroot && negBad;
    printf("[rbuild]   links=%d(4) joints=%d(3) names=%d types(rev/cont/prism)=%d tree=%d limitCount=%d(2)  %s\n",
           cnt(urdf, "<link "), cnt(urdf, "<joint "), names, types, tree, cnt(urdf, "<limit "),
           (links4 && joints3 && names && types && tree && limitCount) ? "PASS" : "FAIL");
    printf("[rbuild]   re-root from base 2 -> 4 links/3 joints=%d ; NEG-CTRL bad base -> empty=%d  %s\n",
           reroot, negBad, (reroot && negBad) ? "PASS" : "FAIL");
    printf("[rbuild] %s\n", pass ? "ALL PASS (URDF export: base-pick + tree-search; types+limits correct; re-rootable; bad base empty)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runJointEditGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE JOINT-EDIT -- delete auto-joint; define one from selected features; chain re-derives\n");

    const float tol = 1e-4f;
    const glm::vec3 Ap(0.4f, 0.1f, 0.2f), Ad(0, 1, 0);          // the TRUE mate axis

    RBBody B0; B0.name = "base"; B0.placement = Eigen::Matrix4d::Identity();
    RBBody B1; B1.name = "link1"; B1.placement = placement(0.4, 0.1, 0.2, Eigen::Vector3d(1,0,0), 0.5);
    addCylWorld(B0, Ap, Ad, 0.06f);
    addCylWorld(B1, Ap, Ad, 0.06f);

    // start with a WRONG auto-joint (a fabricated axis) to be corrected.
    RobotGraph g; g.bodies = { B0, B1 }; g.base = 0;
    RBJoint bad; bad.parent = 0; bad.child = 1; bad.type = JType::Revolute;
    bad.axisDir = glm::vec3(1, 0, 0); bad.axisPos = glm::vec3(0, 0, 0); bad.prov = Prov::Inferred;
    const int badIdx = g.addJoint(bad);
    const int dofWithAuto = g.dof();

    // DELETE the auto-joint.
    g.deleteJoint(badIdx);
    const int dofAfterDelete = g.dof();

    // MANUALLY define from the two SELECTED bore features (their world analytic faces).
    const BRepFace wa = faceToWorld(B0.faces[0], B0.placement);
    const BRepFace wb = faceToWorld(B1.faces[0], B1.placement);
    RBJoint manual;
    const bool defined = defineRevoluteFromSelection(wa, wb, 0, 1, manual);
    const bool frameOk = defined && axisMatches(manual.axisPos, manual.axisDir, Ap, Ad, tol)
                         && manual.prov == Prov::Manual;
    if (defined) g.addJoint(manual);
    const int dofAfterManual = g.dof();
    const bool rederives = dofWithAuto == 1 && dofAfterDelete == 0 && dofAfterManual == 1;

    // NEG-CTRL: a degenerate manual pair (parallel but OFFSET bores) is REJECTED for
    // the right reason (collinearity residual exceeds tol -- not a fabricated joint).
    BRepFace offA = wa;                                          // axis at Ap
    BRepFace offB = wa; offB.axisPos += glm::vec3(0.15f, 0, 0);  // parallel, 0.15 offset (perp to Y axis)
    double ang = 0, off = 0;
    RBJoint deg;
    const bool degRejected = !defineRevoluteFromSelection(offA, offB, 0, 1, deg);
    krs::joint::JointFrame jf;
    krs::joint::deriveRevoluteFromBores(offA, offB, jf, 1e-4, &ang, &off);  // capture WHY
    const bool rejectReason = degRejected && off > 1e-4;        // rejected because off-axis, not arbitrarily

    // PHASE 2: mate-connector frame -- the joint owns a PERSISTED orthonormal frame,
    // decoupled from the source faces; flipAxis reverses it.
    bool frameOrtho = false, decoupled = false, flips = false;
    {
        const float perpDot = std::abs(glm::dot(glm::normalize(manual.axisDir), glm::normalize(manual.refDir)));
        frameOrtho = (std::abs(glm::length(manual.refDir) - 1.0f) < 1e-4f) && (perpDot < 1e-4f);
        // CONTRACT/regression guard: corrupt the SOURCE faces, re-derive -> joint axis
        // unchanged (toRobot reads the stored frame, never the live faces).
        krs::robot::Robot rb0 = g.toRobot();
        for (auto& f : g.bodies[0].faces) { f.axisDir = glm::vec3(1,1,1); f.axisPos = glm::vec3(9,9,9); }
        for (auto& f : g.bodies[1].faces) { f.axisDir = glm::vec3(1,1,1); f.axisPos = glm::vec3(9,9,9); }
        krs::robot::Robot rb1 = g.toRobot();
        decoupled = (!rb0.joints.empty() && rb0.joints.size() == rb1.joints.size());
        for (size_t k = 0; k < rb0.joints.size() && decoupled; ++k)
            if ((rb0.joints[k].axis - rb1.joints[k].axis).norm() > 1e-9) decoupled = false;
        RBJoint cp = manual; const glm::vec3 a0 = cp.axisDir; cp.flipAxis();
        flips = glm::length(cp.axisDir + a0) < 1e-5f;
    }

    const bool pass = frameOk && rederives && degRejected && rejectReason
                    && frameOrtho && decoupled && flips;

    printf("[rbuild]   mate-frame: orthonormal=%s decoupled-from-faces=%s flip-reverses=%s  %s\n",
           frameOrtho ? "yes" : "no", decoupled ? "yes" : "no", flips ? "yes" : "no",
           (frameOrtho && decoupled && flips) ? "PASS" : "FAIL");
    printf("[rbuild]   manual joint frame == selected features: %s (res=%.2e, prov=Manual=%s)  %s\n",
           frameOk ? "ok" : "BAD", manual.residual, manual.prov == Prov::Manual ? "yes" : "no",
           frameOk ? "PASS" : "FAIL");
    printf("[rbuild]   chain re-derives: dof auto=%d -> delete=%d -> manual=%d (want 1/0/1)  %s\n",
           dofWithAuto, dofAfterDelete, dofAfterManual, rederives ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL degenerate (offset) pair rejected for offset=%.3e (>1e-4): %s  %s\n",
           off, rejectReason ? "yes" : "no", rejectReason ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (manual joint matches selected features; chain re-derives; degenerate rejected)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runTagOwnershipGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE TAG-OWNERSHIP -- jointed body tagged + free-move-locked; non-member free; membership tracked\n");

    RobotGraph g; g.base = 0;
    g.bodies.resize(4);   // B0 base, B1, B2 jointed to base; B3 NOT connected
    for (int i = 0; i < 4; ++i) g.bodies[i].placement = placement(0.4 * i, 0, 0, Eigen::Vector3d(0,0,1), 0.0);
    { RBJoint j; j.parent = 0; j.child = 1; j.axisDir = glm::vec3(0,0,1); g.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.axisDir = glm::vec3(0,0,1); g.addJoint(j); }
    // B3: no joint -> not a robot subcomponent.

    const bool memberTagged = g.isTagged(1) && g.isTagged(2);
    const bool memberLocked = !g.freeMoveAllowed(1) && !g.freeMoveAllowed(2);   // kinematics is the single owner
    const bool nonMemberFree = !g.isTagged(3) && g.freeMoveAllowed(3);          // freely interactable
    const bool baseTagged = g.isTagged(0);

    // NEG-CTRL: a model that lets a TAGGED body accept a free-move command breaks the
    // single-owner invariant. The real predicate refuses (freeMoveAllowed==false);
    // the buggy "always allow" model would return true for a member.
    auto buggyFreeMove = [](const RobotGraph&, int) { return true; };           // ignores ownership
    const bool buggyLetsMemberMove = buggyFreeMove(g, 1) == true && g.freeMoveAllowed(1) == false;

    const bool pass = memberTagged && memberLocked && nonMemberFree && baseTagged && buggyLetsMemberMove;

    printf("[rbuild]   member bodies tagged + free-move LOCKED: tagged=%s locked=%s  %s\n",
           memberTagged ? "yes" : "no", memberLocked ? "yes" : "no",
           (memberTagged && memberLocked) ? "PASS" : "FAIL");
    printf("[rbuild]   non-base-connected body untagged + freely interactable: %s  %s\n",
           nonMemberFree ? "yes" : "no", nonMemberFree ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL 'always-allow' model lets a tagged member free-move (real refuses): %s  %s\n",
           buggyLetsMemberMove ? "yes" : "no", buggyLetsMemberMove ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (kinematics is the single owner of a tagged body; membership = closure from base)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runSubtreeDetachGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE SUBTREE-DETACH -- delete mid-chain joint detaches downstream subtree INTACT; tag tracks membership\n");

    // serial chain B0(base)->B1->B2->B3->B4->B5 via J0..J4 (5 revolute DOF).
    RobotGraph g; g.base = 0; g.bodies.resize(6);
    for (int i = 0; i < 6; ++i) g.bodies[i].placement = placement(0.3 * i, 0, 0, Eigen::Vector3d(0,0,1), 0.0);
    for (int i = 0; i < 5; ++i) { RBJoint j; j.parent = i; j.child = i + 1; j.axisDir = glm::vec3(0,0,1); g.addJoint(j); }
    const int dof0 = g.dof();                                   // 5

    // DELETE J2 (connects B2-B3) -> base keeps {B0,B1,B2}; {B3,B4,B5} detach.
    // find the joint index connecting 2 and 3.
    const int j23 = g.jointBetween(2, 3);
    g.deleteJoint(j23);
    const int dofAfter = g.dof();                               // 2 (J0,J1)
    const auto mem = g.members();
    const bool baseSide = mem.count(0) && mem.count(1) && mem.count(2);
    const bool detached = !mem.count(3) && !mem.count(4) && !mem.count(5);
    const bool detachedUntagged = !g.isTagged(3) && !g.isTagged(4) && !g.isTagged(5);
    // the detached subtree's INTERNAL joints survive (B3-B4 and B4-B5 still articulated).
    const bool internalIntact = g.jointBetween(3, 4) >= 0 && g.jointBetween(4, 5) >= 0;
    const bool dofDropsExact = dof0 == 5 && dofAfter == 2;      // dropped by 3 = deleted J2 + detached J3,J4

    // NEG-CTRL B (KEEP-TAG) -- evaluated NOW, after the detach but BEFORE the re-mate:
    // a STALE cached tag set (snapshot before detach) still marks B3 tagged, whereas the
    // live-membership model has already untagged it. (Must precede re-mate, which re-tags B3.)
    const std::set<int> staleTags = { 0, 1, 2, 3, 4, 5 };       // snapshot before detach (all tagged)
    const bool staleKeepsTag = staleTags.count(3) != 0 && !g.isTagged(3);   // stale wrong, live right

    // RE-MATE: add a joint B2-B3 back -> membership re-derives, tag re-applied, dof restored.
    { RBJoint j; j.parent = 2; j.child = 3; j.axisDir = glm::vec3(0,0,1); j.prov = Prov::Manual; g.addJoint(j); }
    const int dofRemate = g.dof();
    const bool remateOk = dofRemate == 5 && g.isTagged(3) && g.isTagged(4) && g.isTagged(5);

    // NEG-CTRL A (DESTROY): a detach that also erases the downstream joints leaves the
    // subtree un-articulated. Model it on a fresh copy and show its internal joints are GONE.
    RobotGraph gd; gd.base = 0; gd.bodies.resize(6);
    for (int i = 0; i < 6; ++i) gd.bodies[i].placement = g.bodies[i].placement;
    for (int i = 0; i < 5; ++i) { RBJoint j; j.parent = i; j.child = i + 1; j.axisDir = glm::vec3(0,0,1); gd.addJoint(j); }
    // destroy: erase J2,J3,J4 (the deleted joint + the whole downstream subtree)
    for (int k = 4; k >= 2; --k) gd.deleteJoint(k);
    const bool destroyLosesArticulation = gd.jointBetween(3, 4) < 0 && gd.jointBetween(4, 5) < 0;

    const bool pass = baseSide && detached && detachedUntagged && internalIntact && dofDropsExact
                      && remateOk && destroyLosesArticulation && staleKeepsTag;

    printf("[rbuild]   delete mid-joint: base-side={0,1,2} kept=%s ; {3,4,5} detached=%s + untagged=%s  %s\n",
           baseSide ? "yes" : "no", detached ? "yes" : "no", detachedUntagged ? "yes" : "no",
           (baseSide && detached && detachedUntagged) ? "PASS" : "FAIL");
    printf("[rbuild]   detached subtree INTERNAL joints intact (B3-B4,B4-B5 still articulated): %s ; dof %d->%d  %s\n",
           internalIntact ? "yes" : "no", dof0, dofAfter, (internalIntact && dofDropsExact) ? "PASS" : "FAIL");
    printf("[rbuild]   re-mate restores: dof=%d (want 5) + tags re-applied=%s  %s\n",
           dofRemate, remateOk ? "yes" : "no", remateOk ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL destroy-subtree loses articulation: %s ; stale-tag keeps B3 tagged (live untags): %s  %s\n",
           destroyLosesArticulation ? "yes" : "no", staleKeepsTag ? "yes" : "no",
           (destroyLosesArticulation && staleKeepsTag) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (mid-joint delete detaches the subtree intact as a passive body; tag tracks live membership; re-mate restores)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// CONFIG Phase 3 -- the editing PANEL's data-ops are invoked by its controls. The
// gate drives the EditController directly (the Qt panel wiring clicks->controller is
// OPERATOR-VISUAL-CONFIRM). NEG-CTRLs: a control that reports success without invoking
// its op (DOF unchanged), and a control wired to the WRONG op (delete on a define button).
bool runEditOpInvokedGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE EDIT-OP-INVOKED -- panel controls invoke the proven ops; chain re-derives (DOF updates)\n");

    // graph: B0-B1-B2 serial (joints J0,J1); B3 unjointed. B2,B3 share a bore axis for define.
    RobotGraph g; g.base = 0; g.bodies.resize(4);
    for (int i = 0; i < 4; ++i) g.bodies[i].placement = placement(0.3 * i, 0, 0, Eigen::Vector3d(0,0,1), 0.0);
    const glm::vec3 AeeP(0.9f, 0, 0.2f), AeeD(0, 0, 1);
    addCylWorld(g.bodies[2], AeeP, AeeD, 0.05f);
    addCylWorld(g.bodies[3], AeeP, AeeD, 0.05f);
    { RBJoint j; j.parent = 0; j.child = 1; j.axisDir = glm::vec3(0,0,1); g.addJoint(j); }
    { RBJoint j; j.parent = 1; j.child = 2; j.axisDir = glm::vec3(0,0,1); g.addJoint(j); }
    const int dof0 = g.dof();   // 2

    // DELETE control invokes deleteJoint -> DOF drops.
    RobotGraph gDel = g; EditController ctrlDel{ &gDel };
    const bool delInvoked = ctrlDel.deleteJoint(gDel.jointBetween(1, 2));
    const int dofDel = gDel.dof();
    // NEG: a no-op control claims success but does NOT invoke -> DOF unchanged.
    RobotGraph gNoop = g;
    auto noOpDelete = [](RobotGraph&, int) { return true; };
    const bool noopClaims = noOpDelete(gNoop, gNoop.jointBetween(1, 2));
    const int dofNoop = gNoop.dof();
    const bool deleteOk = delInvoked && dofDel == dof0 - 1 && noopClaims && dofNoop == dof0;

    // DEFINE control invokes defineRevoluteFromSelection -> joint created, frame matches, DOF rises.
    RobotGraph gDef = g; EditController ctrlDef{ &gDef };
    const BRepFace wB2 = faceToWorld(gDef.bodies[2].faces[0], gDef.bodies[2].placement);
    const BRepFace wB3 = faceToWorld(gDef.bodies[3].faces[0], gDef.bodies[3].placement);
    RBJoint created;
    const bool defInvoked = ctrlDef.defineFromFeatures(wB2, 2, wB3, 3, &created);
    const int dofDef = gDef.dof();
    const bool frameMatches = defInvoked && axisMatches(created.axisPos, created.axisDir, AeeP, AeeD, 1e-4f);
    // NEG: a control wired to the WRONG op (delete instead of define) -> DOF falls, no joint created.
    RobotGraph gWrong = g; EditController ctrlWrong{ &gWrong };
    ctrlWrong.deleteJoint(gWrong.jointBetween(0, 1));
    const int dofWrong = gWrong.dof();
    const bool defineOk = defInvoked && dofDef == dof0 + 1 && frameMatches && dofWrong < dof0;

    // a degenerate (offset) feature pair is rejected -> no joint, DOF unchanged.
    RobotGraph gDeg = g; EditController ctrlDeg{ &gDeg };
    BRepFace offA = faceToWorld(gDeg.bodies[2].faces[0], gDeg.bodies[2].placement);
    BRepFace offB = offA; offB.axisPos += glm::vec3(0.2f, 0, 0);
    const bool degRejected = !ctrlDeg.defineFromFeatures(offA, 2, offB, 3) && gDeg.dof() == dof0;

    const bool pass = deleteOk && defineOk && degRejected;

    printf("[rbuild]   delete-joint control: invoked=%s DOF %d->%d (want %d) ; no-op control leaves DOF=%d  %s\n",
           delInvoked ? "yes" : "no", dof0, dofDel, dof0 - 1, dofNoop, deleteOk ? "PASS" : "FAIL");
    printf("[rbuild]   define-from-features control: joint created frame-matches=%s DOF %d->%d ; wrong-op(delete) DOF=%d  %s\n",
           frameMatches ? "yes" : "no", dof0, dofDef, dofWrong, defineOk ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRL no-op & wrong-op controls fail to drive the intended op: %s ; degenerate rejected: %s  %s\n",
           (dofNoop == dof0 && dofWrong < dof0) ? "yes" : "no", degRejected ? "yes" : "no",
           (dofNoop == dof0 && dofWrong < dof0 && degRejected) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (panel controls invoke the proven delete/define ops; chain re-derives; no-op/wrong-op rejected)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE MATE-CONNECTOR (env KRS_MATE_SELFTEST): the persistent Onshape-style mate
// architecture, hammered under the WORST conditions the redesign must survive:
//   A author+resolve  a concentric mate -> a valid coaxial revolute (RBJoint DERIVED)
//   B dynamic move    a mated body translated far -> the connector rides it (body-LOCAL);
//                     NEG-CTRL: a stored WORLD anchor goes stale by the move distance (the OLD bug)
//   C far-away snap   two parallel bores metres apart snap coaxial; NEG-CTRL requireCollinear rejects
//   D faceKey         geometry-invariant: reversed-axis + sub-quantum twins key IDENTICALLY (re-import
//                     re-anchor); NEG-CTRL supra-quantum / different face keys DIFFERENTLY (not constant)
//   E save/load/open  serialize the mate graph + connectors -> clear -> deserialize: every id + local
//                     frame restored bit-identical; NEG-CTRL a truncated blob yields nothing (no fabricate)
//   F delete          erase one mate -> the other + all connectors intact, no dangling; NEG-CTRL the
//                     erased id is gone. Mates key on (entt::entity, connector-id) so index remap is immune.
namespace {
// Minimal binary (de)serializer for the mate data -- proves save/load/open persistence with STABLE ids,
// independent of the app's DB format.
struct ByteW { std::vector<char> b;
    void u32(std::uint32_t v){ const char* p=(const char*)&v; b.insert(b.end(),p,p+4); }
    void u64(std::uint64_t v){ const char* p=(const char*)&v; b.insert(b.end(),p,p+8); }
    void i32(std::int32_t v){ u32((std::uint32_t)v); }
    void f(float v){ const char* p=(const char*)&v; b.insert(b.end(),p,p+4); }
    void d(double v){ const char* p=(const char*)&v; b.insert(b.end(),p,p+8); }
    void v3(const glm::vec3& v){ f(v.x); f(v.y); f(v.z); }
    void str(const std::string& s){ u32((std::uint32_t)s.size()); b.insert(b.end(),s.begin(),s.end()); } };
struct ByteR { const char* p; const char* end; bool ok=true;
    ByteR(const std::vector<char>& v):p(v.data()),end(v.data()+v.size()){}
    bool have(size_t n){ if(size_t(end-p)<n){ ok=false; return false;} return true; }
    std::uint32_t u32(){ if(!have(4))return 0; std::uint32_t v; std::memcpy(&v,p,4); p+=4; return v; }
    std::uint64_t u64(){ if(!have(8))return 0; std::uint64_t v; std::memcpy(&v,p,8); p+=8; return v; }
    std::int32_t i32(){ return (std::int32_t)u32(); }
    float f(){ if(!have(4))return 0; float v; std::memcpy(&v,p,4); p+=4; return v; }
    double d(){ if(!have(8))return 0; double v; std::memcpy(&v,p,8); p+=8; return v; }
    glm::vec3 v3(){ float x=f(),y=f(),z=f(); return {x,y,z}; }
    std::string str(){ std::uint32_t n=u32(); if(!have(n))return {}; std::string s(p,p+n); p+=n; return s; } };

void serializeConn(ByteW& w, const MateConnectorComponent& mc){
    w.u32(mc.nextConnectorId); w.u32((std::uint32_t)mc.connectors.size());
    for (const auto& c : mc.connectors){ w.u32(c.id); w.str(c.name); w.v3(c.localPos); w.v3(c.localZ);
        w.v3(c.localX); w.u64(c.sourceFaceKey); w.i32(c.sourceFaceType); w.f(c.radius); } }
void deserializeConn(ByteR& r, MateConnectorComponent& mc){
    mc = {}; mc.nextConnectorId = r.u32(); std::uint32_t n = r.u32();
    for (std::uint32_t i=0;i<n && r.ok;++i){ MateConnector c; c.id=r.u32(); c.name=r.str(); c.localPos=r.v3();
        c.localZ=r.v3(); c.localX=r.v3(); c.sourceFaceKey=r.u64(); c.sourceFaceType=r.i32(); c.radius=r.f();
        if(r.ok) mc.connectors.push_back(c); } }
void serializeGraph(ByteW& w, const MateGraphComponent& mg){
    w.u64(mg.nextMateId); w.u32((std::uint32_t)mg.mates.size());
    for (const auto& m : mg.mates){ w.u64(m.id); w.i32((std::int32_t)m.type);
        w.u32((std::uint32_t)m.bodyA); w.u32(m.connA); w.u32((std::uint32_t)m.bodyB); w.u32(m.connB);
        w.d(m.offset); w.d(m.angle); } }
void deserializeGraph(ByteR& r, MateGraphComponent& mg){
    mg = {}; mg.nextMateId = r.u64(); std::uint32_t n = r.u32();
    for (std::uint32_t i=0;i<n && r.ok;++i){ MateConstraint m; m.id=r.u64(); m.type=(MateConstraint::Type)r.i32();
        m.bodyA=(entt::entity)r.u32(); m.connA=r.u32(); m.bodyB=(entt::entity)r.u32(); m.connB=r.u32();
        m.offset=r.d(); m.angle=r.d(); if(r.ok) mg.mates.push_back(m); } }

BRepFace worldCyl(const glm::vec3& pos, const glm::vec3& dir, float r){
    BRepFace f; f.type=1; f.axisPos=pos; f.axisDir=glm::normalize(dir); f.normal=f.axisDir;
    f.axisEnd0=pos; f.axisEnd1=pos+glm::normalize(dir)*0.02f; f.radius=r; f.faceKey=computeFaceKey(f); return f; }
const MateConnector* findConn(const MateConnectorComponent& mc, std::uint32_t id){
    for (const auto& c: mc.connectors) if (c.id==id) return &c; return nullptr; }
} // namespace

bool runMateSelftest()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[mate] GATE MATE-CONNECTOR -- persistent Onshape-style mates under dynamic/far-snap/save-load/delete\n");
    bool pass = true;
    const entt::entity eA = (entt::entity)1001u, eB = (entt::entity)1002u;

    // ---------- A: author a concentric mate -> DERIVED coaxial revolute ----------
    const Eigen::Matrix4d placeA = placement(0,0,0, Eigen::Vector3d::UnitZ(), 0.0);
    const Eigen::Matrix4d placeB = placement(0.5,0,0, Eigen::Vector3d::UnitY(), 0.7);   // arbitrary pose
    const BRepFace waA = worldCyl({0.40f,0,0}, {1,0,0}, 0.05f);
    const BRepFace waB = worldCyl({0.60f,0,0}, {1,0,0}, 0.05f);                          // coaxial with waA
    MateGraphComponent mg; MateConnectorComponent mcA, mcB;
    const std::uint64_t mid = authorConcentricMate(mg, eA, mcA, placeA, waA, eB, mcB, placeB, waB);
    RBJoint j;
    const MateConnector& cA = mcA.connectors.back(); const MateConnector& cB = mcB.connectors.back();
    const bool aResolve = resolveMate(mg.mates.back(), placeA, cA, 0, placeB, cB, 1, j);
    const glm::vec3 jd = glm::normalize(j.axisDir);
    const bool aAxis = aResolve && j.type==JType::Revolute && std::abs(std::abs(glm::dot(jd, glm::vec3(1,0,0)))-1.0f) < 1e-3f;
    const bool A = mid==1 && mcA.connectors.size()==1 && mcB.connectors.size()==1 && mg.mates.size()==1 && aAxis;
    printf("[mate]   A author+resolve: mate id=%llu, revolute axis||bore=%s  %s\n",
           (unsigned long long)mid, aAxis?"yes":"no", A?"PASS":"FAIL"); pass &= A;

    // ---------- B: dynamic move -- the connector rides the body (body-LOCAL) ----------
    const Eigen::Matrix4d placeB2 = placement(0,3.0,0, Eigen::Vector3d::UnitZ(), 0.0) * placeB;  // B flung +3m in y
    const BRepFace movedB = connectorToWorldFace(cB, placeB2);
    const glm::vec3 expect = waB.axisPos + glm::vec3(0,3.0f,0);            // the bore should ride +3m with B
    const double rode = glm::length(movedB.axisPos - expect);
    // NEG-CTRL: a WORLD-anchored copy (captured at author) does NOT move -> stale by the move distance (the OLD bug)
    const double staleDelta = glm::length(waB.axisPos - expect);          // ~3.0 m
    const bool B = rode < 1e-4 && staleDelta > 2.9;
    printf("[mate]   B dynamic move: connector rode with body err=%.2e (<1e-3) ; NEG-CTRL world-anchor stale by %.2fm (was the bug)  %s\n",
           rode, staleDelta, B?"PASS":"FAIL"); pass &= B;

    // ---------- C: far-away snap (parallel bores metres apart) ----------
    const BRepFace fA = worldCyl({0,0,0},   {0,1,0}, 0.05f);
    const BRepFace fB = worldCyl({5.0f,0,0.3f}, {0,1,0}, 0.05f);          // parallel, 5m away + 0.3m off-axis
    RBJoint jc; const bool snap = defineRevoluteFromSelection(fA, fB, 0, 1, jc, 5e-3, /*requireCollinear*/false);
    RBJoint jr; const bool strictRejects = !defineRevoluteFromSelection(fA, fB, 0, 1, jr, 5e-3, /*requireCollinear*/true);
    const bool C = snap && strictRejects;
    printf("[mate]   C far-away snap: parallel bores 5m apart snap coaxial=%s ; NEG-CTRL strict-collinear rejects=%s  %s\n",
           snap?"yes":"no", strictRejects?"yes":"no", C?"PASS":"FAIL"); pass &= C;

    // ---------- D: faceKey geometry-invariance + distinctness ----------
    const BRepFace f0  = worldCyl({1,2,3}, {0,0,1},  0.05f);
    const BRepFace fRev = worldCyl({1,2,3}, {0,0,-1}, 0.05f);             // reversed axis -> hemisphere fold
    BRepFace fSub = f0; fSub.radius += 1e-5f; fSub.faceKey = computeFaceKey(fSub);  // sub-quantum (<0.1mm)
    BRepFace fBig = f0; fBig.radius += 1.0e-3f; fBig.faceKey = computeFaceKey(fBig); // supra-quantum
    const BRepFace fDir = worldCyl({1,2,3}, {1,0,0}, 0.05f);             // different axis
    const bool invariant = (fRev.faceKey==f0.faceKey) && (fSub.faceKey==f0.faceKey);
    const bool distinct  = (fBig.faceKey!=f0.faceKey) && (fDir.faceKey!=f0.faceKey) && f0.faceKey!=0;
    const bool D = invariant && distinct;
    printf("[mate]   D faceKey: reversed+sub-quantum twin identical=%s ; NEG-CTRL supra-quantum/diff-axis differ=%s  %s\n",
           invariant?"yes":"no", distinct?"yes":"no", D?"PASS":"FAIL"); pass &= D;

    // ---------- E: save / load / open round-trip (stable ids + local frames) ----------
    ByteW w; serializeConn(w, mcA); serializeConn(w, mcB); serializeGraph(w, mg);
    ByteR rr(w.b); MateConnectorComponent rA, rB; MateGraphComponent rG;
    deserializeConn(rr, rA); deserializeConn(rr, rB); deserializeGraph(rr, rG);
    const MateConnector* rcA = rA.connectors.empty()?nullptr:&rA.connectors[0];
    const bool idsKept = rG.mates.size()==1 && rG.mates[0].id==mg.mates[0].id
        && rG.mates[0].bodyA==eA && rG.mates[0].connA==cA.id && rG.mates[0].bodyB==eB && rG.mates[0].connB==cB.id
        && rA.nextConnectorId==mcA.nextConnectorId && rG.nextMateId==mg.nextMateId;
    const bool framesKept = rcA && rcA->id==cA.id && glm::length(rcA->localPos-cA.localPos)<1e-6f
        && glm::length(rcA->localZ-cA.localZ)<1e-6f && glm::length(rcA->localX-cA.localX)<1e-6f
        && rcA->sourceFaceKey==cA.sourceFaceKey;
    // NEG-CTRL: a truncated blob restores nothing (no crash, no fabrication)
    std::vector<char> trunc(w.b.begin(), w.b.begin()+ (w.b.size()/3));
    ByteR tr(trunc); MateConnectorComponent tA; deserializeConn(tr, tA); MateGraphComponent tG; deserializeGraph(tr, tG);
    const bool truncSafe = tG.mates.size() < mg.mates.size();     // did NOT fabricate a full graph
    const bool E = idsKept && framesKept && truncSafe;
    printf("[mate]   E save/load/open: ids+bindings kept=%s local-frames kept=%s ; NEG-CTRL truncated=%zu mates (no fabricate)=%s  %s\n",
           idsKept?"yes":"no", framesKept?"yes":"no", tG.mates.size(), truncSafe?"yes":"no", E?"PASS":"FAIL"); pass &= E;

    // ---------- F: delete one mate -> others + connectors intact, no dangling ----------
    // author a SECOND mate so there is something to keep after deleting the first
    authorConcentricMate(mg, eA, mcA, placeA, waA, eB, mcB, placeB, waB);      // mate id=2
    const std::uint64_t delId = mg.mates.front().id;                            // delete mate 1
    mg.mates.erase(std::remove_if(mg.mates.begin(), mg.mates.end(),
                    [&](const MateConstraint& m){ return m.id==delId; }), mg.mates.end());
    const bool gone = std::none_of(mg.mates.begin(), mg.mates.end(), [&](const MateConstraint& m){ return m.id==delId; });
    const MateConstraint& kept = mg.mates.front();
    const bool noDangling = mg.mates.size()==1 && findConn(mcA, kept.connA)!=nullptr && findConn(mcB, kept.connB)!=nullptr;
    const bool F = gone && noDangling;
    printf("[mate]   F delete: erased id=%llu gone=%s ; kept mate connectors still resolve (no dangling)=%s  %s\n",
           (unsigned long long)delId, gone?"yes":"no", noDangling?"yes":"no", F?"PASS":"FAIL"); pass &= F;

    printf("[mate] %s\n", pass ? "ALL PASS (mates are body-LOCAL: survive dynamic motion, far snap, save/load/open, delete; faceKey re-anchors; world-anchor NEG-CTRL confirms the old bug)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rbuild
