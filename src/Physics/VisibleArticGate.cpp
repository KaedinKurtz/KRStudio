#include "VisibleArticGate.hpp"

// The V-assign gate needs BOTH the live articulation (PhysX) and the imported
// STEP solids with their B-Rep bores (OpenCASCADE). Without either, there is
// nothing to validate -> vacuous pass.
#if !defined(KR_WITH_PHYSX) || !defined(KR_WITH_OCCT)
namespace krs::dyn { bool runVisibleArticGateV() { return true; } }
#else

#include "ArticulationSpec.hpp"
#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "CadImporter.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace krs::dyn {
namespace {

// --- rigid pose math (double precision; the arm spans ~3 m, sub-mm metrics) ---
struct DPose { glm::dvec3 p{0.0}; glm::dquat q{1.0, 0.0, 0.0, 0.0}; };

DPose fromArr(const std::array<float,7>& a) {                 // [px,py,pz, qx,qy,qz,qw]
    return { glm::dvec3(a[0], a[1], a[2]), glm::dquat(a[6], a[3], a[4], a[5]) };
}
// delta that carries a body from its REST world pose to its CURRENT world pose:
// worldNow = delta * worldRest, for any body-fixed point.  delta.q = qNow*qRest^-1.
DPose deltaOf(const DPose& now, const DPose& rest) {
    DPose d; d.q = now.q * glm::inverse(rest.q); d.p = now.p - d.q * rest.p; return d;
}
glm::dvec3 applyPt (const DPose& d, const glm::dvec3& x) { return d.q * x + d.p; }
glm::dvec3 applyDir(const DPose& d, const glm::dvec3& v) { return d.q * v; }

// magnitude of the relative rotation between two link deltas (radians)
double relAngle(const DPose& a, const DPose& b) {
    glm::dquat r = a.q * glm::inverse(b.q);
    double w = std::min(1.0, std::abs(r.w));
    return 2.0 * std::acos(w);
}
// perpendicular distance from point X to the line through P with unit dir d
double ptLine(const glm::dvec3& X, const glm::dvec3& P, const glm::dvec3& d) {
    const glm::dvec3 w = X - P;
    return glm::length(w - glm::dot(w, d) * d);
}

struct Bore { glm::dvec3 P0; glm::dvec3 A0; double r; };       // rest world axis line + radius
struct Solid { bool present=false; entt::entity ent{entt::null}; std::vector<Bore> bores; };
struct Hinge { int kA, iA, kB, iB; };                          // a shared (coaxial) bore pair

// 17 solids (inspect index 0..16) -> serial link. Links: 0 fixed base, 1 J1 yaw,
// 2 J2 shoulder, 3 J3 elbow (carries the forearm, the counterbalance STRUT, the
// bolt heads, and the wrist held rigid while J4 is frozen). Derived from the
// shared-hinge connectivity (ROADMAP R.1), NOT from the offset-fit.
int assignLink(int k) {
    if (k == 16 || k == 11 || k == 14) return 0;              // pedestal + base brackets (J1-coaxial)
    if (k == 13) return 1;                                   // carousel / S-axis casting
    if (k == 12) return 2;                                   // upper arm (J2 journal + J3 bore)
    return 3;                                                // forearm + strut + bolts + wrist
}

// locate the FANUC STEP next to the working dir or the deployed assets folder
std::string findStep() {
    const char* cands[] = {
        "assets/FANUC-430 Robot.STEP",
        "build/release/assets/FANUC-430 Robot.STEP",
        "../assets/FANUC-430 Robot.STEP",
    };
    std::error_code ec;
    for (const char* c : cands) if (std::filesystem::exists(c, ec)) return c;
    return cands[0];
}

} // namespace

