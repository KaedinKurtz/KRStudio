// ===========================================================================
// OMPL sprint, Phase 2 — EXECUTE gates (env KRS_EXECUTE_SELFTEST; in the bench).
//
// Execute a PLANNED path (Phase 1 MotionPlanner) through the computed-torque
// controller (krs::ctrl::computedTorque) and verify the robot actually FOLLOWS
// it. Execution is pure-CPU forward dynamics (krs::dyn::SerialChain::
// forwardDynamics + semi-implicit Euler) under GRAVITY — self-contained and
// deterministic, like Phase 1. Gravity is the physical discriminator: the
// computed-torque law feeds back the model (M*a + biasForces incl. gravity) so
// it tracks; the OLD soft PD (tau = Kp*e - Kd*qd, no model / no gravity
// feedforward) lags a moving setpoint and sags under gravity -> it FAILS the
// tracking bound (the non-vacuous negative control).
//
//   EXECUTE-TRACK          : peak ||q_achieved - q_commanded|| DURING motion; CT
//                            below bound, soft PD above it (report both).
//   EXECUTE-COLLISION-FREE : the ACHIEVED CT trajectory is collision-free vs the
//                            true world (planned with a safety margin so tracking
//                            error stays clear). NEG: executing a colliding
//                            straight-line reference -> the achieved path collides.
//   EXECUTE-LIMITS         : achieved q within [qLower,qUpper], achieved |qd| <=
//                            vMax. NEG: a 3x-fast re-timing -> achieved |qd| > vMax.
// ===========================================================================
#include "MotionPlanner.hpp"
#include "ComputedTorque.hpp"
#include "RobotModel.hpp"     // Phase 5 E2E: define the robot via the chain data model

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace krs::plan {

namespace {

constexpr double kPi = 3.14159265358979323846;
Eigen::Matrix3d eye3() { return Eigen::Matrix3d::Identity(); }

// Same 3R arm as the PLAN gate, but with realistic link inertia + COM at the
// link midpoints so the pitch joints carry a genuine gravity load (needed for
// the soft-PD negative control to sag/lag measurably).
void buildArmDyn(krs::dyn::SerialChain& chain, std::vector<LinkCapsule>& caps) {
    using namespace krs::dyn;
    auto body = [](double m, const Eigen::Vector3d& com) {
        DynBody b; b.mass = m; b.com = com; b.inertiaCom = 0.05 * eye3(); return b;
    };
    DynJoint j1; j1.type = JType::Revolute; j1.parent = -1; j1.Rtree = eye3();
    j1.ptree = Eigen::Vector3d(0, 0, 0); j1.axis = Eigen::Vector3d(0, 0, 1);
    j1.qLower = -kPi; j1.qUpper = kPi;
    const int b0 = chain.addBody(j1, body(2.0, Eigen::Vector3d(0, 0, 0.15)));

    DynJoint j2; j2.type = JType::Revolute; j2.parent = b0; j2.Rtree = eye3();
    j2.ptree = Eigen::Vector3d(0, 0, 0.3); j2.axis = Eigen::Vector3d(0, 1, 0);
    j2.qLower = -1.5; j2.qUpper = 1.5;
    const int b1 = chain.addBody(j2, body(1.5, Eigen::Vector3d(0.25, 0, 0)));

    DynJoint j3; j3.type = JType::Revolute; j3.parent = b1; j3.Rtree = eye3();
    j3.ptree = Eigen::Vector3d(0.5, 0, 0); j3.axis = Eigen::Vector3d(0, 1, 0);
    j3.qLower = -2.5; j3.qUpper = 2.5;
    const int b2 = chain.addBody(j3, body(1.0, Eigen::Vector3d(0.25, 0, 0)));

    caps.clear();
    caps.push_back({ b0, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0, 0, 0.3), 0.06 });
    caps.push_back({ b1, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0.5, 0, 0), 0.05 });
    caps.push_back({ b2, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0.5, 0, 0), 0.05 });
}

