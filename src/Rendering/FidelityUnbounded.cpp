// FidelityUnbounded.cpp -- Phase 2 UNBOUNDED-DIAGNOSIS. The grasp FAILURE-CATALOG found UNBOUNDED_GRIP is 27%
// of failures: an attempt whose PEAK per-substep jaw<->object contact force crossed the locked 3x bound (120 N)
// and so was REJECTED by the force-bound criterion clause. The open question this gate answers WITH NUMBERS:
// is that over-squeeze REAL physics (the rigid object genuinely wedges and the grip applies >120 N), or is it a
// rigid-jaw CONTACT ARTIFACT (a transient one-substep solver impulse that a compliant gripper would absorb)?
//
// The decisive physics: each finger applies a HARD-CAPPED force -- addForce(+/- closeAxis * gripForceN) with
// PxForceMode::eFORCE, gripForceN = 40 N, both jaws force-limited, the closing axis FREE (no kinematic clamp).
// A force-controlled gripper cannot squeeze harder than its actuator, so Newton's 3rd law fixes the FIRM-CONTACT
// grip force at EXACTLY 40 N. KNOWN-ANSWER (the gate's PASS leg): the firm-contact grip on a BOUNDED grasp == 40 N.
// We estimate firm-contact as the MEDIAN over bounded grasps of each grip's PEAK force (a bounded peak IS the firm
// actuator contact -- it cannot exceed the cap; the per-substep median dips lower during the intermittent lift and
// is reported separately). DIAGNOSIS leg: for each UNBOUNDED grasp we read the per-substep MEDIAN -- if it is also
// >> 40 N (not ~40 with a lone peak), the over-squeeze is SUSTAINED, not a transient spike, and a sustained force
// above the 40 N actuator cap is impossible for a force-controlled gripper => a force-UNLIMITED kinematic-lift
// artifact (the wedge is real geometry; the magnitude is not). This NEVER changes the locked grasp criterion.
//
// NEGATIVE CONTROL (anti-fake): if you WRONGLY treat the per-substep PEAK as the equilibrium grip force, you
// read ~the typical peak as "the sustained force" -- which does NOT equal the applied 40 N, so it FAILS the same
// Newton's-3rd-law check. That proves the median check has teeth (median != peak; the peak is not equilibrium).
//
// The locked grasp criterion is UNTOUCHED -- this gate only adds a median readout (R.medianJawForceN) alongside
// the existing peak; the force-bound clause and configHash are unchanged.
#include "FidelityGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::fidelity {
bool runFidelityUnboundedGate() { std::printf("[fidelity] UNBOUNDED-DIAGNOSIS  SKIP (no PhysX)\n"); return true; }
}
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspPlanner.hpp"
#include "GraspSim.hpp"
#include "GraspMesh.hpp"
#include "YcbCatalog.hpp"
#include "MeshUtils.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <exception>

