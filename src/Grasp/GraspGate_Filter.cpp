// GraspGate_Filter.cpp -- GATE FILTER (Phase 2): the contamination guard. Over the raw GSO library, classify
// every object as graspable-VALID or REJECTED (with a reason: load-fail / degenerate / non-watertight /
// out-of-scale / non-graspable), and report N raw -> N valid + the rejection breakdown. The rejected fraction
// is a DATASET-QUALITY statistic, NOT a grasp failure. The survivors' names are written to a file so only they
// get CoACD colliders + are grasped (the generalized rate is over VALID objects only). NEGATIVE CONTROLS: a
// known-valid mesh, mis-scaled x1000 (building) / x0.001 (sub-cm) / holed (non-watertight), MUST be rejected --
// proving the filter discriminates and is not trivially passing everything. A 0% rejection rate would be
// suspicious; the gate asserts the rejection rate is non-zero.
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspFilterGate() { std::printf("[GRASP GATE FILTER] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspFilter.hpp"
#include "GraspMesh.hpp"
#include "GsoCatalog.hpp"
#include "MeshUtils.hpp"
#include <vector>
#include <array>
#include <string>
#include <fstream>
#include <cstdlib>

namespace krs::grasp {

namespace {
// a "holed" copy: drop ~8% of triangles to open the surface (boundaryFrac up -> NON_WATERTIGHT).
RenderableMeshComponent holedCopy(const RenderableMeshComponent& m) {
    RenderableMeshComponent h = m; h.indices.clear();
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
        if ((t / 3) % 12 != 0) { h.indices.push_back(m.indices[t]); h.indices.push_back(m.indices[t+1]); h.indices.push_back(m.indices[t+2]); }
    return h;
}
} // namespace

bool runGraspFilterGate() {
    std::printf("\n[GRASP GATE FILTER] GSO mesh validity + graspability filter (real-metric)  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    const auto& cat = gsoCatalog();
    if (cat.empty()) { std::printf("  NO GSO objects under assets/gso -- run scripts/download_gso.py\n"); return false; }

    std::array<int, F_COUNT> hist{};
    std::vector<std::string> valid;
    RenderableMeshComponent firstValidMesh; bool haveValidMesh = false;

    std::printf("  scanning %zu raw GSO objects ...\n", cat.size());
    int n = 0;
    for (const auto& o : cat) {
        ++n;
        RenderableMeshComponent mesh;
        FilterResult r;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
        catch (...) { ++hist[F_LOADFAIL]; if (n % 50 == 0) std::printf("    [%d/%zu] ...\n", n, cat.size()); continue; }
        const MeshMetrics mm = computeMetrics(mesh);
        r = classifyGraspable(mm);
        ++hist[size_t(r)];
        if (r == F_VALID) {
            valid.push_back(o.name);
            if (!haveValidMesh) { firstValidMesh = mesh; haveValidMesh = true; }
        }
        if (n % 50 == 0) std::printf("    [%d/%zu] valid so far=%zu\n", n, cat.size(), valid.size());
    }

    const int raw = int(cat.size());
    const int nValid = hist[F_VALID];
    const float rejectRate = raw ? 100.0f * float(raw - nValid) / float(raw) : 0.0f;

    std::printf("  ---- N raw=%d -> N valid=%d  (rejection rate %.1f%%) ----\n", raw, nValid, rejectRate);
    for (int i = 1; i < F_COUNT; ++i)
        if (hist[size_t(i)] > 0) std::printf("    rejected %-16s %d\n", filterName(i), hist[size_t(i)]);

    // write the survivor list for CoACD generation + GATE GENERALIZE.
    const char* env = std::getenv("KRS_GSO_DIR");
    const std::string outPath = (env ? std::string(env) : std::string("C:/Users/kurtz/KRStudio/KRStudio/assets/gso")) + "/_valid.txt";
    { std::ofstream out(outPath); for (const auto& v : valid) out << v << "\n"; }
    std::printf("  wrote %zu valid names -> %s\n", valid.size(), outPath.c_str());

    // NEG-CONTROLS: a valid mesh, deliberately broken/mis-scaled, MUST be rejected (the filter discriminates).
    bool negOk = false;
    if (haveValidMesh) {
        const FilterResult big   = classifyGraspable(computeMetrics(scaledCopy(firstValidMesh, 1000.0)));  // building
        const FilterResult tiny  = classifyGraspable(computeMetrics(scaledCopy(firstValidMesh, 0.001)));   // sub-cm
        const FilterResult holed = classifyGraspable(computeMetrics(holedCopy(firstValidMesh)));            // non-watertight
        std::printf("  neg-ctrl: x1000=%s  x0.001=%s  holed=%s  (all must be != VALID)\n",
                    filterName(big), filterName(tiny), filterName(holed));
        negOk = (big != F_VALID) && (tiny != F_VALID) && (holed != F_VALID);
    }

    // PASS: some objects survive (so the generalized measurement has a population), the filter REJECTS a non-zero
    // fraction (it is not trivially passing everything), and the neg-controls are all rejected (it discriminates).
    const bool t_pop    = (nValid >= 50);
    const bool t_reject = (rejectRate > 0.0f);
    const bool t_neg    = negOk;
    const bool pass = t_pop && t_reject && t_neg;
    std::printf("  population(>=50 valid)=%d  rejects-some(>0%%)=%d  neg-ctrls-rejected=%d\n", t_pop, t_reject, t_neg);
    std::printf("[GRASP GATE FILTER] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
