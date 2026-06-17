// GraspGate_CoacdReal.cpp -- GATE COACD-REAL (Phase 1): the CoACD collider must PRESERVE grasp-relevant
// concavities (mug handle holes, cup interiors, pitcher handle gaps) that the V-HACD collider FILLS. The prior
// GATE COACD only dropped a ball into a big OPEN bowl -- a concavity V-HACD already preserves -- so it did NOT
// discriminate the two backends on the concavities that actually matter for grasping. This gate does.
//
// Construction (discriminating by design): for each concave object, sample a grid of points. A "cavity point"
// is one that is EMPTY (outside the true watertight mesh, by ray-parity) yet INSIDE the V-HACD collider -- i.e.
// a point V-HACD WRONGLY fills (it bridged a concavity into solid). That set IS the cavity V-HACD fills, so:
//   - V-HACD preserves 0% of it BY DEFINITION  (the failing NEGATIVE CONTROL);
//   - CoACD must leave most of it EMPTY        (the measured number -- a jaw can enter the cavity).
// A point is tested against a collider with PxGeometryQuery::pointDistance(...) == 0 (scene-free, exact inside
// test for convex hulls). The gate PASSES iff, on at least one GRASP-RELEVANT (non-bowl) concavity, V-HACD
// fills a substantial cavity AND CoACD preserves the majority of it -- a test that is FALSE on V-HACD (which
// fills it) and TRUE on CoACD. Locked physics asserted unchanged (same configHash); the collider is the only
// variable.
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspCoacdRealGate() { std::printf("[GRASP GATE COACD-REAL] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspSim.hpp"            // assertPhysicsLocked
#include "GraspMesh.hpp"
#include "CoacdCollider.hpp"
#include "YcbCatalog.hpp"
#include "MeshUtils.hpp"
#include "CollisionCookingService.hpp"
#include "SimulationController.hpp"
#include "RayPick.hpp"
#include <PxPhysicsAPI.h>
#include <vector>
#include <string>
#include <cmath>

using namespace physx;

namespace krs::grasp {
namespace {

// inside the true watertight solid: ray-parity (a slightly-perturbed +X ray; odd # of forward crossings = in).
bool insideTrueMesh(const glm::vec3& p, const RenderableMeshComponent& m) {
    static const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 0.0131f, 0.0071f));
    int crossings = 0;
    const auto& V = m.vertices; const auto& I = m.indices;
    for (size_t t = 0; t + 2 < I.size(); t += 3) {
        float tt;
        if (krs::pick::rayTriangle(p, dir, V[I[t]].position, V[I[t + 1]].position, V[I[t + 2]].position, tt) && tt > 1e-5f)
            ++crossings;
    }
    return (crossings & 1) != 0;
}

bool insideAnyHull(const glm::vec3& p, const std::vector<PxConvexMesh*>& hulls) {
    const PxVec3 pp(p.x, p.y, p.z);
    for (PxConvexMesh* h : hulls)
        if (h && PxGeometryQuery::pointDistance(pp, PxConvexMeshGeometry(h, PxMeshScale(1.0f)), PxTransform(PxIdentity)) == 0.0f)
            return true;
    return false;
}

// cook the CoACD parts file into convex hulls (mirrors GraspSim's CoACD path).
std::vector<PxConvexMesh*> cookCoacd(CollisionCookingService& cook, const std::string& path) {
    std::vector<std::vector<glm::vec3>> parts; std::vector<PxConvexMesh*> hulls;
    if (!loadCoacdParts(path, parts)) return hulls;
    for (auto& part : parts) {
        std::vector<Vertex> vv(part.size());
        for (size_t k = 0; k < part.size(); ++k) vv[k].position = part[k];
        PxConvexMesh* h = cook.requestConvexHull(vv, "creal_coacd").get();
        if (h) hulls.push_back(h);
    }
    return hulls;
}
} // namespace

bool runGraspCoacdRealGate() {
    std::printf("\n[GRASP GATE COACD-REAL] CoACD preserves grasp-relevant concavities that V-HACD FILLS  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    PxPhysics* phys = &PxGetPhysics();
    SimulationController::ensurePhysxExtensions();
    auto& cook = CollisionCookingService::instance();
    if (!cook.isInitialized()) cook.initialize(phys);
    PxMaterial* mat = phys->createMaterial(kLockedPhysics.frictionMu, kLockedPhysics.frictionMu, 0.0f);

    // live-physics guard: a throwaway scene must match the locked config (same hash printed above).
    bool guardOk = false;
    { PxDefaultCpuDispatcher* d = PxDefaultCpuDispatcherCreate(1);
      PxSceneDesc gd(phys->getTolerancesScale()); gd.gravity = PxVec3(0.0f, -kLockedPhysics.gravity, 0.0f);
      gd.cpuDispatcher = d; gd.filterShader = PxDefaultSimulationFilterShader;
      PxScene* gs = phys->createScene(gd);
      guardOk = assertPhysicsLocked(gs, mat, kLockedPhysics.gripForceN);
      gs->release(); d->release(); }

    constexpr int N = 28;            // grid resolution per axis (finer -> tighter thin-wall coverage sampling)
    bool anyGraspRelevantDiscriminates = false, anyCoverageBad = false;
    int nConcaveTested = 0;

    std::printf("  %-18s cavity-pts  V-HACD-presv  CoACD-presv   (solid coverage)   discriminates?\n", "object");
    for (const auto& o : ycbCatalog()) {
        if (!o.concavity) continue;  // the 4 grasp-relevant concave objects (pitcher/bowl/mug/cup)
        RenderableMeshComponent mesh;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
        catch (const std::exception& e) { std::printf("  %-18s load failed: %s\n", o.id.c_str(), e.what()); return false; }
        const MeshMetrics mm = computeMetrics(mesh);

        std::vector<PxConvexMesh*> vhacd = cook.requestConvexDecomposition(mesh.vertices, mesh.indices, "creal_vhacd").get();
        std::vector<PxConvexMesh*> coacd = cookCoacd(cook, o.coacdPath());
        if (vhacd.empty() || coacd.empty()) {
            std::printf("  %-18s collider missing (vhacd=%zu coacd=%zu) -- run scripts/gen_coacd.py\n",
                        o.id.c_str(), vhacd.size(), coacd.size());
            return false;
        }
        ++nConcaveTested;

        const glm::vec3 lo = glm::vec3(mm.aabbMin), hi = glm::vec3(mm.aabbMax), ext = hi - lo;
        int cavityPts = 0, coacdEmpty = 0;
        int solidPts = 0, coacdCover = 0;            // solid-coverage sanity: CoACD must FILL the true solid, not just preserve cavities
        for (int ix = 0; ix < N; ++ix) for (int iy = 0; iy < N; ++iy) for (int iz = 0; iz < N; ++iz) {
            const glm::vec3 p(lo.x + ext.x * (ix + 0.5f) / N, lo.y + ext.y * (iy + 0.5f) / N, lo.z + ext.z * (iz + 0.5f) / N);
            // V-HACD (a fill-everything decomposition) contains BOTH the true solid and the cavities it bridges,
            // so a point outside V-HACD is neither -> skip (cheap convex test first; ray-parity only inside V-HACD).
            if (!insideAnyHull(p, vhacd)) continue;
            if (insideTrueMesh(p, mesh)) {               // SOLID point -> a faithful collider must contain it
                ++solidPts;
                if (insideAnyHull(p, coacd)) ++coacdCover;
            } else {                                     // empty space V-HACD FILLS -> a filled cavity point
                ++cavityPts;
                if (!insideAnyHull(p, coacd)) ++coacdEmpty;  // CoACD leaves it empty -> preserved
            }
        }
        const float coacdPreserve = cavityPts ? float(coacdEmpty) / float(cavityPts) : 0.0f;
        const float coacdCoverage = solidPts ? float(coacdCover) / float(solidPts) : 0.0f;
        // grasp-relevant = a non-bowl concavity (handle / cup interior): the discriminating case the bowl test missed.
        const bool graspRelevant = (o.id != "024_bowl");
        const bool discriminates = (cavityPts >= 40) && (coacdPreserve >= 0.50f);
        // sanity floor: CoACD must faithfully FILL the true solid (a broken/gappy collider would be << this and
        // would cause spurious grasp failures). 0.70 clears a genuinely gappy collider; the graspable objects
        // here read 91-99.9% and only the thin-shell bowl dips to ~83% (grid under-samples 1-2 mm walls).
        const bool covers = (coacdCoverage >= 0.70f);
        if (graspRelevant && discriminates) anyGraspRelevantDiscriminates = true;
        if (!covers) anyCoverageBad = true;

        std::printf("  %-18s %6d         %5.1f%%          %5.1f%%      solidCover %5.1f%%   %s\n",
                    o.id.c_str(), cavityPts, 0.0f, coacdPreserve * 100.0f, coacdCoverage * 100.0f,
                    discriminates ? (graspRelevant ? "DISCRIM(grasp-rel)" : "discrim(bowl)") : "no");
    }

    // PASS: the guard holds AND at least one GRASP-RELEVANT concavity (handle/interior, not the bowl) is a real
    // cavity V-HACD FILLS (>=40 pts, V-HACD preserves 0%) that CoACD PRESERVES (>=50%). The V-HACD 0%-preserve
    // column IS the failing negative control; the gate is FALSE on V-HACD, TRUE on CoACD -> it discriminates.
    const bool pass = guardOk && (nConcaveTested >= 1) && anyGraspRelevantDiscriminates && !anyCoverageBad;
    std::printf("  guard=%s  concave-objects=%d  grasp-relevant-discriminator(V-HACD fills, CoACD preserves)=%d  coverage-ok=%d\n",
                guardOk ? "LOCKED" : "FAIL", nConcaveTested, anyGraspRelevantDiscriminates, !anyCoverageBad);
    std::printf("[GRASP GATE COACD-REAL] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
