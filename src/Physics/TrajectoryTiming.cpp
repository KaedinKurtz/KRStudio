// ===========================================================================
// krs::traj -- trapezoidal joint-space time-parameterization (impl + GATE).
//
// See TrajectoryTiming.hpp for the method. Summary of the per-segment math:
//   A segment moves along the chord q(s) = q0 + s*d, s in [0,1], d = q1-q0.
//   Joint i traces q_i(t) = q0_i + s(t)*d_i, so |q̇_i| = |d_i|*ṡ, |q̈_i| = |d_i|*s̈
//   (chord is linear in s -> no s̈-independent centripetal term). Thus the whole
//   segment's scalar limits are
//       ṡ_max = min_i vMax_i / |d_i|            (fastest-limited joint caps speed)
//       s̈_max = min_i aMax_i / |d_i|            (             "        caps accel)
//   over dofs with |d_i|>0. A trapezoid on s(t) from 0->1 with these caps is then
//   feasible for every joint simultaneously. Segments are joined through a full
//   stop (ṡ=0 at each waypoint), so accel stays bounded across junctions.
// ===========================================================================
#include "TrajectoryTiming.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <limits>

namespace krs::traj {

namespace {

// Trapezoidal (or triangular) time profile of a scalar path parameter s: 0 -> 1,
// with a peak rate vCap and acceleration aCap. If the cruise phase vanishes the
// profile degenerates to a symmetric triangle (accelerate half, decelerate half).
struct SegProfile {
    double ta = 0.0;    // accel phase duration  [0, ta)
    double tc = 0.0;    // cruise phase duration  [ta, ta+tc)
    double T  = 0.0;    // total segment time     (= 2*ta + tc)
    double v  = 0.0;    // achieved peak rate ṡ
    double a  = 0.0;    // accel magnitude s̈ during ramps (0 for a zero-length seg)