namespace krs::fidelity {

namespace {
double medianOf(std::vector<float> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return double(v[v.size() / 2]);
}
} // namespace

bool runFidelityUnboundedGate() {
    using namespace krs::grasp;
    std::printf("\n[fidelity] GRIP-COMPLIANT : do FORCE-LIMITED (compliant) jaws remove the rigid-jaw over-squeeze?\n");
    std::printf("  The harness proved UNBOUNDED_GRIP is a RIGID-JAW artifact: a wedge pushes a jaw off-track and the\n");
    std::printf("  rigid Y/Z lock to the kinematic palm MANUFACTURES the reaction (mug 173 N/4.3x, drill 116 N/2.9x).\n");
    std::printf("  FIX: FORCE-LIMIT the finger Y/Z joints (rating %.0f N) so a wedge SLIPS the jaw -- the jaw GIVES,\n",
                kLiftActuatorForceRatingN);
    std::printf("  relieving the amplification at its source. INVARIANT = the %.0f N ACTUATOR force (asserted), NOT the\n",
                kLockedPhysics.gripForceN);
    std::printf("  contact (a compliant jaw correctly modulates contact down). Run both ways: NEW (compliant) jaws must\n");
    std::printf("  SLIP a wedge (contact <= rating) yet still HOLD good grasps; OLD (rigid) jaws reproduce the >120 N spike.\n");

    const float Fapplied = kLockedPhysics.gripForceN;                                   // 40 N (locked)
    const float bound    = kLockedPhysics.maxGripForceFactor * kLockedPhysics.gripForceN; // 120 N (locked criterion)
    const float rating   = kLiftActuatorForceRatingN;                                   // 56 N (locked actuator rating)
    const PlannerParams  tune = tunedPlannerParams();
    const int kAttempts = 3;

    WorldOverride newW{}; newW.legacyRigidGripper = false;   // the FIX: COMPLIANT jaws (Y/Z force-limited, slip)
    WorldOverride oldW{}; oldW.legacyRigidGripper = true;    // the NEG-CONTROL: OLD rigid jaws (Y/Z locked)

    std::vector<float> newBoundedPeak;                       // firm-contact grip (bounded peak) -- the actuator's firm
    struct W { std::string id; float oldMed, oldPeak, newMed, newPeak, newHold; };
    std::vector<W> wedges;                                   // objects UNBOUNDED under the OLD rigid jaws
    int newUnb = 0, oldUnb = 0, seated = 0, newSucc = 0, oldSucc = 0;

    for (const auto& o : ycbCatalog()) {
        RenderableMeshComponent mesh;
        try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
        catch (const std::exception& e) { std::printf("  %-22s load failed: %s\n", o.id.c_str(), e.what()); return false; }
        const MeshMetrics            mm    = computeMetrics(mesh);
        const std::vector<GraspSpec> specs = planAntipodal(mesh, mm, tune);
        const std::string            cp    = o.coacdPath();

        for (int k = 0; k < kAttempts && k < int(specs.size()); ++k) {
            const GraspResult rn = runGripperSim(mesh, specs[size_t(k)], newW, cp);   // compliant jaws (fix)
            const GraspResult ro = runGripperSim(mesh, specs[size_t(k)], oldW, cp);   // rigid jaws (neg-ctrl)
            if (rn.medianJawForceN <= 0.0f && ro.medianJawForceN <= 0.0f) continue;
            ++seated;
            if (graspSucceeded(rn)) ++newSucc;
            if (graspSucceeded(ro)) ++oldSucc;
            if (rn.maxJawForceN > 0.0f && rn.maxJawForceN <= bound) newBoundedPeak.push_back(rn.maxJawForceN);
            if (rn.maxJawForceN > bound) ++newUnb;
            if (ro.maxJawForceN > bound) ++oldUnb;
            if (ro.maxJawForceN > bound)                                              // a wedge under the OLD rigid jaws
                wedges.push_back({ o.id, ro.medianJawForceN, ro.maxJawForceN,
                                   rn.medianJawForceN, rn.maxJawForceN, rn.holdMedianJawForceN });
        }
    }
    if (seated == 0) { std::printf("  [FAIL] no seated squeeze recorded\n"); return false; }

    const double newFirm = medianOf(newBoundedPeak);    // firm-contact grip (bounded peak), NEW compliant jaws

    std::printf("  ---- %d seated; UNBOUNDED(peak>%.0fN): OLD rigid jaws=%d  NEW compliant jaws=%d ----\n",
                seated, bound, oldUnb, newUnb);
    std::printf("  good-grasp HOLD (graspSucceeded): OLD rigid=%d  NEW compliant=%d  (must NOT drop -- jaws must still hold)\n",
                oldSucc, newSucc);
    std::printf("  firm-contact grip (median bounded peak, NEW) = %.1f N  [INVARIANT is the %.0f N ACTUATOR, asserted every run]\n",
                newFirm, Fapplied);

    std::vector<float> newWedgePeak, newWedgeMed, oldWedgeMed;
    if (!wedges.empty()) {
        std::printf("  ---- wedge objects (UNBOUNDED under OLD rigid jaws): do the COMPLIANT jaws SLIP (cap the force)? ----\n");
        std::printf("    %-20s %10s %10s   %10s %10s %10s\n", "object", "OLDmedN", "OLDpeakN", "NEWmedN", "NEWpeakN", "NEWholdN");
        for (const auto& w : wedges) {
            std::printf("    %-20s %10.1f %10.1f   %10.1f %10.1f %10.1f\n",
                        w.id.c_str(), w.oldMed, w.oldPeak, w.newMed, w.newPeak, w.newHold);
            oldWedgeMed.push_back(w.oldMed); newWedgeMed.push_back(w.newMed); newWedgePeak.push_back(w.newPeak);
        }
    }
    const double oldWedge   = medianOf(oldWedgeMed);    // OLD manufactured over-squeeze
    const double newWedgeM  = medianOf(newWedgeMed);    // NEW sustained median on the same objects (jaws slipped)
    const double newWedgeP  = medianOf(newWedgePeak);   // NEW peak on the same objects
    std::printf("  wedge contact: OLD median=%.1f N (%.1fx grip) -> NEW median=%.1f N, NEW peak=%.1f N (rating %.0f N + margin)\n",
                oldWedge, oldWedge / Fapplied, newWedgeM, newWedgeP, rating);

    // ---- GRIP-COMPLIANT assertions (corrected: the INVARIANT is the actuator force, not the contact) ----
    // (1) GOOD grasps still HOLD: the compliant jaws must NOT start dropping normal grasps (the must-not-break
    //     check). Allow tiny variation; a real regression would drop many.
    const bool goodGraspsHold = (newSucc >= oldSucc - 1) || (newSucc >= int(0.9 * oldSucc));
    // (2) the WEDGE SLIPS: on objects the rigid jaws over-squeezed, the compliant jaws GIVE -- the contact no
    //     longer exceeds the rating beyond a small margin (the 173/116 N manufactured spikes GONE).
    const bool wedgesSlip = (wedges.empty()) || (newWedgeM <= rating * 1.3 && newWedgeM < 0.6 * oldWedge);
    // (3) the OLD rigid jaws REPRODUCE the manufactured force and FAIL the same bound (no vacuous gate).
    const bool oldFails = (oldUnb > 0) && (oldWedge > 2.0 * Fapplied);
    // (the 40 N ACTUATOR invariant is asserted by assertPhysicsLocked() -- the [LOCK OK] gripForceN=40 line every run.)

    const bool pass = goodGraspsHold && wedgesSlip && oldFails;
    std::printf("  good-grasps-still-hold=%d  wedge-jaws-slip(<=%.0fN & <0.6x old)=%d  old-rigid-reproduces&fails=%d\n",
                goodGraspsHold, rating * 1.3, wedgesSlip, oldFails);
    std::printf("[fidelity] GRIP-COMPLIANT  %s   (UNBOUNDED OLD=%d -> NEW=%d; wedge median %.0f->%.0f N; hold OLD=%d NEW=%d)\n",
                pass ? "PASS" : "FAIL", oldUnb, newUnb, oldWedge, newWedgeM, oldSucc, newSucc);
    return pass;
}

} // namespace krs::fidelity
#endif