bool runVisibleArticGateV()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[vassign] GATE V (V.1 / V-assign) - 17-solid -> serial-link assignment correctness\n");

    // ---- import the real STEP -> 17 entities with auto-detected B-Rep bores ----
    // ONE scene holds both the solids and the live articulation, so the V.3 writeback
    // (link pose -> solid TransformComponent) can be exercised end-to-end (V2).
    Scene scene;
    const std::string path = findStep();
    krs::cad::ImportResult ir = krs::cad::importStep(scene, path, 0.001f);
    if (!ir.ok) { printf("[vassign] FAIL: STEP import failed (%s): %s\n", path.c_str(), ir.message.c_str()); return false; }
    if (ir.solids != 17) { printf("[vassign] FAIL: expected 17 solids, got %d (%s)\n", ir.solids, path.c_str()); return false; }

    auto& reg = scene.getRegistry();
    std::array<Solid, 17> S;
    int assignedSolids = 0;
    for (auto e : reg.view<TagComponent>()) {
        const std::string& tag = reg.get<TagComponent>(e).tag;
        const std::string pfx = "STEP solid ";
        if (tag.rfind(pfx, 0) != 0) continue;
        const int N = std::atoi(tag.c_str() + pfx.size());   // 1-indexed
        const int k = N - 1;
        if (k < 0 || k >= 17) continue;
        S[k].present = true;
        S[k].ent = e;
        ++assignedSolids;
        if (reg.any_of<AttachmentComponent>(e)) {
            for (const auto& f : reg.get<AttachmentComponent>(e).frames) {
                const glm::dvec3 A0(f.localAxis.x, f.localAxis.y, f.localAxis.z);
                const double an = glm::length(A0);
                if (an < 1e-9) continue;
                S[k].bores.push_back({ glm::dvec3(f.localPosition.x, f.localPosition.y, f.localPosition.z),
                                       A0 / an, double(f.radius) });
            }
        }
    }
    // V1 coverage: every imported solid present + maps to a valid link
    bool coverage = (assignedSolids == 17);
    for (int k = 0; k < 17; ++k) if (S[k].present) { const int L = assignLink(k); if (L < 0 || L > 3) coverage = false; }
    printf("[vassign]  V1 coverage: %d/17 solids present, all -> link in [0,3]  %s\n",
           assignedSolids, coverage ? "PASS" : "FAIL");

    // ---- build the canonical serial articulation (A1 4-link, GATE-H/D validated) ----
    RobotArticSpec ps; ps.fixBase = true;
    auto addJ = [&](int parent, float ax, float ay, float az, float px, float py, float pz) {
        ArticJointSpec j; j.parent = parent; j.revolute = true;
        j.axis = { ax, ay, az };
        j.Rtree = { 1,0,0, 0,1,0, 0,0,1 };
        j.ptree = { px, py, pz };
        j.mass = 1.0f; j.com = { 0,0,0 }; j.inertiaDiag = { 0.1f, 0.1f, 0.1f };
        ps.joints.push_back(j);
    };
    addJ(-1, 0,1,0,  0.f,    0.f,   0.f);     // J1 base yaw  (Y @ origin)
    addJ( 0, 1,0,0,  0.f,    0.74f, 0.305f);  // J2 shoulder  (X @ 0.74,0.305)
    addJ( 1, 1,0,0,  0.f,    1.075f,0.f);     // J3 elbow     (X, +1.075 from J2 => world 1.815,0.305)
    addJ( 2, 0,0,1,  0.f,    0.25f, 0.f);     // J4 wrist roll (Z) -- present but FROZEN

    SimulationController sim(&scene);
    sim.setRobotArticulationSpec(ps);
    sim.play();
    if (sim.articDofCount() != 4) { printf("[vassign] FAIL: live articulation dof=%d (expected 4)\n", sim.articDofCount()); sim.stop(); return false; }
    sim.setSceneGravity(0,0,0);

    // rest link poses at q=0 (the STEP assembly pose the meshes are baked in)
    { std::vector<float> q0(4, 0.f); sim.setArticJointPositions(q0); }
    auto p0 = sim.articLinkPoses();                          // [J1body, J2body, J3body, J4body]
    if (int(p0.size()) != 4) { printf("[vassign] FAIL: articLinkPoses size=%d\n", int(p0.size())); sim.stop(); return false; }

    // ---- V.3 mapping: each MOVING link (0-based) -> its solid entities; rest = q=0 ----
    // movingLink m drives serial link (m+1): m0=J1 carousel, m1=J2 upper arm, m2=J3 forearm
    // group, m3=J4 (frozen, no solids). Base solids (link 0) are unmapped -> stay at rest.
    std::vector<std::vector<entt::entity>> movingLinkEntities(4);
    for (int k = 0; k < 17; ++k) {
        if (!S[k].present) continue;
        const int L = assignLink(k);
        if (L >= 1 && L <= 4) movingLinkEntities[L - 1].push_back(S[k].ent);
    }
    sim.setArticulationVizMapping(movingLinkEntities);       // captures rest link poses
    std::array<DPose,4> rest;                                // index = link 0..3
    rest[0] = DPose{};                                       // link 0 = fixed base (never moves)
    rest[1] = fromArr(p0[0]); rest[2] = fromArr(p0[1]); rest[3] = fromArr(p0[2]);

    // sweep J1/J2/J3 (J4 frozen). At each pose capture the canonical link delta AND
    // exercise the live V.3 writeback; V2 = the writeback's TransformComponents must
    // match the canonical delta (from articLinkPoses) to <1e-6.
    const int T = 40;
    std::vector<std::array<DPose,4>> dl(T);
    double v2Err = 0.0;
    for (int t = 0; t < T; ++t) {
        const double a = double(t) / (T - 1);
        std::vector<float> q = { float(0.6*a), float(0.5*a), float(0.8*a), 0.f };
        sim.setArticJointPositions(q);
        auto pp = sim.articLinkPoses();
        if (int(pp.size()) != 4) { printf("[vassign] FAIL: articLinkPoses size=%d mid-sweep\n", int(pp.size())); sim.stop(); return false; }
        dl[t][0] = DPose{};                                  // base stays identity
        dl[t][1] = deltaOf(fromArr(pp[0]), rest[1]);
        dl[t][2] = deltaOf(fromArr(pp[1]), rest[2]);
        dl[t][3] = deltaOf(fromArr(pp[2]), rest[3]);

        sim.writeBackArticulationViz();                      // V.3: drive solid TransformComponents
        // V2 measures the RENDERED world-position error: the renderer applies the
        // TransformComponent to the (world-baked) vertices, so we compare a few real
        // points on each solid (its bore endpoints) transformed by the written
        // component vs by the canonical link delta. Meters, float-storage-limited.
        for (int k = 0; k < 17; ++k) {
            if (!S[k].present) continue;
            const int L = assignLink(k);
            if (L < 1 || L > 3) continue;                    // base solids unmapped (stay at rest)
            const auto& xf = reg.get<TransformComponent>(S[k].ent);
            const glm::dquat xq(double(xf.rotation.w), double(xf.rotation.x), double(xf.rotation.y), double(xf.rotation.z));
            const glm::dvec3 xt(xf.translation.x, xf.translation.y, xf.translation.z);
            for (const Bore& b : S[k].bores) {
                for (double off : { 0.0, 0.1 }) {            // two points on the bore axis -> position + angle
                    const glm::dvec3 x = b.P0 + off * b.A0;
                    const glm::dvec3 wWrite = xq * x + xt;            // what the renderer draws
                    const glm::dvec3 wCanon = applyPt(dl[t][L], x);   // canonical link delta
                    v2Err = std::max(v2Err, glm::length(wWrite - wCanon));
                }
            }
        }
    }
    sim.stop();
    // V2 bound 1e-5 m: the whole articulation pipeline is single-precision (PhysX
    // float), so at the FANUC's ~3 m reach a float32 transform bottoms out at the
    // ULP floor (~1.1e-6 m here) -- the same float precision H1/D1 hit at ~1.3e-6
    // under their 1e-4 bounds. 1e-5 is 10x the float floor and still rejects any
    // real writeback bug (wrong link index / quaternion order fails by 0.1-3 m).
    const bool v2 = v2Err < 1e-5;
    printf("[vassign]  V2 writeback world-pos vs canonical link delta: maxErr=%.3e m (bound 1e-05, float floor ~1.1e-6)  %s\n",
           v2Err, v2 ? "PASS" : "FAIL");

    // ---- shared hinges: coaxial bore pairs across DIFFERENT solids (rest pose) ----
    std::vector<Hinge> hinges;
    for (int kA = 0; kA < 17; ++kA) {
        if (!S[kA].present) continue;
        for (int kB = kA + 1; kB < 17; ++kB) {
            if (!S[kB].present) continue;
            for (int iA = 0; iA < int(S[kA].bores.size()); ++iA)
            for (int iB = 0; iB < int(S[kB].bores.size()); ++iB) {
                const Bore& bA = S[kA].bores[iA];
                const Bore& bB = S[kB].bores[iB];
                if (std::abs(glm::dot(bA.A0, bB.A0)) < 0.999) continue;            // parallel axes
                if (ptLine(bB.P0, bA.P0, bA.A0) > 0.005) continue;                 // <5 mm line distance
                hinges.push_back({ kA, iA, kB, iB });
            }
        }
    }

    // ---- metric over an assignment: max hinge coincidence DRIFT + max joint sweep ----
    // dev(t) = axis-line separation of the two bores carried by their assigned links.
    // We score the DRIFT from rest, |dev(t) - dev(0)|, so the bore-match imperfection
    // (each pair is only coaxial to within the 5 mm match tol) cancels out and the
    // number is the purely MOTION-induced incoherence: ~0 for a correct assignment,
    // huge when a solid is carried by the wrong link.
    auto metric = [&](const std::array<int,17>& asn, double& coincDrift, double& jointMot) {
        coincDrift = 0.0; jointMot = 0.0;
        for (const Hinge& h : hinges) {
            const int LA = asn[h.kA], LB = asn[h.kB];
            const Bore& bA = S[h.kA].bores[h.iA];
            const Bore& bB = S[h.kB].bores[h.iB];
            double sweep = 0.0, dev0 = 0.0;
            for (int t = 0; t < T; ++t) {
                const glm::dvec3 PA = applyPt (dl[t][LA], bA.P0);
                const glm::dvec3 DA = glm::normalize(applyDir(dl[t][LA], bA.A0));
                const glm::dvec3 PB = applyPt (dl[t][LB], bB.P0);
                const glm::dvec3 DB = glm::normalize(applyDir(dl[t][LB], bB.A0));
                const double dev = std::max(ptLine(PB, PA, DA),
                                            ptLine(PB + 0.5 * DB, PA, DA));        // position + angular drift
                if (t == 0) dev0 = dev;
                coincDrift = std::max(coincDrift, std::abs(dev - dev0));
                if (LA != LB) sweep = std::max(sweep, relAngle(dl[t][LA], dl[t][LB]));
            }
            if (LA != LB) jointMot = std::max(jointMot, sweep);
        }
    };

    std::array<int,17> asn{};
    for (int k = 0; k < 17; ++k) asn[k] = assignLink(k);

    double coincDrift = 0, jointMot = 0;
    metric(asn, coincDrift, jointMot);
    // V-assign positive: bores stay coherent under motion (drift <1 mm) AND the joints
    // really move (>0.2 rad). The 1 mm bound is motion-induced drift, NOT absolute
    // coincidence, so it is purely numerical for a correct assignment.
    const double DRIFT_TOL = 0.001, MOTION_FLOOR = 0.2;
    const bool vCoherent = coincDrift < DRIFT_TOL;
    const bool vMotion   = jointMot   > MOTION_FLOOR;
    printf("[vassign]  V-assign (correct): coincDrift=%.3e m (bound %.0e), jointSweep=%.3f rad (floor %.2f), hinges=%d  %s\n",
           coincDrift, DRIFT_TOL, jointMot, MOTION_FLOOR, int(hinges.size()), (vCoherent && vMotion) ? "PASS" : "FAIL");

    // NEGATIVE CONTROL: weld a WRIST solid (15) onto the upper arm (link 2). The
    // wrist hinges it shares with the forearm (link 3) must then DRIFT as the elbow
    // bends -> coincDrift spikes far past the bound. If it did NOT, the witness would
    // be vacuous and V-assign must fail.
    std::array<int,17> bad = asn; bad[15] = 2;               // solid 15 (wrist) -> wrong link
    double driftBad = 0, motBad = 0;
    metric(bad, driftBad, motBad);
    const bool guardBites = driftBad > 0.10;                 // a real mis-assignment is grossly rejected
    printf("[vassign]  neg-ctrl (solid15->link2): coincDrift=%.3e m -> guard %s\n",
           driftBad, guardBites ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = coverage && v2 && vCoherent && vMotion && guardBites && !hinges.empty();
    printf("[vassign] %s\n", pass ? "ALL PASS (V1 + V2 + V-assign + neg-ctrl)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::dyn
#endif // KR_WITH_PHYSX && KR_WITH_OCCT