    // s, ṡ, s̈ at local time u in [0,T].
    void eval(double u, double& s, double& sd, double& sdd) const {
        if (T <= 0.0) { s = 0.0; sd = 0.0; sdd = 0.0; return; }
        u = std::clamp(u, 0.0, T);
        if (u < ta) {                       // accelerate
            s   = 0.5 * a * u * u;
            sd  = a * u;
            sdd = a;
        } else if (u < ta + tc) {           // cruise
            const double uc = u - ta;
            s   = 0.5 * a * ta * ta + v * uc;
            sd  = v;
            sdd = 0.0;
        } else {                            // decelerate
            const double ud = u - ta - tc;
            const double sAtDecel = 0.5 * a * ta * ta + v * tc;
            s   = sAtDecel + v * ud - 0.5 * a * ud * ud;
            sd  = v - a * ud;
            sdd = -a;
        }
        s = std::clamp(s, 0.0, 1.0);
    }
};

// Build the s:0->1 trapezoid from the segment's scalar caps.
//   vCap = ṡ_max, aCap = s̈_max  (both > 0).
// Distance in s is exactly 1. Triangular when the peak the accel can build over
// the ramp-up/ramp-down never reaches vCap (short move); trapezoidal otherwise.
SegProfile buildProfile(double vCap, double aCap) {
    SegProfile p;
    p.a = aCap;
    // Distance covered ramping 0->vCap and back down = vCap^2 / aCap. If that
    // already exceeds 1, we never reach vCap -> triangle peaking at vPeak<vCap.
    const double sRamp = (vCap * vCap) / aCap;   // aCap>0 guaranteed by caller
    if (sRamp >= 1.0) {
        // Triangular: 2 * (½ a ta²) = 1  ->  ta = sqrt(1/aCap); vPeak = a*ta.
        p.ta = std::sqrt(1.0 / aCap);
        p.tc = 0.0;
        p.v  = aCap * p.ta;
        p.T  = 2.0 * p.ta;
    } else {
        // Trapezoid: ramp time to vCap, cruise covers the remaining s-distance.
        p.ta = vCap / aCap;
        p.v  = vCap;
        const double sCruise = 1.0 - sRamp;      // > 0 here
        p.tc = sCruise / vCap;
        p.T  = 2.0 * p.ta + p.tc;
    }
    return p;
}

} // namespace

TimedTrajectory timeParameterize(const std::vector<Eigen::VectorXd>& waypoints,
                                 const TimingLimits& limits,
                                 double dt) {
    TimedTrajectory out;

    // ---- degenerate / invalid input guards (no throw, no div0) --------------
    if (waypoints.empty()) return out;                 // ok stays false
    const int nq = int(waypoints.front().size());
    if (limits.vMax.size() != nq || limits.aMax.size() != nq) return out;
    if (dt <= 0.0) dt = 1e-3;                           // clamp a nonsense clock

    // Single waypoint (or all-duplicate): a stationary 1-sample trajectory.
    // (Handled generically below once segments collapse, but short-circuit here
    //  so a lone waypoint always yields a valid ok=true result.)

    // ---- build per-segment profiles -----------------------------------------
    // Skip zero-motion segments; a moving dof with vMax<=0 or aMax<=0 is infeasible.
    struct Seg { SegProfile prof; const Eigen::VectorXd* q0; const Eigen::VectorXd* q1; };
    std::vector<Seg> segs;
    segs.reserve(waypoints.size());

    for (size_t k = 1; k < waypoints.size(); ++k) {
        const Eigen::VectorXd& a = waypoints[k - 1];
        const Eigen::VectorXd& b = waypoints[k];
        if (a.size() != nq || b.size() != nq) return out;   // ragged input
        const Eigen::VectorXd d = b - a;

        double vCap = std::numeric_limits<double>::infinity();
        double aCap = std::numeric_limits<double>::infinity();
        bool moves = false;
        for (int i = 0; i < nq; ++i) {
            const double di = std::abs(d[i]);
            if (di <= 1e-12) continue;                        // this dof is stationary
            moves = true;
            const double vi = limits.vMax[i], ai = limits.aMax[i];
            if (vi <= 0.0 || ai <= 0.0) return out;           // NEG-CTRL: infeasible -> ok=false
            vCap = std::min(vCap, vi / di);
            aCap = std::min(aCap, ai / di);
        }
        if (!moves) continue;                                 // duplicate waypoint -> zero time

        Seg s;
        s.prof = buildProfile(vCap, aCap);
        s.q0 = &waypoints[k - 1];
        s.q1 = &waypoints[k];
        segs.push_back(s);
    }

    // No motion anywhere (single waypoint or all duplicates): 1 stationary sample.
    if (segs.empty()) {
        out.t.push_back(0.0);
        out.q.push_back(waypoints.back());
        out.qd.push_back(Eigen::VectorXd::Zero(nq));
        out.qdd.push_back(Eigen::VectorXd::Zero(nq));
        out.duration = 0.0;
        out.ok = true;
        return out;
    }

    // ---- global segment start times -----------------------------------------
    std::vector<double> segStart(segs.size(), 0.0);
    double total = 0.0;
    for (size_t i = 0; i < segs.size(); ++i) { segStart[i] = total; total += segs[i].prof.T; }
    out.duration = total;

    // ---- sample on the fixed dt clock ---------------------------------------
    // For each sample time τ locate its segment, evaluate s,ṡ,s̈, and map to
    // joint space through the chord: q = q0 + s*d, q̇ = ṡ*d, q̈ = s̈*d.
    auto sampleAt = [&](double tau, Eigen::VectorXd& q, Eigen::VectorXd& qd, Eigen::VectorXd& qdd) {
        tau = std::clamp(tau, 0.0, total);
        // find segment: last seg whose start <= tau
        size_t si = segs.size() - 1;
        for (size_t i = 0; i < segs.size(); ++i) {
            const double segEnd = segStart[i] + segs[i].prof.T;
            if (tau <= segEnd + 1e-12) { si = i; break; }
        }
        const Seg& sg = segs[si];
        const double u = tau - segStart[si];
        double s, sd, sdd;
        sg.prof.eval(u, s, sd, sdd);
        const Eigen::VectorXd d = *sg.q1 - *sg.q0;
        q   = *sg.q0 + s * d;
        qd  = sd * d;
        qdd = sdd * d;
    };

    const int nSteps = std::max(1, int(std::ceil(total / dt)));
    out.t.reserve(nSteps + 1);
    out.q.reserve(nSteps + 1);
    out.qd.reserve(nSteps + 1);
    out.qdd.reserve(nSteps + 1);

    for (int k = 0; k <= nSteps; ++k) {
        double tau = k * dt;
        bool last = false;
        if (tau >= total) { tau = total; last = true; }       // final sample lands on duration
        Eigen::VectorXd q, qd, qdd;
        sampleAt(tau, q, qd, qdd);
        out.t.push_back(tau);
        out.q.push_back(q);
        out.qd.push_back(qd);
        out.qdd.push_back(qdd);
        if (last) break;
    }

    // Pin the final sample EXACTLY to the last waypoint (kill fp drift from the
    // trapezoid integral so "reaches the goal" is bit-exact) and zero end vel/acc.
    if (!out.q.empty()) {
        out.q.back()   = waypoints.back();
        out.qd.back()  = Eigen::VectorXd::Zero(nq);
        out.qdd.back() = Eigen::VectorXd::Zero(nq);
        out.t.back()   = total;
    }

    out.ok = true;
    return out;
}

// ===========================================================================
// GATE  (KRS_TRAJECTORY_SELFTEST -> runTrajectoryTimingGate -> _Exit)
// ===========================================================================
namespace {

// Max per-joint |velocity| and |acceleration| measured by finite-differencing the
// SAMPLED q (independent of the reported qd/qdd -- so the check does not trust the
// profile's own bookkeeping, only the positions the controller would actually see).
void fdLimits(const TimedTrajectory& tr, Eigen::VectorXd& vPeak, Eigen::VectorXd& aPeak) {
    const int nq = int(tr.q.front().size());
    vPeak = Eigen::VectorXd::Zero(nq);
    aPeak = Eigen::VectorXd::Zero(nq);
    std::vector<Eigen::VectorXd> vel;
    for (size_t k = 1; k < tr.q.size(); ++k) {
        const double h = tr.t[k] - tr.t[k - 1];
        if (h <= 1e-12) { vel.push_back(Eigen::VectorXd::Zero(nq)); continue; }
        const Eigen::VectorXd v = (tr.q[k] - tr.q[k - 1]) / h;
        vel.push_back(v);
        vPeak = vPeak.cwiseMax(v.cwiseAbs());
    }
    for (size_t k = 1; k < vel.size(); ++k) {
        const double h = tr.t[k + 1] - tr.t[k];               // interval of the later velocity sample
        if (h <= 1e-12) continue;
        const Eigen::VectorXd acc = (vel[k] - vel[k - 1]) / h;
        aPeak = aPeak.cwiseMax(acc.cwiseAbs());
    }
}

TimingLimits mkLimits(const Eigen::VectorXd& v, const Eigen::VectorXd& a) {
    TimingLimits L; L.vMax = v; L.aMax = a; return L;
}

Eigen::VectorXd vec3(double a, double b, double c) { Eigen::VectorXd v(3); v << a, b, c; return v; }

} // namespace

bool runTrajectoryTimingGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[traj] GATE TRAJECTORY -- trapezoidal joint-space time-scaling respects vMax/aMax, reaches goal, monotone in limits\n");
    bool pass = true;

