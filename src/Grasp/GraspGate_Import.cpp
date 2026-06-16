// GraspGate_Import.cpp -- GATE IMPORT (Phase 0). Every YCB object loads with a valid mesh, sits at real-meter
// scale (universal manipulation band + tight published-dimension anchors where pinnable), has finite
// mass/inertia (mass = stated density x enclosed volume), and is NaN-free. The negative control proves the
// scale check has teeth: a x1000 (mm-as-meters) copy of an object becomes a ~100 m monster and MUST be rejected.
#include "GraspGates.hpp"
#include "GraspPhysicsConfig.hpp"
#include "YcbCatalog.hpp"
#include "GraspMesh.hpp"
#include "MeshUtils.hpp"
#include <cstdio>
#include <cmath>
#include <stdexcept>

namespace krs::grasp {

static bool scalePasses(const MeshMetrics& m, const YcbObject& o) {
    if (!(m.longest >= kScaleBandMin && m.longest <= kScaleBandMax)) return false;   // universal band
    if (o.anchorLongestMin > 0.0 && !(m.longest >= o.anchorLongestMin && m.longest <= o.anchorLongestMax))
        return false;                                                               // tight published anchor
    return true;
}

bool runGraspImportGate() {
    std::printf("\n[GRASP GATE IMPORT] YCB load + real-meter scale + mass/inertia + NaN  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());
    const auto& cat = ycbCatalog();
    const double density = double(kLockedPhysics.densityKgM3);
    int pass = 0; const int total = int(cat.size());

    for (const YcbObject& o : cat) {
        bool loaded = false, sOk = false, mOk = false, nOk = false;
        double longest = 0, massKg = 0; int nv = 0, nt = 0;
        try {
            const RenderableMeshComponent mesh = MeshUtils::loadMeshFromFile(o.meshPath());
            loaded = !mesh.vertices.empty() && !mesh.indices.empty();
            const MeshMetrics m = computeMetrics(mesh);
            longest = m.longest; nv = m.nVerts; nt = m.nTris;
            nOk = m.finite;                                  // positions/normals finite AND volume > 0
            massKg = density * m.volume;                     // stated density x enclosed volume
            mOk = std::isfinite(massKg) && massKg >= 0.005 && massKg <= 5.0;
            sOk = scalePasses(m, o);
        } catch (const std::exception& e) {
            std::printf("  %-20s LOAD-FAILED: %s\n", o.id.c_str(), e.what());
        }
        const bool ok = loaded && sOk && mOk && nOk;
        if (ok) ++pass;
        std::printf("  %-20s n=%6d tris=%6d longest=%.4f m mass=%.3f kg [scale %s mass %s nan-free %s] -> %s\n",
                    o.id.c_str(), nv, nt, longest, massKg,
                    sOk ? "ok" : "BAD", mOk ? "ok" : "BAD", nOk ? "ok" : "BAD", ok ? "ok" : "FAIL");
    }

    // ---- NEG-CTRL: a x1000 (mm-as-meters) copy must be REJECTED by the scale check (the band has teeth). ----
    bool negRejects = false; double negLongest = 0;
    try {
        const RenderableMeshComponent base = MeshUtils::loadMeshFromFile(cat.front().meshPath());
        const RenderableMeshComponent big = scaledCopy(base, 1000.0);
        const MeshMetrics m = computeMetrics(big);
        negLongest = m.longest;
        negRejects = !(m.longest >= kScaleBandMin && m.longest <= kScaleBandMax);
    } catch (const std::exception& e) {
        std::printf("  NEG-CTRL load-failed: %s\n", e.what());
    }
    std::printf("  NEG-CTRL : %s x1000 -> longest=%.1f m -> scale check %s\n",
                cat.front().id.c_str(), negLongest, negRejects ? "REJECTS (ok)" : "accepts (FAIL)");

    const bool result = (pass == total) && negRejects;
    std::printf("[GRASP GATE IMPORT] %d/%d objects valid; neg-ctrl rejects x1000 -> %s\n",
                pass, total, result ? "PASS" : "FAIL");
    return result;
}

} // namespace krs::grasp
