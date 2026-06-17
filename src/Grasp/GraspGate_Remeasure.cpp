// GraspGate_Remeasure.cpp -- GATE REMEASURE (Phase 2): re-measure the grasp success rate with the ONLY variable
// being the collider (V-HACD -> CoACD). The planner is geometry-only (it ray-casts the visual mesh, never the
// collider), so the SAME tuned grasps are scored twice -- once with the V-HACD collider, once with CoACD --
// under the SAME LOCKED criterion (configHash asserted unchanged). This is an apples-to-apples isolation: if the
// CoACD rate rises, objects became correctly graspable (a concavity a jaw can now enter), NOT because anything
// got easier. A random-grasp neg-control under CoACD must still score low (the criterion is not trivially
// passable with the new collider). Per-object before/after is reported so flips can be attributed.
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspRemeasureGate() { std::printf("[GRASP GATE REMEASURE] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspPlanner.hpp"
#include "GraspSim.hpp"
#include "GraspMesh.hpp"
#include "YcbCatalog.hpp"
#include "MeshUtils.hpp"
#include <vector>
#include <string>

namespace krs::grasp {

namespace { constexpr int kAttempts = 3; }   // same K and denominator as GATE PLANNER

static int scoreCollider(const RenderableMeshComponent& mesh, const std::vector<GraspSpec>& specs,
                         const WorldOverride& locked, const std::string& coacdPath, bool& allLocked) {
    int succ = 0;
    for (int k = 0; k < kAttempts; ++k) {
        if (k >= int(specs.size())) continue;                 // planner found < K -> failure (counts in denom)
        const GraspResult r = runGripperSim(mesh, specs[size_t(k)], locked, coacdPath);
        if (!r.physicsLocked) allLocked = false;              // the locked-physics guard must hold in every run
        if (graspSucceeded(r)) ++succ;
    }
    return succ;
}

bool runGraspRemeasureGate() {
    std::printf("\n[GRASP GATE REMEASURE] V-HACD vs CoACD collider, SAME tuned grasps + LOCKED criterion  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    const WorldOverride locked{};
    const PlannerParams tune = tunedPlannerParams();

    int totV = 0, totC = 0, totRandC = 0, denom = 0, nObj = 0, flippedUp = 0, flippedDown = 0;
    bool allLocked = true;

    std::printf("  %-22s  V-HACD  CoACD   rand(CoACD)   delta  [category]\n", "object");
    int oi = 0;
    for (const auto& o : ycbCatalog()) {
        ++oi;
        RenderableMeshComponent mesh;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
        catch (const std::exception& e) { std::printf("  %-22s load failed: %s\n", o.id.c_str(), e.what()); return false; }
        const MeshMetrics mm = computeMetrics(mesh);

        const std::vector<GraspSpec> tuneSpecs = planAntipodal(mesh, mm, tune);   // collider-independent grasps
        std::vector<GraspSpec> randSpecs;
        for (int k = 0; k < kAttempts; ++k) randSpecs.push_back(randomGrasp(mm, unsigned(oi * 100 + k)));

        const int sV  = scoreCollider(mesh, tuneSpecs, locked, "",             allLocked);  // V-HACD (before)
        const int sC  = scoreCollider(mesh, tuneSpecs, locked, o.coacdPath(),  allLocked);  // CoACD  (after)
        const int sRC = scoreCollider(mesh, randSpecs, locked, o.coacdPath(),  allLocked);  // random neg-ctrl under CoACD

        if (sC > sV) ++flippedUp; else if (sC < sV) ++flippedDown;
        totV += sV; totC += sC; totRandC += sRC; denom += kAttempts; ++nObj;

        std::printf("  %-22s  %d/%d     %d/%d    %d/%d         %+d     [%s]%s\n",
                    o.id.c_str(), sV, kAttempts, sC, kAttempts, sRC, kAttempts, sC - sV, o.category.c_str(),
                    (sC != sV) ? "  <-- flipped" : "");
    }

    const float rV = denom ? float(totV) / denom : 0.0f;
    const float rC = denom ? float(totC) / denom : 0.0f;
    const float rRC = denom ? float(totRandC) / denom : 0.0f;

    std::printf("  ------------------------------------------------------------\n");
    std::printf("  RATE  V-HACD(before)=%.1f%% (%d/%d)   CoACD(after)=%.1f%% (%d/%d)   delta=%+.1f%%   random(CoACD)=%.1f%%\n",
                rV * 100, totV, denom, rC * 100, totC, denom, (rC - rV) * 100, rRC * 100);
    std::printf("  objects improved=%d  regressed=%d  (over %d objects)\n", flippedUp, flippedDown, nObj);

    // PASS validates the MEASUREMENT, not a particular direction (an honest re-measurement): the locked-physics
    // guard held in EVERY run (same hash, collider-only change), the random neg-control stays low under CoACD,
    // and the heuristic still beats random under CoACD. The before/after delta is REPORTED, not required to be
    // positive -- a flat rate would be the honest finding that the collider was not the bottleneck.
    const bool t_locked  = allLocked;
    const bool t_random  = (rRC <= 0.30f) && (rC > rRC);
    const bool pass = t_locked && t_random;
    std::printf("  all-runs-locked(same hash)=%d  random-low&beaten(CoACD)=%d\n", t_locked, t_random);
    std::printf("[GRASP GATE REMEASURE] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