    const int nq = 3;
    const Eigen::VectorXd vMax = vec3(1.0, 2.0, 1.5);
    const Eigen::VectorXd aMax = vec3(2.0, 4.0, 3.0);
    const TimingLimits lim = mkLimits(vMax, aMax);
    const double dt = 0.005;

    // A 3-waypoint joint path (two segments, direction change at the middle).
    std::vector<Eigen::VectorXd> wp = {
        vec3(0.0, 0.0, 0.0),
        vec3(1.2, -0.8, 0.5),
        vec3(0.3,  0.4, 1.1),
    };

    // ---- LIMIT-RESPECT: finite-diff'd v & a never exceed the box -------------
    {
        const TimedTrajectory tr = timeParameterize(wp, lim, dt);
        Eigen::VectorXd vPk, aPk;
        bool ok = tr.ok && tr.q.size() >= 2;
        if (ok) {
            fdLimits(tr, vPk, aPk);
            // small numerical slack: forward-difference of a trapezoid slightly
            // overshoots the analytic bound at the ramp corners (O(dt) sampling).
            const double vSlack = 1.02, aSlack = 1.15;
            bool vOk = true, aOk = true;
            for (int i = 0; i < nq; ++i) {
                if (vPk[i] > vMax[i] * vSlack) vOk = false;
                if (aPk[i] > aMax[i] * aSlack) aOk = false;
            }
            printf("[traj]   duration=%.4fs samples=%zu\n", tr.duration, tr.q.size());
            printf("[traj]   vPeak=(%.3f,%.3f,%.3f) vMax=(%.2f,%.2f,%.2f)  within=%s  %s\n",
                   vPk[0], vPk[1], vPk[2], vMax[0], vMax[1], vMax[2], vOk ? "yes" : "NO", vOk ? "PASS" : "FAIL");
            printf("[traj]   aPeak=(%.3f,%.3f,%.3f) aMax=(%.2f,%.2f,%.2f)  within=%s  %s\n",
                   aPk[0], aPk[1], aPk[2], aMax[0], aMax[1], aMax[2], aOk ? "yes" : "NO", aOk ? "PASS" : "FAIL");
            ok = vOk && aOk;
        } else {
            printf("[traj]   trajectory build FAILED  NO  FAIL\n");
        }
        pass &= ok;
    }

