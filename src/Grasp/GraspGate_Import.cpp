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
        bool loaded = false, sOk = false, mOk = false, nOk = false, wOk = false;
        double longest = 0, massKg = 0, bfrac = 0; int nv = 0;
        try {
            const RenderableMeshComponent mesh = MeshUtils::loadMeshFromFile(o.meshPath());
            loaded = !mesh.vertices.empty() && !mesh.indices.empty();
            const MeshMetrics m = computeMetrics(mesh);
            longest = m.longest; nv = m.nVerts; bfrac = m.boundaryFrac;
            nOk = m.finite;                                  // positions/normals finite AND volume > 0
            wOk = m.watertight;                              // closed + consistent winding (cookable; correct sign)
            massKg = density * m.volume;                     // raw-mesh upper-bound mass (Phase-2 re-asserts vs live getMass)
            mOk = std::isfinite(massKg) && massKg >= 0.005 && massKg <= 5.0;
            sOk = scalePasses(m, o);
        } catch (const std::exception& e) {
            std::printf("  %-20s LOAD-FAILED: %s\n", o.id.c_str(), e.what());
        }
        const bool ok = loaded && sOk && mOk && nOk && wOk;
        if (ok) ++pass;
        std::printf("  %-20s n=%6d longest=%.4f m mass=%.3f kg bdry=%.2f%% [scale %s mass %s nan %s watertight %s] -> %s\n",
                    o.id.c_str(), nv, longest, massKg, 100.0 * bfrac,
                    sOk ? "ok" : "BAD", mOk ? "ok" : "BAD", nOk ? "ok" : "BAD", wOk ? "ok" : "BAD", ok ? "ok" : "FAIL");
    }

    // ---- NEG-CTRLs: prove the scale check has teeth in BOTH directions and via the tight anchor path. ----
    auto loadObj = [&](const char* id) -> RenderableMeshComponent {
        for (const auto& o : cat) if (o.id == id) return MeshUtils::loadMeshFromFile(o.meshPath());
        return MeshUtils::loadMeshFromFile(cat.front().meshPath());
    };
    bool negBig = false, negSmall = false, negAnchor = false;
    double bigL = 0, smallL = 0, anchorL = 0;
    try {
        const RenderableMeshComponent base = loadObj("003_cracker_box");
        bigL = computeMetrics(scaledCopy(base, 1000.0)).longest;        // mm-as-meters
        negBig = !(bigL >= kScaleBandMin && bigL <= kScaleBandMax);
        smallL = computeMetrics(scaledCopy(base, 0.001)).longest;        // m-as-km (under-scale)
        negSmall = !(smallL >= kScaleBandMin && smallL <= kScaleBandMax);
        // anchor-path: bowl x1.5 stays inside the universal band [0.02,0.40] but breaks its tight [0.145,0.175].
        YcbObject bowl{}; for (const auto& o : cat) if (o.id == "024_bowl") bowl = o;
        const MeshMetrics mb = computeMetrics(scaledCopy(loadObj("024_bowl"), 1.5));
        anchorL = mb.longest;
        negAnchor = (anchorL >= kScaleBandMin && anchorL <= kScaleBandMax) && !scalePasses(mb, bowl);
    } catch (const std::exception& e) {
        std::printf("  NEG-CTRL load-failed: %s\n", e.what());
    }
    std::printf("  NEG-CTRL : x1000 ->%.1f m %s ; x0.001 ->%.5f m %s ; bowl x1.5 ->%.3f m (in band) anchor %s\n",
                bigL, negBig ? "REJECT(ok)" : "accept(FAIL)",
                smallL, negSmall ? "REJECT(ok)" : "accept(FAIL)",
                anchorL, negAnchor ? "REJECT(ok)" : "accept(FAIL)");

    // ---- NEG-CTRL (watertight has teeth): a HOLED copy (10% of triangles removed -> open boundary) must be
    //      rejected by the watertight check, while the intact mesh passed it. ----
    bool negHole = false; double holeBdry = 0;
    try {
        RenderableMeshComponent holed = loadObj("003_cracker_box");
        const size_t keepTris = (holed.indices.size() / 3) * 9 / 10;   // drop ~10% of triangles
        holed.indices.resize(keepTris * 3);
        const MeshMetrics mh = computeMetrics(holed);
        holeBdry = mh.boundaryFrac;
        negHole = !mh.watertight && mh.boundaryFrac > 0.01;            // open boundary -> not watertight
    } catch (const std::exception& e) {
        std::printf("  NEG-CTRL(hole) load-failed: %s\n", e.what());
    }
    std::printf("  NEG-CTRL : holed cracker_box (10%% tris removed) bdry=%.1f%% -> watertight %s\n",
                100.0 * holeBdry, negHole ? "REJECT(ok)" : "accept(FAIL)");

    const bool result = (pass == total) && negBig && negSmall && negAnchor && negHole;
    std::printf("[GRASP GATE IMPORT] %d/%d objects valid; neg-ctrls (x1000 / x0.001 / anchor / holed-mesh) reject -> %s\n",
                pass, total, result ? "PASS" : "FAIL");
    return result;
}

} // namespace krs::grasp