JointLimits armLimits() {
    JointLimits lim;
    lim.qLower.resize(3); lim.qUpper.resize(3); lim.vMax.resize(3);
    lim.qLower << -kPi, -1.5, -2.5;
    lim.qUpper <<  kPi,  1.5,  2.5;
    lim.vMax   << 2.0, 2.0, 3.0;
    return lim;
}

CollisionWorld sphereWorld(const std::vector<LinkCapsule>& caps, double sphereR) {
    CollisionWorld w; w.capsules = caps; w.tolerance = 1e-6;
    w.obstacles.push_back(Obstacle::sphere(Eigen::Vector3d(0.8, 0, 0.3), sphereR));
    w.obstacles.push_back(Obstacle::halfSpace(Eigen::Vector3d(0, 0, 1), Eigen::Vector3d(0, 0, -0.2)));
    return w;
}

Eigen::VectorXd q3(double a, double b, double c) { Eigen::VectorXd q(3); q << a, b, c; return q; }

// A time-parameterized trajectory through a waypoint path: each segment is a
// cubic ease (s = u^2(3-2u), zero velocity at the waypoints), duration sized so
// the peak per-dof speed (1.5*|dq|/T) rides at vMax * speedScale.
struct TimedTraj {
    std::vector<Eigen::VectorXd> wp;
    std::vector<double> segT, segStart;
    double total = 0.0;

    void build(const std::vector<Eigen::VectorXd>& path, const JointLimits& lim, double speedScale) {
        wp = path; segT.clear(); segStart.clear();
        double t = 0.0;
        for (size_t k = 1; k < wp.size(); ++k) {
            const Eigen::VectorXd dq = wp[k] - wp[k - 1];
            double T = 0.2;
            for (int i = 0; i < dq.size(); ++i)
                T = std::max(T, 1.5 * std::abs(dq[i]) / (lim.vMax[i] * speedScale));
            segStart.push_back(t); segT.push_back(T); t += T;
        }
        total = t;
    }
    void sample(double tq, Eigen::VectorXd& q, Eigen::VectorXd& qd, Eigen::VectorXd& qdd) const {
        if (wp.empty()) return;
        if (tq >= total || segT.empty()) { q = wp.back(); qd.setZero(q.size()); qdd.setZero(q.size()); return; }
        size_t s = 0;
        while (s + 1 < segT.size() && tq >= segStart[s] + segT[s]) ++s;
        const double T = segT[s];
        const double u = std::clamp((tq - segStart[s]) / T, 0.0, 1.0);
        const double sc = u * u * (3.0 - 2.0 * u);
        const double sd = (6.0 * u - 6.0 * u * u) / T;
        const double sdd = (6.0 - 12.0 * u) / (T * T);
        const Eigen::VectorXd dq = wp[s + 1] - wp[s];
        q = wp[s] + dq * sc; qd = dq * sd; qdd = dq * sdd;
    }
};

struct ExecResult {
    double peakErr = 0.0;                       // peak ||q_achieved - q_commanded|| during motion
    double maxVelRatio = 0.0;                   // max_i,t |qd_i|/vMax_i
    double minPosMargin = 1e30;                 // min over t,dof of bound margin
    std::vector<Eigen::VectorXd> achieved;      // achieved q(t)
};

