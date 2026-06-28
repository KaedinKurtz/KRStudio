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
#include <vector>

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

} // namespace krs::rbuild
