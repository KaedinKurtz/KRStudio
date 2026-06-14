#include "VisibleArticGate.hpp"

// The V-assign gate needs BOTH the live articulation (PhysX) and the imported
// STEP solids with their B-Rep bores (OpenCASCADE). Without either, there is
// nothing to validate -> vacuous pass.
#if !defined(KR_WITH_PHYSX) || !defined(KR_WITH_OCCT)
namespace krs::dyn { bool runVisibleArticGateV() { return true; } bool runFanucBootGateV6() { return true; } }
#else

#include "ArticulationSpec.hpp"
#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "CadImporter.hpp"
#include "FanucArticulation.hpp"   // SINGLE SOURCE OF TRUTH for the solid->link assignment + setup

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

// NOTE: the solid->link assignment + the canonical spec + the scene setup live in
// krs::fanuc (FanucArticulation.hpp) -- the SINGLE SOURCE OF TRUTH shared with the
// app boot scene. This gate consumes that helper so it validates exactly what boots.

} // namespace

bool runVisibleArticGateV()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[vassign] GATE V (V.1 / V-assign) - 17-solid -> serial-link assignment correctness\n");

    // ---- set up the FANUC via the SHARED helper -- the SAME path the app boots ----
    // ONE scene holds both the solids and the live articulation, so the V.3 writeback
    // (link pose -> solid TransformComponent) is exercised end-to-end (V2).
    Scene scene;
    SimulationController sim(&scene);
    const std::string path = krs::fanuc::findStepAsset();
    krs::fanuc::Setup setup = krs::fanuc::setupFanucScene(scene, sim, path);
    if (!setup.ok) { printf("[vassign] FAIL: setupFanucScene (%s): %s\n", path.c_str(), setup.message.c_str()); sim.stop(); return false; }
    auto& reg = scene.getRegistry();

    // SINGLE-SOURCE-OF-TRUTH assert: the booted assignment must equal the canonical map
    // AND a frozen expected fingerprint. If solidLink() is ever edited (or the app grows
    // its own override), this trips rather than silently rendering a wrong arm.
    static const char* kExpectedFingerprint = "fanuc-v1:33333333333021030";
    const bool fpMatch = (setup.fingerprint == krs::fanuc::assignmentFingerprint())
                      && (setup.fingerprint == kExpectedFingerprint);
    printf("[vassign]  V-source fingerprint: setup=%s canonical=%s expected=%s  %s\n",
           setup.fingerprint.c_str(), krs::fanuc::assignmentFingerprint().c_str(), kExpectedFingerprint,
           fpMatch ? "PASS" : "FAIL");

    // collect the independent witness (B-Rep bores) per solid from the helper's entities
    std::array<Solid, 17> S;
    int assignedSolids = 0;
    for (int k = 0; k < krs::fanuc::kSolidCount; ++k) {
        const entt::entity e = setup.solidEntity[k];
        if (e == entt::null || !reg.valid(e)) continue;
        S[k].present = true; S[k].ent = e; ++assignedSolids;
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
    bool coverage = (assignedSolids == 17);
    for (int k = 0; k < 17; ++k) if (S[k].present) { const int L = krs::fanuc::solidLink(k); if (L < 0 || L > 3) coverage = false; }
    printf("[vassign]  V1 coverage: %d/17 solids present, all -> link in [0,3]  %s\n",
           assignedSolids, coverage ? "PASS" : "FAIL");

    // rest link poses at q=0 (setupFanucScene left the arm at its STEP assembly pose)
    auto p0 = sim.articLinkPoses();                          // [J1body, J2body, J3body, J4body]
    if (int(p0.size()) != 4) { printf("[vassign] FAIL: articLinkPoses size=%d\n", int(p0.size())); sim.stop(); return false; }
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
            const int L = krs::fanuc::solidLink(k);
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
    for (int k = 0; k < 17; ++k) asn[k] = krs::fanuc::solidLink(k);

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

    const bool pass = coverage && fpMatch && v2 && vCoherent && vMotion && guardBites && !hinges.empty();
    printf("[vassign] %s\n", pass ? "ALL PASS (V-source + V1 + V2 + V-assign + neg-ctrl)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE V.6 — boot path: the SAME shared helper + demo drive + tick loop the app
// boots with must make the FANUC visibly move (and the assignment fingerprint
// must match the canonical map). Proves the boot path and the gate path are one.
// ===========================================================================
bool runFanucBootGateV6()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fanuc boot] GATE V.6 - boot path (shared helper + demo drive + tick) makes the FANUC move\n");

    const std::string path = krs::fanuc::findStepAsset();
    const entt::entity FORE_IDX = entt::null; (void)FORE_IDX;

    // helper to set up + tick N frames, returning the forearm's net translation + finiteness
    auto run = [&](bool driveOn, float& moved, bool& finite, std::string& fp) -> bool {
        Scene scene; SimulationController sim(&scene);
        krs::fanuc::Setup s = krs::fanuc::setupFanucScene(scene, sim, path);
        if (!s.ok) { printf("[fanuc boot] FAIL: setupFanucScene: %s\n", s.message.c_str()); sim.stop(); return false; }
        fp = s.fingerprint;
        auto& reg = scene.getRegistry();
        const entt::entity fore = s.solidEntity[1];        // inspect idx 1 = forearm (link 3)
        const glm::vec3 p0 = reg.get<TransformComponent>(fore).translation;
        sim.setArticulationDemoDrive(driveOn);
        for (int i = 0; i < 60; ++i) sim.tick();           // the app's per-frame call
        const glm::vec3 p1 = reg.get<TransformComponent>(fore).translation;
        finite = std::isfinite(p1.x) && std::isfinite(p1.y) && std::isfinite(p1.z);
        moved = glm::length(p1 - p0);
        sim.stop();
        return true;
    };

    float movedOn = 0, movedOff = 0; bool finOn = false, finOff = false; std::string fpOn, fpOff;
    if (!run(true,  movedOn,  finOn,  fpOn))  return false;
    if (!run(false, movedOff, finOff, fpOff)) return false;

    const bool fpMatch = (fpOn == krs::fanuc::assignmentFingerprint());
    const bool moves   = movedOn > 0.05f && finOn;         // forearm translates >5 cm over 60 ticks
    const bool ctrl    = movedOff < 1e-4f;                 // NEGATIVE CONTROL: no drive -> no motion
    printf("[fanuc boot]  fingerprint=%s (%s); demoDrive: forearm moved=%.3f m finite=%d; neg-ctrl(no drive) moved=%.3e m\n",
           fpOn.c_str(), fpMatch ? "canonical" : "MISMATCH", movedOn, int(finOn), movedOff);
    const bool pass = fpMatch && moves && ctrl;
    printf("[fanuc boot] %s\n", pass ? "ALL PASS (boot path moves the FANUC; fingerprint canonical; neg-ctrl still)"
                                     : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::dyn
#endif // KR_WITH_PHYSX && KR_WITH_OCCT