// Execute a timed trajectory with either computed torque or the soft PD, under
// gravity, via forward dynamics + semi-implicit Euler.
ExecResult execute(const krs::dyn::SerialChain& chain, const TimedTraj& traj,
                   const JointLimits& lim, bool useComputedTorque, const Eigen::Vector3d& gravity) {
    ExecResult r;
    const int nq = chain.nq();
    const double dt = 1.0 / 240.0;
    const double Kp = 900.0, Kd = 60.0;                      // computed torque
    const double KpOld[3] = { 50.0, 50.0, 35.0 };            // soft PD: stable but no model/gravity
    const double KdOld[3] = { 12.0, 12.0, 9.0 };             // feedforward -> sags+lags (finite, large)
    Eigen::VectorXd q = traj.wp.front(), qd = Eigen::VectorXd::Zero(nq);
    const int steps = int(std::ceil(traj.total / dt)) + 1;
    Eigen::VectorXd q_des(nq), qd_des(nq), qdd_des(nq);
    for (int k = 0; k <= steps; ++k) {
        const double t = k * dt;
        traj.sample(t, q_des, qd_des, qdd_des);
        Eigen::VectorXd tau(nq);
        if (useComputedTorque)
            tau = krs::ctrl::computedTorque(chain, q, qd, q_des, qd_des, qdd_des, Kp, Kd, gravity);
        else
            for (int i = 0; i < nq; ++i) tau[i] = KpOld[i] * (q_des[i] - q[i]) - KdOld[i] * qd[i];
        const Eigen::VectorXd qdd = chain.forwardDynamics(q, qd, tau, gravity);
        qd += qdd * dt; q += qd * dt;                        // semi-implicit Euler
        r.achieved.push_back(q);
        if (k > 3) r.peakErr = std::max(r.peakErr, (q - q_des).norm());
        for (int i = 0; i < nq; ++i) {
            r.maxVelRatio = std::max(r.maxVelRatio, std::abs(qd[i]) / lim.vMax[i]);
            r.minPosMargin = std::min(r.minPosMargin, std::min(q[i] - lim.qLower[i], lim.qUpper[i] - q[i]));
        }
    }
    return r;
}

double trajMaxPen(const krs::dyn::SerialChain& chain, const CollisionWorld& w,
                  const std::vector<Eigen::VectorXd>& traj) {
    double m = 0.0;
    for (const auto& q : traj) m = std::max(m, w.maxPenetration(chain, q));
    return m;
}

} // namespace

