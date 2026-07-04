// MotionBehavior.cpp -- see MotionBehavior.hpp. Play a timed motion as a behavior-tree Action.
#include "MotionBehavior.hpp"
#include <cstdio>
#include <memory>
#include <algorithm>

namespace krs::motion {

using krs::policy::Status;
using krs::policy::Blackboard;

namespace {
// Sample q at time t by linear interpolation between the trajectory's timestamped samples.
Eigen::VectorXd sampleAt(const krs::traj::TimedTrajectory& traj, double t) {
    if (traj.q.empty()) return Eigen::VectorXd();
    if (t <= traj.t.front()) return traj.q.front();
    if (t >= traj.t.back())  return traj.q.back();
    // binary search for the bracketing interval
    size_t lo = 0, hi = traj.t.size() - 1;
    while (hi - lo > 1) { const size_t mid = (lo + hi) / 2; (traj.t[mid] <= t ? lo : hi) = mid; }
    const double h = traj.t[hi] - traj.t[lo];
    const double a = (h > 1e-12) ? (t - traj.t[lo]) / h : 0.0;
    return traj.q[lo] + a * (traj.q[hi] - traj.q[lo]);
}
} // namespace

krs::policy::Action::Fn playTrajectory(const krs::traj::TimedTrajectory& traj,
                                       std::function<void(const Eigen::VectorXd&)> apply,
                                       const std::string& progressKey) {
    // Shared per-Action clock so the functor is re-entrant across ticks (a running motion resumes).
    auto clock = std::make_shared<double>(-1.0);   // -1 => not yet started
    const double duration = traj.duration;
    return [traj, apply, progressKey, clock, duration](double dt, Blackboard& bb) -> Status {
        if (traj.q.empty()) return Status::Success;      // nothing to do
        if (*clock < 0.0) *clock = 0.0; else *clock += dt;
        const double t = std::min(*clock, duration);
        const Eigen::VectorXd q = sampleAt(traj, t);
        if (apply && q.size() > 0) apply(q);
        bb.setNumber(progressKey, (duration > 1e-12) ? t / duration : 1.0);
        if (*clock >= duration - 1e-12) { *clock = -1.0; return Status::Success; }  // done -> reset for reuse
        return Status::Running;
    };
}

// ================================================================================================
// GATE MOTION-BEHAVIOR
// ================================================================================================
bool runMotionBehaviorGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[motionbt] GATE MOTION-BEHAVIOR -- a behavior tree executes timed motions in order\n");

    // Two simple 2-dof joint-space motions, time-parameterized.
    krs::traj::TimingLimits lim; lim.vMax = Eigen::VectorXd::Constant(2, 1.0); lim.aMax = Eigen::VectorXd::Constant(2, 2.0);
    auto mk = [](double a0, double a1, double b0, double b1) {
        std::vector<Eigen::VectorXd> wps(2, Eigen::VectorXd(2));
        wps[0] << a0, a1; wps[1] << b0, b1; return wps;
    };
    const krs::traj::TimedTrajectory tA = krs::traj::timeParameterize(mk(0, 0, 1.0, 0.5), lim, 0.02);
    const krs::traj::TimedTrajectory tB = krs::traj::timeParameterize(mk(1.0, 0.5, -0.5, 1.2), lim, 0.02);

    // A robot "state" the actions drive.
    Eigen::VectorXd state(2); state << 0, 0;
    auto apply = [&state](const Eigen::VectorXd& q) { state = q; };

    // Record state at the moment trajB starts (to prove ordering: trajA finished first).
    Eigen::VectorXd stateAtBStart(2); stateAtBStart << 99, 99;
    bool bStarted = false;
    auto applyB = [&](const Eigen::VectorXd& q) { if (!bStarted) { stateAtBStart = state; bStarted = true; } state = q; };

    // Sequence: play A -> wait 0.1 -> play B.
    auto seq = std::make_unique<Sequence>();
    seq->add(std::make_unique<Action>(playTrajectory(tA, apply, "prog")));
    seq->add(std::make_unique<Wait>(0.1));
    seq->add(std::make_unique<Action>(playTrajectory(tB, applyB, "prog")));
    Tree tree(std::move(seq));

    // Drive with dt ticks until the tree completes (or a tick budget).
    const double dt = 0.02;
    int ticks = 0; Status s = Status::Running;
    for (; ticks < 5000; ++ticks) { s = tree.tick(dt); if (s != Status::Running) break; }

    const bool completed = (s == Status::Success);
    const bool endsAtB = (state - tB.q.back()).cwiseAbs().maxCoeff() < 1e-6;
    // ordering: when B started, state had reached A's end (A fully executed first).
    const bool ordered = bStarted && (stateAtBStart - tA.q.back()).cwiseAbs().maxCoeff() < 1e-6;
    const double prog = tree.blackboard().getNumber("prog", -1);
    printf("[motionbt]   sequence of motions: completed=%s (%d ticks) ; ends at trajB end=%s ; A-before-B order=%s ; progress=%.2f  %s\n",
           completed?"yes":"no", ticks, endsAtB?"yes":"no", ordered?"yes":"no", prog,
           (completed && endsAtB && ordered && std::abs(prog - 1.0) < 1e-6) ? "PASS" : "FAIL");

    // NEG-CTRL: a Condition-gated motion does not run while the condition is false. Selector[
    //   Sequence[ Condition(go==true), play(tA->markRan) ], AlwaysFail ] -- with go=false the motion
    //   never applies, state stays at origin.
    Eigen::VectorXd st2(2); st2 << 0, 0;
    bool ranGated = false;
    auto gatedApply = [&](const Eigen::VectorXd& q) { ranGated = true; st2 = q; };
    auto gseq = std::make_unique<Sequence>();
    gseq->add(std::make_unique<Condition>([](Blackboard& bb) { return bb.getBool("go", false); }));
    gseq->add(std::make_unique<Action>(playTrajectory(tA, gatedApply, "g")));
    Tree gtree(std::move(gseq));
    gtree.blackboard().setBool("go", false);
    for (int i = 0; i < 50; ++i) gtree.tick(dt);
    const bool negOk = !ranGated && (st2.cwiseAbs().maxCoeff() < 1e-12);
    printf("[motionbt]   NEG-CTRL condition=false gates the motion (never ran=%s)  %s\n",
           !ranGated?"yes":"NO", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = completed && endsAtB && ordered && std::abs(prog - 1.0) < 1e-6 && negOk;
    printf("[motionbt] %s\n", pass ? "ALL PASS (a behavior tree plays timed motions in order, resumable across ticks; a condition gates a motion)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::motion