    // ---- REACHES-GOAL: last sample == final waypoint; first == first ---------
    {
        const TimedTrajectory tr = timeParameterize(wp, lim, dt);
        bool ok = tr.ok && !tr.q.empty();
        double startErr = 1e30, goalErr = 1e30;
        if (ok) {
            startErr = (tr.q.front() - wp.front()).norm();
            goalErr  = (tr.q.back()  - wp.back()).norm();
            ok = startErr < 1e-9 && goalErr < 1e-9;
        }
        printf("[traj]   startErr=%.2e goalErr=%.2e reaches-goal=%s  %s\n",
               startErr, goalErr, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // ---- MONOTONE: raising the limits strictly shortens the duration ---------
    // Scale the whole box by 1x, 2x, 4x -- duration must strictly decrease.
    {
        const TimedTrajectory t1 = timeParameterize(wp, mkLimits(vMax * 1.0, aMax * 1.0), dt);
        const TimedTrajectory t2 = timeParameterize(wp, mkLimits(vMax * 2.0, aMax * 2.0), dt);
        const TimedTrajectory t4 = timeParameterize(wp, mkLimits(vMax * 4.0, aMax * 4.0), dt);
        const bool built = t1.ok && t2.ok && t4.ok;
        const bool mono = built && t2.duration < t1.duration - 1e-6
                                && t4.duration < t2.duration - 1e-6;
        printf("[traj]   duration 1x=%.4f 2x=%.4f 4x=%.4f  strictly-decreasing=%s  %s\n",
               t1.duration, t2.duration, t4.duration, mono ? "yes" : "NO", mono ? "PASS" : "FAIL");
        pass &= mono;
    }

    // ---- PROFILE-SHAPE: a lone 2-waypoint move has a trapezoid/triangle speed -
    // The scalar speed |q̇| must rise from ~0, reach a plateau/peak, and fall back
    // to ~0 -- i.e. exactly one local maximum, with distinct rising & falling runs.
    {
        std::vector<Eigen::VectorXd> two = { vec3(0, 0, 0), vec3(2.0, 0, 0) };
        // long move so a cruise plateau exists (trapezoid, not a bare triangle).
        const TimedTrajectory tr = timeParameterize(two, mkLimits(vec3(0.5, 1, 1), vec3(1.0, 1, 1)), dt);
        bool ok = tr.ok && tr.q.size() > 6;
        int nUp = 0, nDown = 0, peakIdx = -1; double peak = -1.0;
        if (ok) {
            std::vector<double> spd(tr.q.size(), 0.0);
            for (size_t k = 1; k < tr.q.size(); ++k) {
                const double h = tr.t[k] - tr.t[k - 1];
                spd[k] = (h > 1e-12) ? (tr.q[k] - tr.q[k - 1]).norm() / h : 0.0;
                if (spd[k] > peak) { peak = spd[k]; peakIdx = int(k); }
            }
            // count sign changes of the speed derivative over the interior.
            for (size_t k = 2; k + 1 < spd.size(); ++k) {
                if (spd[k] > spd[k - 1] + 1e-9) ++nUp;
                if (spd[k] < spd[k - 1] - 1e-9) ++nDown;
            }
            // starts near rest, ends near rest, has a real hump in the middle.
            const bool startsSlow = spd[1] < peak * 0.6;
            const bool endsSlow   = spd[spd.size() - 1] < peak * 0.6;
            const bool humped     = nUp > 0 && nDown > 0 && peakIdx > 1 && peakIdx < int(spd.size()) - 1;
            printf("[traj]   speed profile: peak=%.3f up-samples=%d down-samples=%d startsSlow=%d endsSlow=%d\n",
                   peak, nUp, nDown, int(startsSlow), int(endsSlow));
            ok = startsSlow && endsSlow && humped;
        }
        printf("[traj]   trapezoidal-shape=%s  %s\n", ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // ---- NEG-CTRL: zero vMax on a MOVING dof -> rejected, no div0 -------------
    {
        const Eigen::VectorXd vZero = vec3(0.0, 2.0, 1.5);    // joint 0 (which moves) has vMax=0
        const TimedTrajectory tr = timeParameterize(wp, mkLimits(vZero, aMax), dt);
        const bool rejected = !tr.ok && tr.q.empty();
        // Contrast: the SAME zero-vMax dof is fine if that dof never moves.
        std::vector<Eigen::VectorXd> stillJoint0 = { vec3(0.0, 0.0, 0.0), vec3(0.0, 0.5, 0.3) };
        const TimedTrajectory ok2 = timeParameterize(stillJoint0, mkLimits(vZero, aMax), dt);
        const bool acceptsWhenIdle = ok2.ok;
        printf("[traj]   zero-vMax(moving)=rejected(%d)  zero-vMax(idle-dof)=accepted(%d)  %s\n",
               int(rejected), int(acceptsWhenIdle),
               (rejected && acceptsWhenIdle) ? "PASS" : "FAIL");
        pass &= rejected && acceptsWhenIdle;
    }

    // ---- DEGENERATE: single waypoint -> 1 stationary sample, ok=true ----------
    {
        std::vector<Eigen::VectorXd> one = { vec3(0.4, -0.2, 0.9) };
        const TimedTrajectory tr = timeParameterize(one, lim, dt);
        const bool ok = tr.ok && tr.q.size() == 1 && tr.duration == 0.0
                        && (tr.q.front() - one.front()).norm() < 1e-12;
        printf("[traj]   single-waypoint stationary=%s  %s\n", ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    printf("[traj] %s\n", pass ? "ALL PASS (trapezoidal timing feasible + monotone)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::traj