bool runExecuteGate() {
    std::fprintf(stderr, "TRACE execute: enter\n");
    krs::dyn::SerialChain chain;
    std::vector<LinkCapsule> caps;
    buildArmDyn(chain, caps);
    const JointLimits lim = armLimits();
    const Eigen::Vector3d gravity(0, 0, -9.81);

    const double trueR = 0.25, planR = 0.35;                 // plan with a 0.10 m safety margin
    const CollisionWorld trueWorld = sphereWorld(caps, trueR);
    const CollisionWorld planWorld = sphereWorld(caps, planR);

    const Eigen::VectorXd qA = q3(-1.2, 0.0, 0.0);
    const Eigen::VectorXd qB = q3(+1.2, 0.0, 0.0);

    // Plan the collision-free path (with margin) to execute.
    MotionPlanner planner(chain, planWorld, lim);
    PlanRequest rc; rc.start = qA; rc.goal = qB; rc.seed = 7; rc.denseWaypoints = 8;
    std::fprintf(stderr, "TRACE execute: planning\n");
    const PlanResult plan = planner.plan(rc);
    std::fprintf(stderr, "TRACE execute: planned solved=%d\n", int(plan.solved));
    if (!plan.solved) { std::printf("  [execute] PLAN FAILED -- cannot run execute gate\n"); return false; }

    bool allOk = true;

    // ---- EXECUTE-TRACK -------------------------------------------------------
    {
        TimedTraj traj; traj.build(plan.waypoints, lim, 1.0);
        std::fprintf(stderr, "TRACE execute: TRACK exec CT (total=%.2fs)\n", traj.total);
        const ExecResult ct = execute(chain, traj, lim, true, gravity);
        std::fprintf(stderr, "TRACE execute: TRACK exec softPD\n");
        const ExecResult pd = execute(chain, traj, lim, false, gravity);
        const double bound = 0.10;
        std::printf("  [execute-track] computed-torque peak=%.4f rad | NEG soft-PD peak=%.4f rad "
                    "(bound %.2f; CT<bound<PD)\n", ct.peakErr, pd.peakErr, bound);
        const bool ok = ct.peakErr < bound && pd.peakErr > bound && pd.peakErr > 3.0 * ct.peakErr;
        std::printf("    -> EXECUTE-TRACK %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- EXECUTE-COLLISION-FREE ---------------------------------------------
    {
        TimedTraj traj; traj.build(plan.waypoints, lim, 1.0);
        const ExecResult ct = execute(chain, traj, lim, true, gravity);
        const double achievedPen = trajMaxPen(chain, trueWorld, ct.achieved);

        // NEG-CTRL: a straight-line reference that drives the arm THROUGH the sphere.
        std::vector<Eigen::VectorXd> line;
        for (int k = 0; k < 64; ++k) { const double a = double(k) / 63.0; line.push_back(qA + a * (qB - qA)); }
        TimedTraj badTraj; badTraj.build(line, lim, 1.0);
        const ExecResult bad = execute(chain, badTraj, lim, true, gravity);
        const double negPen = trajMaxPen(chain, trueWorld, bad.achieved);

        std::printf("  [execute-collisionfree] achieved pen=%.4f (planned w/ %.2fm margin) | "
                    "NEG colliding-ref achieved pen=%.4f\n", achievedPen, planR - trueR, negPen);
        const bool ok = achievedPen < 1e-3 && negPen > 1e-2;
        std::printf("    -> EXECUTE-COLLISION-FREE %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- EXECUTE-LIMITS ------------------------------------------------------
    {
        TimedTraj traj; traj.build(plan.waypoints, lim, 1.0);
        const ExecResult ct = execute(chain, traj, lim, true, gravity);

        // NEG-CTRL: a 3x-fast re-timing -> the achieved velocity exceeds vMax.
        TimedTraj fast; fast.build(plan.waypoints, lim, 3.0);
        const ExecResult fastRes = execute(chain, fast, lim, true, gravity);

        std::printf("  [execute-limits] posMargin=%.4f velRatio=%.4f (<=1 ok) | "
                    "NEG 3x-fast velRatio=%.4f (>1)\n",
                    ct.minPosMargin, ct.maxVelRatio, fastRes.maxVelRatio);
        const bool ok = ct.minPosMargin > -1e-3 && ct.maxVelRatio < 1.10 && fastRes.maxVelRatio > 1.10;
        std::printf("    -> EXECUTE-LIMITS %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    std::printf("  [execute gate] %s\n", allOk ? "ALL PASS" : "FAIL");
    return allOk;
}

// ===========================================================================
// Phase 5 — E2E: a robot DEFINED via the chain data model (Phase 4) is PLANNED
// (Phase 1) and EXECUTED (Phase 2). Every stage's number is asserted, and
// severing any stage localizes the break to that stage (upstream stays healthy).
// ===========================================================================
bool runE2EGate() {
    std::fprintf(stderr, "TRACE e2e: enter\n");
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[e2e] GATE E2E -- robot DEFINED via chain -> PLANNED -> EXECUTED; sever any stage localizes the break\n");
    const Eigen::Vector3d gravity(0, 0, -9.81);
    const double trueR = 0.25, planR = 0.35;
    const Eigen::VectorXd qA = q3(-1.2, 0, 0), qB = q3(1.2, 0, 0);

    const std::vector<LinkCapsule> caps = {
        { 0, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0,0,0.3), 0.06 },
        { 1, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.5,0,0), 0.05 },
        { 2, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.5,0,0), 0.05 } };
    const JointLimits lim = armLimits();
    JointLimits tight = armLimits();                     // sever PLAN: disconnected bounds
    tight.qLower[1] = -0.05; tight.qUpper[1] = 0.05; tight.qLower[2] = -0.08; tight.qUpper[2] = 0.08;
    const CollisionWorld planWorld = sphereWorld(caps, planR);
    const CollisionWorld trueWorld = sphereWorld(caps, trueR);

    // DEFINE the robot via the Phase-4 chain data model (empty -> severed DEFINE).
    auto makeRobot = [](bool empty) {
        krs::robot::Robot r; r.name = "e2ebot"; r.nLinks = 4;
        if (!empty) {
            krs::robot::Joint j1; j1.member = true; j1.axis = Eigen::Vector3d(0,0,1); j1.ptree = Eigen::Vector3d(0,0,0);   j1.qLower = -kPi; j1.qUpper = kPi;
            krs::robot::Joint j2; j2.member = true; j2.axis = Eigen::Vector3d(0,1,0); j2.ptree = Eigen::Vector3d(0,0,0.3); j2.qLower = -1.5; j2.qUpper = 1.5;
            krs::robot::Joint j3; j3.member = true; j3.axis = Eigen::Vector3d(0,1,0); j3.ptree = Eigen::Vector3d(0.5,0,0); j3.qLower = -2.5; j3.qUpper = 2.5;
            krs::robot::Joint jX; jX.member = false; jX.axis = Eigen::Vector3d(1,0,0); jX.ptree = Eigen::Vector3d(0.5,0,0);
            r.joints = { j1, j2, j3, jX };
        }
        r.mount.type = "flange"; r.mount.link = 2; r.mount.framePos = Eigen::Vector3d(0.5,0,0);
        return r;
    };

    struct Stages { int nq; bool defineH, planH, execH; double planPen, trackErr, achievedPen; };
    auto pipeline = [&](bool severDefine, bool severPlan, bool severExecute) -> Stages {
        Stages st{ 0, false, false, false, 1e30, 1e30, 1e30 };
        krs::robot::Robot r = makeRobot(severDefine);
        const krs::dyn::SerialChain chain = r.toChain();
        st.nq = chain.nq();
        st.defineH = (st.nq == 3);
        if (!st.defineH) return st;                       // DEFINE severed -> cannot proceed
        MotionPlanner planner(chain, planWorld, severPlan ? tight : lim);
        PlanRequest rq; rq.start = qA; rq.goal = qB; rq.seed = 7; rq.denseWaypoints = 8; rq.maxIterations = 20000;
        const PlanResult pr = planner.plan(rq);
        st.planPen = pr.solved ? trajMaxPen(chain, planWorld, pr.waypoints) : 1e30;
        st.planH = pr.solved && st.planPen < 1e-6;
        if (!pr.solved) return st;                        // PLAN severed -> no path downstream
        TimedTraj traj; traj.build(pr.waypoints, lim, 1.0);
        const ExecResult ex = execute(chain, traj, lim, !severExecute, gravity);  // sever EXECUTE -> soft PD
        st.trackErr = ex.peakErr;
        st.achievedPen = trajMaxPen(chain, trueWorld, ex.achieved);
        st.execH = ex.peakErr < 0.10 && st.achievedPen < 1e-3;
        return st;
    };

    const Stages full = pipeline(false, false, false);
    printf("[e2e]   PIPELINE: DEFINE nq=%d | PLAN solved pen=%.4f | EXECUTE trackErr=%.4f achievedPen=%.4f\n",
           full.nq, full.planPen, full.trackErr, full.achievedPen);
    const bool fullOk = full.defineH && full.planH && full.execH;
    printf("[e2e]   E2E-PLAN-EXECUTE (every stage's number healthy): %s\n", fullOk ? "PASS" : "FAIL");

    const Stages sD = pipeline(true, false, false);
    const Stages sP = pipeline(false, true, false);
    const Stages sE = pipeline(false, false, true);
    const bool locD = !sD.defineH;                                   // break at DEFINE
    const bool locP = sP.defineH && !sP.planH;                       // DEFINE ok, break at PLAN
    const bool locE = sE.defineH && sE.planH && !sE.execH;           // DEFINE+PLAN ok, break at EXECUTE
    printf("[e2e]   SEVER-DEFINE  -> nq=%d defineH=%d  %s\n", sD.nq, int(sD.defineH), locD ? "localized@DEFINE" : "NOT");
    printf("[e2e]   SEVER-PLAN    -> defineH=%d planH=%d  %s\n", int(sP.defineH), int(sP.planH), locP ? "localized@PLAN" : "NOT");
    printf("[e2e]   SEVER-EXECUTE -> defineH=%d planH=%d execH=%d trackErr=%.4f  %s\n",
           int(sE.defineH), int(sE.planH), int(sE.execH), sE.trackErr, locE ? "localized@EXECUTE" : "NOT");
    const bool ok = fullOk && locD && locP && locE;
    printf("[e2e] %s\n", ok ? "ALL PASS (define->plan->execute closes; severing any stage localizes the break)"
                            : "FAILURES PRESENT");
    fflush(stdout);
    return ok;
}

} // namespace krs::plan
