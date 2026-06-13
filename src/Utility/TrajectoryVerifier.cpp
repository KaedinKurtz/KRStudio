#include "TrajectoryVerifier.hpp"
#include "MpmAdjoint.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace krs::hil {

static double accelNorm(const std::vector<double>& qdd) {
    double s = 0.0; for (double a : qdd) s += a * a; return std::sqrt(s); // L2 over joints
}

std::vector<SegmentToken> TrajectoryVerifier::submit(const std::vector<TrajPoint>& traj,
                                                     const MaterialSpec& mat)
{
    std::vector<SegmentToken> out;
    if (traj.size() < 2) return out;
    const double flagThreshold = 0.75 * mat.yieldStress;          // 75% of yield
    for (size_t i = 0; i + 1 < traj.size(); ++i) {
        SegmentToken t;
        t.index = int(i); t.tStart = traj[i].t; t.tEnd = traj[i + 1].t;
        t.peakAccel = std::max(accelNorm(traj[i].qdd), accelNorm(traj[i + 1].qdd)); // segment peak |a|
        // Fast surrogate: conservative inertial stress estimate sigma ~ rho*L*a.
        t.surrogateStress = mat.density * mat.charLength * t.peakAccel * kSurrogateSafety;
        t.flagged = t.surrogateStress > flagThreshold;
        if (t.flagged) {
            // Fork the exact double-precision MpmAdjoint pass to a background
            // thread; the planner thread returns without waiting on it.
            const double a = t.peakAccel; const MaterialSpec m = mat;
            t.result = std::async(std::launch::async, [a, m]() {
                auto e = krs::mpmad::evaluatePeakStress(a, m.youngsE, m.nu, m.density, m.yieldStress);
                ExactResult r; r.maxVonMises = e.maxVonMises;
                r.verdict = e.exceeded ? Verdict::Rejected : Verdict::Safe; // exact yield decision
                return r;
            });
        } else {
            std::promise<ExactResult> p; p.set_value(ExactResult{ Verdict::Safe, 0.0 }); // immediate SAFE
            t.result = p.get_future();
        }
        out.push_back(std::move(t));
    }
    return out;
}

bool runTrajectoryHilSelfTest()
{
    using clk = std::chrono::steady_clock;
    auto ms = [](clk::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    std::fprintf(stderr, "[HIL] === TRAJECTORY_HIL_LOOP ===\n");

    // 5 s trajectory, 3 joints, 0.1 s knots. Baseline is gentle; a deliberate
    // high-acceleration transient sits in [2.0,2.4]s, a moderate (flagged but
    // survivable) bump in [3.5,3.8]s.
    std::vector<TrajPoint> traj;
    const int N = 50;
    for (int i = 0; i < N; ++i) {
        TrajPoint p; p.t = 0.1 * i; p.q = { 0.0, 0.0, 0.0 };
        double a = 12.0;                              // baseline accel per joint (norm ~21, unflagged)
        if (i >= 20 && i <= 24) a = 1500.0;           // high transient (norm ~2598, exact REJECTED)
        else if (i >= 35 && i <= 38) a = 40.0;        // moderate bump (norm ~69; flagged but exact SAFE)
        p.qdd = { a, a, a };
        traj.push_back(p);
    }
    MaterialSpec mat; // E=1e5, yield=1e5, rho=1000, L=0.25

    // Probe one exact pass to time it (for the non-blocking assertion).
    auto pr0 = clk::now();
    auto probe = krs::mpmad::evaluatePeakStress(2598.0, mat.youngsE, mat.nu, mat.density, mat.yieldStress);
    double tOne = ms(clk::now() - pr0);

    TrajectoryVerifier verifier;
    auto t0 = clk::now();
    auto tokens = verifier.submit(traj, mat);          // returns immediately
    double submitMs = ms(clk::now() - t0);

    int flagged = 0, pendingAtStart = 0;
    for (auto& tk : tokens)
        if (tk.flagged) { ++flagged; if (tk.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) ++pendingAtStart; }

    // (1) fast sweep flags the high-acceleration window (segments touching pts 20..24)
    bool flaggedTransient = true;
    for (auto& tk : tokens) {
        bool inHigh = (tk.index >= 19 && tk.index <= 24);
        if (inHigh && !tk.flagged) flaggedTransient = false;
    }
    // baseline gentle segments must NOT be flagged
    bool baselineClean = true;
    for (auto& tk : tokens)
        if (tk.index >= 0 && tk.index <= 15 && tk.flagged) baselineClean = false;

    // (2) non-blocking: submit returned well before even one exact pass could run,
    //     and at least one flagged future is still pending.
    bool nonBlocking = (submitMs < 0.5 * tOne) && (pendingAtStart > 0);

    // (3) resolve futures; high window -> REJECTED, moderate bump -> SAFE.
    bool rejectedHigh = false, safeModerate = true;
    for (auto& tk : tokens) {
        if (!tk.flagged) continue;
        ExactResult r = tk.result.get();
        if (tk.index >= 19 && tk.index <= 24 && r.verdict == Verdict::Rejected) rejectedHigh = true;
        if (tk.index >= 34 && tk.index <= 38 && r.verdict != Verdict::Safe) safeModerate = false;
        std::fprintf(stderr, "[HIL]   seg %2d  t=[%.1f,%.1f]  peakAccel=%.0f  surrogate=%.3e  -> %s (vм=%.3e)\n",
                     tk.index, tk.tStart, tk.tEnd, tk.peakAccel, tk.surrogateStress,
                     r.verdict == Verdict::Rejected ? "REJECTED" : "SAFE", r.maxVonMises);
    }

    bool pass = flaggedTransient && baselineClean && nonBlocking && rejectedHigh && safeModerate;
    std::fprintf(stderr,
        "[HIL] TRAJECTORY_HIL_LOOP %s  flagged=%d pendingAtStart=%d submit=%.3fms oneExact=%.1fms\n",
        pass ? "PASS" : "FAIL", flagged, pendingAtStart, submitMs, tOne);
    std::fprintf(stderr,
        "[HIL]   flagTransient=%d baselineClean=%d nonBlocking=%d rejectedHigh=%d safeModerate=%d probeVM=%.3e\n",
        flaggedTransient, baselineClean, nonBlocking, rejectedHigh, safeModerate, probe.maxVonMises);
    return pass;
}

} // namespace krs::hil
