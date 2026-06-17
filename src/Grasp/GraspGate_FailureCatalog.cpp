// GraspGate_FailureCatalog.cpp -- GATE FAILURE-CATALOG (Phase 4): characterise WHY the tuned planner's grasps
// fail. Over the whole 20-object YCB library, under the SAME LOCKED physics, run the tuned planner's grasps and
// classify EVERY non-success into an exhaustive taxonomy of physical failure modes. The gate asserts 100%
// classification coverage (no failure falls through) -- and a NEGATIVE CONTROL proves the coverage metric has
// teeth: an INCOMPLETE taxonomy (that only recognises "no grasp found" + "not gripped") leaves the slip/drift
// failures UNCLASSIFIED, so its coverage is < 100%. The taxonomy is the deliverable: which objects fail and why.
//
// Failure modes (every non-success maps to exactly one; checked in physical-root-cause order):
//   NO_ANTIPODAL_GRASP  -- the planner found no antipodal pair for this attempt (thin-shell / unreachable cavity)
//   GRIP_NOT_SEATED     -- both jaws never closed on the object (vertical axis under the table, or ejected)
//   UNBOUNDED_GRIP      -- a transient contact spike pushed the peak jaw force over the locked bound; the
//                          force-bound criterion clause REJECTED it (the anti-cheat WORKING -- a real failure mode,
//                          never a success, so it cannot inflate the rate)
//   SLIP_FELL           -- gripped + lifted, then slipped out and re-touched the ground
//   CONTACT_INTERMITTENT-- gripped but jaw contact was not continuous (>=90%) through lift+hold
//   DRIFT_ROTATE        -- held to the end but the object's centre missed the target pose (rotated/drifted)
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspFailureCatalogGate() { std::printf("[GRASP GATE FAILURE-CATALOG] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspPlanner.hpp"
#include "GraspSim.hpp"
#include "GraspMesh.hpp"
#include "YcbCatalog.hpp"
#include "MeshUtils.hpp"
#include <vector>
#include <array>

namespace krs::grasp {

namespace {
constexpr int kAttempts = 3;
enum Fail { F_SUCCESS = 0, F_NO_GRASP, F_NOT_SEATED, F_UNBOUNDED, F_SLIP_FELL, F_INTERMITTENT, F_DRIFT, F_COUNT };
const char* kName[F_COUNT] = { "SUCCESS", "NO_ANTIPODAL_GRASP", "GRIP_NOT_SEATED", "UNBOUNDED_GRIP",
                               "SLIP_FELL", "CONTACT_INTERMITTENT", "DRIFT_ROTATE" };

// Classify one attempt. `found` = did the planner propose a grasp for this slot. Returns a mode in [0,F_COUNT)
// for the FULL taxonomy; the order is physical root cause first. Returns -1 only if nothing matches (the
// "unclassified" sentinel the gate asserts never happens).
int classifyFull(bool found, const GraspResult& r) {
    if (!found)                                                       return F_NO_GRASP;
    if (graspSucceeded(r))                                           return F_SUCCESS;
    if (r.maxJawForceN > kLockedPhysics.maxGripForceFactor * kLockedPhysics.gripForceN) return F_UNBOUNDED;
    if (!r.grippedAtLiftoff)                                         return F_NOT_SEATED;
    if (r.groundContactAfterLiftoff)                                 return F_SLIP_FELL;
    if (r.contactFrac < kLockedPhysics.contactFrac)                 return F_INTERMITTENT;
    if (r.centerErrM >= kLockedPhysics.successDistM)               return F_DRIFT;
    return -1;                                                        // unclassified (should never happen)
}

// NEG-CONTROL: a deliberately INCOMPLETE taxonomy that only recognises the two coarsest modes. Everything else
// is "unclassified" -> its coverage of the real failures is < 100%, proving the full taxonomy's 100% is earned.
bool classifyPartial(bool found, const GraspResult& r, int& mode) {
    if (!found)            { mode = F_NO_GRASP;   return true; }
    if (graspSucceeded(r)) { mode = F_SUCCESS;    return true; }
    if (!r.grippedAtLiftoff){ mode = F_NOT_SEATED; return true; }
    return false;                                                     // slip / intermittent / drift -> UNCLASSIFIED
}
} // namespace

bool runGraspFailureCatalogGate() {
    std::printf("\n[GRASP GATE FAILURE-CATALOG] classify 100%% of tuned-planner failures (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    const WorldOverride locked{};
    const PlannerParams tune = tunedPlannerParams();

    std::array<int, F_COUNT> hist{};               // full-taxonomy histogram over all attempts
    int totalAttempts = 0, totalFailures = 0, unclassifiedFull = 0, unclassifiedPartial = 0;

    std::printf("  %-22s succ  dominant-failure        modes\n", "object");
    int oi = 0;
    for (const auto& o : ycbCatalog()) {
        ++oi;
        RenderableMeshComponent mesh;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
        catch (const std::exception& e) { std::printf("  %-22s load failed: %s\n", o.id.c_str(), e.what()); return false; }
        const MeshMetrics mm = computeMetrics(mesh);
        const std::vector<GraspSpec> specs = planAntipodal(mesh, mm, tune);

        std::array<int, F_COUNT> objHist{};
        int succ = 0;
        for (int k = 0; k < kAttempts; ++k) {
            const bool found = (k < int(specs.size()));
            GraspResult r{};
            if (found) r = runGripperSim(mesh, specs[size_t(k)], locked);

            const int mFull = classifyFull(found, r);
            if (mFull < 0) { ++unclassifiedFull; }
            else { ++hist[size_t(mFull)]; ++objHist[size_t(mFull)]; if (mFull == F_SUCCESS) ++succ; }

            int mPart = -1;
            if (!classifyPartial(found, r, mPart)) ++unclassifiedPartial;   // partial taxonomy can't place it

            ++totalAttempts;
            if (mFull != F_SUCCESS) ++totalFailures;
        }

        // dominant non-success mode for this object (for the catalogue line).
        int dom = -1, domN = 0;
        for (int m = 1; m < F_COUNT; ++m) if (objHist[size_t(m)] > domN) { domN = objHist[size_t(m)]; dom = m; }
        std::printf("  %-22s %d/%d   %-22s [%s]\n", o.id.c_str(), succ, kAttempts,
                    dom < 0 ? "(none -- all succeeded)" : kName[dom], o.category.c_str());
    }

    // taxonomy histogram.
    std::printf("  ---------------- taxonomy (%d attempts, %d failures) ----------------\n", totalAttempts, totalFailures);
    for (int m = 1; m < F_COUNT; ++m)
        if (hist[size_t(m)] > 0) std::printf("    %-22s %d\n", kName[m], hist[size_t(m)]);

    const float covFull    = totalFailures ? 100.0f * float(totalFailures - unclassifiedFull) / float(totalFailures) : 100.0f;
    const float covPartial = totalFailures ? 100.0f * float(totalFailures - unclassifiedPartial) / float(totalFailures) : 100.0f;
    int distinctModes = 0; for (int m = 1; m < F_COUNT; ++m) if (hist[size_t(m)] > 0) ++distinctModes;

    std::printf("  coverage: FULL taxonomy = %.1f%% (%d unclassified) ; INCOMPLETE neg-ctrl = %.1f%% (%d unclassified)\n",
                covFull, unclassifiedFull, covPartial, unclassifiedPartial);
    std::printf("  distinct failure modes observed = %d ; UNBOUNDED_GRIP (force-bound clause rejected) = %d (anti-cheat working)\n",
                distinctModes, hist[F_UNBOUNDED]);

    // PASS: the full taxonomy classifies 100% of failures, the taxonomy is non-trivial (>=2 modes), and the
    // INCOMPLETE neg-control genuinely leaves gaps (<100%) -- proving the 100% coverage is earned, not vacuous.
    const bool t_full    = (unclassifiedFull == 0);
    const bool t_rich    = (distinctModes >= 2);
    const bool t_negctrl = (totalFailures == 0) || (unclassifiedPartial > 0 && covPartial < 100.0f);
    const bool pass = t_full && t_rich && t_negctrl;

    std::printf("  full-covers-100%%=%d  taxonomy-rich(>=2)=%d  incomplete-leaves-gaps=%d\n",
                t_full, t_rich, t_negctrl);
    std::printf("[GRASP GATE FAILURE-CATALOG] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
