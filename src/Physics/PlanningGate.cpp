// ===========================================================================
// OMPL sprint, Phase 1 — PLAN gates (env KRS_PLANNING_SELFTEST; in the overnight
// bench). Each sub-gate reports a measured number and carries a non-vacuous
// negative control. The planner plans over a real krs::dyn::SerialChain config
// space; the gate re-checks every result INDEPENDENTLY against the analytic
// collision world (it never trusts OMPL's own checker), and shows the naive
// straight-line path FAILS that same independent check.
//
//   PLAN-COLLISION-FREE : RRTConnect & RRTstar dense paths pen==0; straight-line
//                         neg-ctrl penetrates the sphere (~0.30 m); RRT* <= RRTConnect length.
//   PLAN-LIMITS         : every waypoint in [qLower,qUpper]; vMax respected.
//                         neg-ctrls: out-of-range waypoint flagged; 2x-fast parameterization flagged.
//   PLAN-CONNECTIVITY   : solved first==start, last==goal; boxed-in (disconnected)
//                         scenario returns FAILURE, not a fabricated path.
//   PLAN-DETERMINISM    : same seed -> identical waypoints; different seed -> differ.
// ===========================================================================
#include "MotionPlanner.hpp"

#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

namespace krs::plan {

namespace {

constexpr double kPi = 3.14159265358979323846;

Eigen::Matrix3d eye3() { return Eigen::Matrix3d::Identity(); }

// 3R arm: J1 yaw(Z) at origin, J2 pitch(Y) at shoulder +0.3 Z, J3 pitch(Y) after
// a 0.5 m upper arm. Capsules: 0.3 m base post (body0), 0.5 m upper arm (body1),
// 0.5 m fore-arm (body2). At q=0 the arm points +X at height 0.3, reaching x=1.0.
void buildArm(krs::dyn::SerialChain& chain, std::vector<LinkCapsule>& caps) {
    using namespace krs::dyn;
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye3();

    DynJoint j1; j1.type = JType::Revolute; j1.parent = -1; j1.Rtree = eye3();
    j1.ptree = Eigen::Vector3d(0, 0, 0); j1.axis = Eigen::Vector3d(0, 0, 1);
    j1.qLower = -kPi; j1.qUpper = kPi;
    const int b0 = chain.addBody(j1, body);

    DynJoint j2; j2.type = JType::Revolute; j2.parent = b0; j2.Rtree = eye3();
    j2.ptree = Eigen::Vector3d(0, 0, 0.3); j2.axis = Eigen::Vector3d(0, 1, 0);
    j2.qLower = -1.5; j2.qUpper = 1.5;
    const int b1 = chain.addBody(j2, body);

    DynJoint j3; j3.type = JType::Revolute; j3.parent = b1; j3.Rtree = eye3();
    j3.ptree = Eigen::Vector3d(0.5, 0, 0); j3.axis = Eigen::Vector3d(0, 1, 0);
    j3.qLower = -2.5; j3.qUpper = 2.5;
    const int b2 = chain.addBody(j3, body);

    caps.clear();
    caps.push_back({ b0, Eigen::Vector3d(0, 0, 0),   Eigen::Vector3d(0, 0, 0.3), 0.06 });  // base post
    caps.push_back({ b1, Eigen::Vector3d(0, 0, 0),   Eigen::Vector3d(0.5, 0, 0), 0.05 });  // upper arm
    caps.push_back({ b2, Eigen::Vector3d(0, 0, 0),   Eigen::Vector3d(0.5, 0, 0), 0.05 });  // fore arm
}

JointLimits armLimits(double pitchLo = -1.5, double pitchHi = 1.5) {
    JointLimits lim;
    lim.qLower.resize(3); lim.qUpper.resize(3); lim.vMax.resize(3);
    lim.qLower << -kPi, pitchLo, std::max(-2.5, pitchLo * 1.6);
    lim.qUpper <<  kPi, pitchHi, std::min( 2.5, pitchHi * 1.6);
    lim.vMax   << 2.0, 2.0, 3.0;
    return lim;
}

// Open scene: a sphere the arm must route around + a floor halfspace.
CollisionWorld openWorld(const std::vector<LinkCapsule>& caps) {
    CollisionWorld w; w.capsules = caps; w.tolerance = 1e-6;
    w.obstacles.push_back(Obstacle::sphere(Eigen::Vector3d(0.8, 0, 0.3), 0.25));
    w.obstacles.push_back(Obstacle::halfSpace(Eigen::Vector3d(0, 0, 1), Eigen::Vector3d(0, 0, -0.2)));
    return w;
}

Eigen::VectorXd q3(double a, double b, double c) { Eigen::VectorXd q(3); q << a, b, c; return q; }

// Densely sample the straight line start->goal (the naive "planner" that ignores
// collision) for the independent collision check.
std::vector<Eigen::VectorXd> straightLine(const Eigen::VectorXd& a, const Eigen::VectorXd& b, int n) {
    std::vector<Eigen::VectorXd> p; p.reserve(n);
    for (int k = 0; k < n; ++k) { const double t = double(k) / (n - 1); p.push_back(a + t * (b - a)); }
    return p;
}

// Max penetration of a path under the world (the INDEPENDENT re-check).
double pathMaxPen(const krs::dyn::SerialChain& chain, const CollisionWorld& w,
                  const std::vector<Eigen::VectorXd>& path) {
    double m = 0.0;
    for (const auto& q : path) m = std::max(m, w.maxPenetration(chain, q));
    return m;
}

} // namespace

bool runPlanningGate() {
    std::fprintf(stderr, "TRACE planning: enter\n");
    krs::dyn::SerialChain chain;
    std::vector<LinkCapsule> caps;
    buildArm(chain, caps);
    const CollisionWorld world = openWorld(caps);
    const JointLimits lim = armLimits();

    const Eigen::VectorXd qA = q3(-1.2, 0.0, 0.0);
    const Eigen::VectorXd qB = q3(+1.2, 0.0, 0.0);

    // Sanity: endpoints must be collision-free and in-bounds (else the gate is vacuous).
    const double penA = world.maxPenetration(chain, qA), penB = world.maxPenetration(chain, qB);
    std::printf("  [plan-setup] endpoint penetration  start=%.6f  goal=%.6f  (both must be 0)\n", penA, penB);
    const bool endpointsOk = penA < 1e-6 && penB < 1e-6;

    MotionPlanner planner(chain, world, lim);

    bool allOk = endpointsOk;

    // ---- PLAN-COLLISION-FREE -------------------------------------------------
    {
        PlanRequest rc; rc.start = qA; rc.goal = qB; rc.kind = PlannerKind::RRTConnect;
        rc.seed = 7; rc.maxIterations = 30000; rc.denseWaypoints = 256;
        std::fprintf(stderr, "TRACE cf: RRTConnect start\n");
        const PlanResult rrt = planner.plan(rc);
        std::fprintf(stderr, "TRACE cf: RRTConnect done solved=%d iters=%u t=%.2fs\n",
                     int(rrt.solved), rrt.iterations, rrt.planTimeSec);

        PlanRequest rs = rc; rs.kind = PlannerKind::RRTstar; rs.maxIterations = 8000;
        std::fprintf(stderr, "TRACE cf: RRTstar start\n");
        const PlanResult star = planner.plan(rs);
        std::fprintf(stderr, "TRACE cf: RRTstar done solved=%d iters=%u t=%.2fs\n",
                     int(star.solved), star.iterations, star.planTimeSec);

        const double penConnect = rrt.solved  ? pathMaxPen(chain, world, rrt.waypoints)  : 1e30;
        const double penStar    = star.solved ? pathMaxPen(chain, world, star.waypoints) : 1e30;
        const std::vector<Eigen::VectorXd> naive = straightLine(qA, qB, 256);
        const double penNaive = pathMaxPen(chain, world, naive);

        std::printf("  [plan-collisionfree] RRTConnect solved=%d pen=%.6f len=%.4f t=%.3fs | "
                    "RRTstar solved=%d pen=%.6f len=%.4f | NEG straight-line pen=%.4f\n",
                    int(rrt.solved), penConnect, rrt.pathLength, rrt.planTimeSec,
                    int(star.solved), penStar, star.pathLength, penNaive);

        const bool ok = rrt.solved && star.solved
                     && penConnect < 1e-6 && penStar < 1e-6           // planned paths collision-free
                     && penNaive > 1e-3                               // independent checker FAILS the naive path
                     && star.pathLength <= rrt.pathLength * 1.25;     // optimizing planner no worse (loose)
        std::printf("    -> PLAN-COLLISION-FREE %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- PLAN-LIMITS ---------------------------------------------------------
    {
        PlanRequest rc; rc.start = qA; rc.goal = qB; rc.seed = 7;
        const PlanResult rrt = planner.plan(rc);
        bool ok = rrt.solved;
        if (rrt.solved) {
            const double margin = positionMargin(rrt.waypoints, lim);
            const std::vector<double> times = timeParameterize(rrt.waypoints, lim, 1.0);
            const double vr = velocityRatio(rrt.waypoints, times, lim);

            // neg-ctrl A: an out-of-range waypoint -> position margin goes negative.
            std::vector<Eigen::VectorXd> bad = rrt.waypoints;
            bad.push_back(lim.qUpper + Eigen::VectorXd::Constant(3, 0.5));
            const double marginBad = positionMargin(bad, lim);
            // neg-ctrl B: a 2x-fast re-parameterization -> velocity ratio > 1.
            const std::vector<double> fast = timeParameterize(rrt.waypoints, lim, 2.0);
            const double vrFast = velocityRatio(rrt.waypoints, fast, lim);

            std::printf("  [plan-limits] posMargin=%.4f (>=0 ok)  velRatio=%.4f (<=1 ok) | "
                        "NEG out-of-range margin=%.4f  NEG 2x-fast velRatio=%.4f\n",
                        margin, vr, marginBad, vrFast);
            ok = margin >= -1e-9 && vr <= 1.0 + 1e-6 && marginBad < 0.0 && vrFast > 1.0 + 1e-6;
        }
        std::printf("    -> PLAN-LIMITS %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- PLAN-CONNECTIVITY ---------------------------------------------------
    {
        PlanRequest rc; rc.start = qA; rc.goal = qB; rc.seed = 7;
        const PlanResult rrt = planner.plan(rc);
        double startErr = 1e30, goalErr = 1e30;
        if (rrt.solved && !rrt.waypoints.empty()) {
            startErr = (rrt.waypoints.front() - qA).norm();
            goalErr  = (rrt.waypoints.back()  - qB).norm();
        }

        // NEG-CTRL: tighten the pitch dofs so the yaw=0 slice is fully blocked and
        // the long way is severed at the +/-pi yaw limit -> qA,qB are in disconnected
        // c-space components -> the planner MUST return FAILURE (not a fake path).
        const JointLimits tight = armLimits(-0.05, 0.05);
        MotionPlanner walled(chain, world, tight);
        PlanRequest rw; rw.start = qA; rw.goal = qB; rw.seed = 7; rw.maxIterations = 20000;
        const PlanResult blocked = walled.plan(rw);

        std::printf("  [plan-connectivity] solved=%d startErr=%.2e goalErr=%.2e | "
                    "NEG boxed-in solved=%d iters=%u (must be 0/FAILURE)\n",
                    int(rrt.solved), startErr, goalErr, int(blocked.solved), blocked.iterations);
        const bool ok = rrt.solved && startErr < 1e-6 && goalErr < 1e-6
                     && !blocked.solved && blocked.waypoints.empty();
        std::printf("    -> PLAN-CONNECTIVITY %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- PLAN-DETERMINISM ----------------------------------------------------
    {
        PlanRequest r1; r1.start = qA; r1.goal = qB; r1.seed = 42;
        const PlanResult a = planner.plan(r1);
        const PlanResult b = planner.plan(r1);            // same seed
        PlanRequest r2 = r1; r2.seed = 99;
        const PlanResult c = planner.plan(r2);            // different seed

        auto maxDiff = [](const PlanResult& x, const PlanResult& y) -> double {
            if (!x.solved || !y.solved) return 1e30;
            if (x.waypoints.size() != y.waypoints.size()) return 1e30;
            double m = 0.0;
            for (size_t k = 0; k < x.waypoints.size(); ++k)
                m = std::max(m, (x.waypoints[k] - y.waypoints[k]).cwiseAbs().maxCoeff());
            return m;
        };
        const double sameSeed = maxDiff(a, b);
        const double diffSeed = maxDiff(a, c);
        std::printf("  [plan-determinism] same-seed maxdiff=%.2e (=0 ok) | diff-seed maxdiff=%.2e (>0 ok)\n",
                    sameSeed, diffSeed);
        const bool ok = a.solved && b.solved && c.solved
                     && sameSeed < 1e-9 && diffSeed > 1e-6;
        std::printf("    -> PLAN-DETERMINISM %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- PROFILE: plan time vs scene complexity -----------------------------
    {
        for (int nObs : { 1, 4, 16 }) {
            CollisionWorld w; w.capsules = caps; w.tolerance = 1e-6;
            w.obstacles.push_back(Obstacle::sphere(Eigen::Vector3d(0.8, 0, 0.3), 0.25));
            w.obstacles.push_back(Obstacle::halfSpace(Eigen::Vector3d(0, 0, 1), Eigen::Vector3d(0, 0, -0.2)));
            // extra spheres placed BELOW the floor-cleared workspace, off the corridor
            for (int e = 1; e < nObs; ++e) {
                const double ang = 0.7 * e;
                w.obstacles.push_back(Obstacle::sphere(
                    Eigen::Vector3d(0.5 * std::cos(ang) - 0.9, 0.5 * std::sin(ang), 1.2), 0.12));
            }
            MotionPlanner p(chain, w, lim);
            PlanRequest rc; rc.start = qA; rc.goal = qB; rc.seed = 7;
            const PlanResult r = p.plan(rc);
            std::printf("  [plan-profile] obstacles=%2d solved=%d t=%.3fs iters=%u len=%.4f\n",
                        nObs, int(r.solved), r.planTimeSec, r.iterations, r.pathLength);
        }
    }

    // ---- FUZZ: random valid endpoint pairs -> every solved path collision-free
    {
        std::mt19937 rng(12345u);
        auto sampleValid = [&](Eigen::VectorXd& q) -> bool {
            std::uniform_real_distribution<double> d0(lim.qLower[0], lim.qUpper[0]);
            std::uniform_real_distribution<double> d1(lim.qLower[1], lim.qUpper[1]);
            std::uniform_real_distribution<double> d2(lim.qLower[2], lim.qUpper[2]);
            for (int tries = 0; tries < 64; ++tries) {
                q = q3(d0(rng), d1(rng), d2(rng));
                if (world.maxPenetration(chain, q) < 1e-6) return true;
            }
            return false;
        };
        int solved = 0, total = 0; double worstPen = 0.0;
        for (int t = 0; t < 8; ++t) {
            Eigen::VectorXd s, g;
            if (!sampleValid(s) || !sampleValid(g)) continue;
            ++total;
            PlanRequest rc; rc.start = s; rc.goal = g; rc.seed = 100u + t; rc.maxIterations = 12000;
            const PlanResult r = planner.plan(rc);
            if (r.solved) { ++solved; worstPen = std::max(worstPen, pathMaxPen(chain, world, r.waypoints)); }
        }
        std::printf("  [plan-fuzz] solved %d/%d random pairs; worst path penetration=%.6f (must be 0)\n",
                    solved, total, worstPen);
        const bool ok = worstPen < 1e-6;     // no solved path may collide
        std::printf("    -> PLAN-FUZZ %s\n", ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    std::printf("  [planning gate] %s\n", allOk ? "ALL PASS" : "FAIL");
    return allOk;
}

} // namespace krs::plan
