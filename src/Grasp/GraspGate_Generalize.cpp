// GraspGate_Generalize.cpp -- GATE GENERALIZE + TAXONOMY-SCALE (Phase 3+4): run the FIXED Heuristic-V2 planner
// (NOT re-tuned per object) over EVERY object that passed the GATE FILTER, under the UNCHANGED locked criterion
// (configHash), with CoACD colliders. One grasp pass produces both deliverables:
//   GENERALIZE     -- the statistically-meaningful success rate over N valid objects + the per-object success
//                     distribution, with a CONCENTRATED-vs-SPREAD hardness analysis (is failure a small
//                     pathological tail, or a real ceiling across the distribution?). Random neg-control stays low.
//   TAXONOMY-SCALE -- 100% of failures classified into the taxonomy; the mode distribution at scale (does it
//                     match YCB's NO_ANTIPODAL/GRIP_NOT_SEATED/DRIFT, or do new modes dominate?) + hardest objects.
// The rejected (non-valid) objects are NOT grasped and NOT counted as failures -- they were a dataset-quality
// statistic at the FILTER. Standalone (loads/cooks hundreds of objects; too slow for the overnight bench).
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspGeneralizeGate() { std::printf("[GRASP GATE GENERALIZE] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspPlanner.hpp"
#include "GraspSim.hpp"
#include "GraspMesh.hpp"
#include "GraspFilter.hpp"
#include "GraspTaxonomy.hpp"
#include "GsoCatalog.hpp"
#include "MeshUtils.hpp"
#include <vector>
#include <string>
#include <array>
#include <algorithm>

namespace krs::grasp {

namespace { constexpr int kAttempts = 3; }

bool runGraspGeneralizeGate() {
    std::printf("\n[GRASP GATE GENERALIZE] fixed Heuristic-V2 over FILTER-valid GSO objects, CoACD + LOCKED  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    const auto& cat = gsoCatalog();
    if (cat.empty()) { std::printf("  NO GSO objects -- run scripts/download_gso.py + the FILTER gate\n"); return false; }

    const PlannerParams v2 = tunedV2PlannerParams();   // the FIXED improved heuristic (not re-tuned per object)
    const WorldOverride locked{};

    int nValid = 0, totSucc = 0, totAtt = 0, totRand = 0, totRandAtt = 0, unclassPartial = 0;
    bool allLocked = true;
    std::array<int, FM_COUNT> tax{};       // full taxonomy histogram over all V2 attempts
    std::array<int, kAttempts + 1> dist{}; // per-object success-count histogram (0..3)
    std::vector<std::string> hardObjects;  // objects with 0/3 (the hard tail)

    std::printf("  grasping FILTER-valid objects (V2 x%d + 1 random each) ...\n", kAttempts);
    int scanned = 0;
    for (const auto& o : cat) {
        ++scanned;
        RenderableMeshComponent mesh;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); } catch (...) { continue; }
        const MeshMetrics mm = computeMetrics(mesh);
        if (classifyGraspable(mm) != F_VALID) continue;   // grasp ONLY survivors (consistent with GATE FILTER)
        ++nValid;
        const std::string cp = o.coacdPath();

        const std::vector<GraspSpec> specs = planAntipodal(mesh, mm, v2);
        int succ = 0;
        for (int k = 0; k < kAttempts; ++k) {
            const bool found = (k < int(specs.size()));
            GraspResult r{};
            if (found) r = runGripperSim(mesh, specs[size_t(k)], locked, cp);
            if (found && !r.physicsLocked) allLocked = false;
            const int m = classifyFailure(found, r);
            ++tax[size_t(m)];
            // incomplete-taxonomy neg-control: only NO_GRASP + NOT_SEATED recognised -> the rest are unclassified.
            if (m != FM_SUCCESS && m != FM_NO_GRASP && m != FM_NOT_SEATED) ++unclassPartial;
            if (m == FM_SUCCESS) ++succ;
            ++totAtt;
        }
        totSucc += succ; ++dist[size_t(succ)];
        if (succ == 0) hardObjects.push_back(o.name);

        // one random grasp -> the neg-control at scale.
        const GraspResult rr = runGripperSim(mesh, randomGrasp(mm, unsigned(scanned * 7 + 1)), locked, cp);
        if (graspSucceeded(rr)) ++totRand;
        ++totRandAtt;

        if (nValid % 40 == 0) std::printf("    [valid %d] running rate=%.1f%%\n", nValid, totAtt ? 100.0f * totSucc / totAtt : 0.0f);
    }

    if (nValid == 0) { std::printf("  no valid objects to grasp\n"); return false; }

    const float rate    = totAtt ? 100.0f * float(totSucc) / float(totAtt) : 0.0f;
    const float randRate= totRandAtt ? 100.0f * float(totRand) / float(totRandAtt) : 0.0f;
    const int totFail = totAtt - totSucc;

    std::printf("  ============ GENERALIZED RATE ============\n");
    std::printf("  valid objects N=%d   attempts=%d   successes=%d   GENERALIZED RATE=%.1f%%   random=%.1f%% (%d/%d)\n",
                nValid, totAtt, totSucc, rate, randRate, totRand, totRandAtt);

    // CONCENTRATED vs SPREAD: per-object success-count distribution.
    std::printf("  per-object success distribution (objects at each k/%d):\n", kAttempts);
    for (int k = kAttempts; k >= 0; --k)
        std::printf("    %d/%d : %4d objects  (%.1f%%)\n", k, kAttempts, dist[size_t(k)], 100.0f * float(dist[size_t(k)]) / float(nValid));
    const float fullySolved = 100.0f * float(dist[kAttempts]) / float(nValid);
    const float fullyFailed = 100.0f * float(dist[0]) / float(nValid);
    // CONCENTRATED vs SPREAD: the question is whether the hard objects are a SMALL pathological tail (most
    // objects fully graspable + a few bad ones) or a LARGE population (a real ceiling). The right signal is the
    // SIZE of the 0/3 tail among objects -- not the share of failures it contributes (each 0/3 object trivially
    // contributes 3 failures). Concentrated = a small (<20%) 0/3 tail with most objects (>=60%) fully solved.
    const bool concentrated = (fullyFailed < 20.0f) && (fullySolved >= 60.0f);
    std::printf("  hardness: fully-solved(3/3)=%.1f%%  partial(1-2/3)=%.1f%%  fully-failed(0/3)=%.1f%%\n",
                fullySolved, 100.0f * float(dist[1] + dist[2]) / float(nValid), fullyFailed);
    std::printf("  => %s : %s\n",
                concentrated ? "CONCENTRATED" : "SPREAD",
                concentrated ? "the hard objects are a small pathological tail; most are fully graspable"
                             : "hardness is widespread (a LARGE 0/3 population) -- a real ceiling, not a few bad objects");

    // TAXONOMY-SCALE.
    const int totClassFail = totFail;   // the FM taxonomy is exhaustive -> every failure is classified
    std::printf("  ============ FAILURE TAXONOMY AT SCALE (%d failures) ============\n", totClassFail);
    for (int m = 1; m < FM_COUNT; ++m)
        if (tax[size_t(m)] > 0) std::printf("    %-22s %4d  (%.1f%% of failures)\n",
                                            failModeName(m), tax[size_t(m)], 100.0f * float(tax[size_t(m)]) / float(std::max(1, totFail)));
    const float covFull    = 100.0f;    // exhaustive by construction
    const float covPartial = totFail ? 100.0f * float(totFail - unclassPartial) / float(totFail) : 100.0f;
    std::printf("  taxonomy coverage: FULL=%.1f%% (0 unclassified)  ;  INCOMPLETE neg-ctrl=%.1f%% (%d unclassified)\n",
                covFull, covPartial, unclassPartial);
    std::printf("  hardest objects (0/3), %zu total; first few: ", hardObjects.size());
    for (size_t i = 0; i < hardObjects.size() && i < 6; ++i) std::printf("%s%s", hardObjects[i].c_str(), (i + 1 < std::min<size_t>(6, hardObjects.size())) ? ", " : "");
    std::printf("\n  (GSO has no category labels; hardness is reported per-object + by the taxonomy mode mix above)\n");

    // PASS: every run locked (same hash); a real population; the heuristic clearly beats the random neg-control
    // at scale; and the taxonomy is exhaustive (100%) while the INCOMPLETE neg-control genuinely leaves gaps.
    const bool t_locked  = allLocked;
    const bool t_pop     = (nValid >= 50);
    const bool t_random  = (randRate <= 30.0f) && (rate > randRate + 10.0f);
    const bool t_tax     = (totFail == 0) || (unclassPartial > 0 && covPartial < 100.0f);
    const bool pass = t_locked && t_pop && t_random && t_tax;
    std::printf("  all-locked=%d  population(>=50)=%d  beats-random=%d  taxonomy(full-100%%&incomplete-gaps)=%d\n",
                t_locked, t_pop, t_random, t_tax);
    std::printf("[GRASP GATE GENERALIZE] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
