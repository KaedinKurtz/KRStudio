#pragma once

#include <future>
#include <vector>

/**
 * @brief Asynchronous multi-fidelity trajectory validation. A planner injects a
 * time-parametric joint trajectory; a fast analytic surrogate sweep flags
 * segments whose predicted internal stress approaches the material yield, and
 * only those flagged segments are forked to the heavy double-precision
 * MpmAdjoint backend (`evaluatePeakStress`) on background threads. The planner
 * gets pending futures immediately and is never blocked by the exact math.
 */
namespace krs::hil {

/// One sampled trajectory knot: time, joint positions, joint accelerations.
struct TrajPoint {
    double t = 0.0;
    std::vector<double> q;    // joint positions (rad / m)
    std::vector<double> qdd;  // joint accelerations (rad/s^2 / m/s^2)
};

/// Material the trajectory is evaluated against.
struct MaterialSpec {
    double youngsE = 1.0e5;     // Pa
    double nu = 0.3;            // -
    double density = 1000.0;    // kg/m^3
    double yieldStress = 1.0e5; // Pa (von Mises yield)
    double charLength = 0.25;   // m (characteristic load length for the surrogate)
};

enum class Verdict { Safe, Rejected };

/// Resolved result of the exact pass (also returned immediately for unflagged).
struct ExactResult {
    Verdict verdict = Verdict::Safe;
    double maxVonMises = 0.0;   // Pa, from the exact MpmAdjoint rollout
};

/// Per-segment token. For flagged segments `result` is a pending future that
/// resolves SAFE/REJECTED once the background MpmAdjoint pass completes; for
/// unflagged segments it is already-ready SAFE.
struct SegmentToken {
    int index = 0;
    double tStart = 0.0, tEnd = 0.0;
    double peakAccel = 0.0;       // m/s^2, max joint |qdd| over the segment
    bool flagged = false;         // surrogate predicted stress > 75% yield
    double surrogateStress = 0.0; // Pa, fast-pass estimate
    std::future<ExactResult> result;
};

class TrajectoryVerifier {
public:
    // Fast sweep is synchronous and cheap; flagged segments fork to std::async.
    // Returns immediately with pending futures for the flagged segments.
    std::vector<SegmentToken> submit(const std::vector<TrajPoint>& traj, const MaterialSpec& mat);

    // Conservative neural-surrogate stand-in: a coarse high-recall pre-filter.
    // The inertial estimate (rho*L*a) under-predicts the true dynamic-impact
    // stress, so we scale it up generously to guarantee the fast pass never
    // misses a real failure; the expensive exact pass then clears the false
    // positives (the whole point of the multi-fidelity split).
    static constexpr double kSurrogateSafety = 10.0;
};

/// TRAJECTORY_HIL_LOOP verification module (Task 3).
bool runTrajectoryHilSelfTest();

} // namespace krs::hil
