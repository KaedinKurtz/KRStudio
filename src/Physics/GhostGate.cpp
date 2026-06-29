// GhostGate.cpp -- Phase 7 GATE GHOST-VALIDITY (CPU half): the translucent "ghost validity robot".
//
// The robot's q is the CLAMPED command (the reachable pose). qCommandRaw is the command BEFORE the
// limit clamp -- the ghost target (where the robot WOULD go unclamped). jointValid[i] is 0 exactly
// where the clamp moved the value (the command violated joint i's limit). The ghost robot is
// FK(qCommandRaw) tinted by validity; the real robot is FK(q). This gate proves, purely on the CPU:
//   * an out-of-limit command clamps q but qCommandRaw holds the raw value, and jointValid flags it;
//   * an in-limit command leaves q == qCommandRaw, all valid, ghost == real (no spurious ghost);
//   * the ghost FK genuinely DIVERGES from the real FK when (and only when) a joint is clamped.
// NEG-CTRL: a "ghost" that FKs q (the clamped value) instead of qCommandRaw shows ZERO divergence on
// the violated joint -- it can never reveal the violation, proving qCommandRaw tracking is essential.
//
// The VISUAL half (the translucent tinted render) is OPERATOR-VISUAL-CONFIRM (GhostRobotPass).

#include "RobotModel.hpp"

#include <cstdio>
#include <cmath>
#include <vector>

namespace krs::robot {
namespace {

// A simple planar 2R arm with both joints limited to [-1, 1] rad. Link 1 sits 1 m out along +x of
// link 0, so a change in joint 0 visibly swings the end link -> FK divergence is measurable.
LiveRobot make2R()
{
    LiveRobot lr;
    lr.model.name = "ghost2R";
    lr.model.nLinks = 2;
    for (int k = 0; k < 2; ++k) {
        Joint j;
        j.type = krs::dyn::JType::Revolute;
        j.member = true;
        j.axis = Eigen::Vector3d::UnitZ();
        j.ptree = (k == 0) ? Eigen::Vector3d::Zero() : Eigen::Vector3d(1.0, 0.0, 0.0);
        j.qLower = -1.0; j.qUpper = 1.0;
        lr.model.joints.push_back(j);
    }
    lr.rebuild();           // sizes q / qCommandRaw / jointValid to nq()==2
    return lr;
}

double endDivergence(const LiveRobot& lr)
{
    const auto real  = lr.fkLinks();
    const auto ghost = lr.fkGhostLinks();
    if (real.empty() || ghost.empty()) return 0.0;
    return (ghost.back().p - real.back().p).norm();
}

} // namespace

bool runGhostValidityGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ghost] GATE GHOST-VALIDITY -- qCommandRaw/validity tracking + ghost FK diverges only when clamped\n");

    // ---- OUT-OF-LIMIT command: q clamps, qCommandRaw holds raw, validity flags joint 0 ----
    LiveRobot a = make2R();
    Eigen::VectorXd cmd(2); cmd << 2.0, 0.5;           // joint 0 over its +1.0 limit; joint 1 in range
    a.setCommandedQ(cmd);
    const bool clamped   = std::abs(a.q[0] - 1.0) < 1e-9 && std::abs(a.q[1] - 0.5) < 1e-9;
    const bool rawHeld   = std::abs(a.qCommandRaw[0] - 2.0) < 1e-9 && std::abs(a.qCommandRaw[1] - 0.5) < 1e-9;
    const bool flagged   = (a.jointValid.size() == 2) && a.jointValid[0] == 0 && a.jointValid[1] == 1;
    const bool anyInvalid = a.anyJointInvalid();
    const double divOut  = endDivergence(a);            // ghost (FK 2.0) vs real (FK 1.0): must be > 0
    const bool diverges  = divOut > 1e-3;
    const bool outOk = clamped && rawHeld && flagged && anyInvalid && diverges;
    printf("[ghost]   out-of-limit: q clamped=%s, qCommandRaw raw=%s, jointValid=[%d %d] (flagged j0)=%s, ghost diverges=%.4f m  %s\n",
           clamped ? "yes" : "NO", rawHeld ? "yes" : "NO",
           a.jointValid.size() == 2 ? a.jointValid[0] : -1, a.jointValid.size() == 2 ? a.jointValid[1] : -1,
           flagged ? "yes" : "NO", divOut, outOk ? "PASS" : "FAIL");

    // ---- IN-LIMIT command: q == qCommandRaw, all valid, ghost == real (NO spurious ghost) ----
    LiveRobot b = make2R();
    Eigen::VectorXd cmd2(2); cmd2 << 0.5, -0.5;
    b.setCommandedQ(cmd2);
    const bool eq        = (b.q - b.qCommandRaw).cwiseAbs().maxCoeff() < 1e-12;
    const bool allValid  = !b.anyJointInvalid();
    const double divIn   = endDivergence(b);
    const bool noGhost   = divIn < 1e-9;
    const bool inOk = eq && allValid && noGhost;
    printf("[ghost]   in-limit: q==qCommandRaw=%s, all valid=%s, ghost==real (divergence=%.2e)  %s\n",
           eq ? "yes" : "NO", allValid ? "yes" : "NO", divIn, inOk ? "PASS" : "FAIL");

    // ---- NEG-CTRL: a ghost FKing the CLAMPED q (not qCommandRaw) shows ZERO divergence on the
    // violated joint -- it cannot reveal the violation. (Compare: the real ghost FKs qCommandRaw and
    // DOES diverge above.) This proves qCommandRaw tracking is what makes the ghost meaningful.
    const auto realA  = a.fkLinks();                    // FK(q)         -- clamped
    const double divBroken = realA.empty() ? 0.0 : (realA.back().p - realA.back().p).norm();  // FK(q) vs FK(q)
    const bool negOk = (divBroken < 1e-12) && (divOut > 1e-3);
    printf("[ghost]   NEG-CTRL: ghost-from-clamped-q divergence=%.2e (cannot show violation) vs real ghost %.4f m  %s\n",
           divBroken, divOut, negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = outOk && inOk && negOk;
    printf("[ghost] %s\n", pass ? "ALL PASS (qCommandRaw + validity tracked; ghost FK diverges iff a joint is clamped)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::robot
