#pragma once
// GraspPlanner.hpp -- a TUNEABLE HEURISTIC parallel-jaw grasp planner (pure geometry, no PhysX). It samples
// surface points, shoots an inward ray to find the opposing surface (krs::pick::rayTriangle), keeps pairs whose
// outward normals are nearly opposed (antipodal => inside the friction cone), and scores them by normal
// alignment, distance of the grasp line from the centre of mass (stability), and width. The output is a ranked
// set of GraspSpec the LOCKED runGripperSim then judges -- the planner is the ONLY thing we tune; the test is
// never touched. A baseline (loose) and a tuned (tight, CoM-aware) parameter set let the gate show the honest
// success RATE improve from one to the other, against a random-grasp negative control.
#include "GraspSim.hpp"   // GraspSpec
#include "GraspMesh.hpp"  // MeshMetrics
#include "components.hpp" // RenderableMeshComponent
#include <vector>

namespace krs::grasp {

// Every knob here is a PLANNER parameter (never a physics/criterion parameter). Tuning these is the sprint's
// whole job; none of them can change what counts as a success.
struct PlannerParams {
    int   surfaceSampleCount    = 400;     // # surface points probed for an opposing face
    float antipodalToleranceDeg = 20.0f;   // max deviation of the two outward normals from perfectly opposed
    float minJawSpanM           = 0.01f;   // reject pairs narrower than this (noise / degenerate); 1cm admits thin shafts
    float maxJawSpanM           = 0.165f;  // reject pairs wider than the jaw can open (span <= ~maxJaw - clearance)
    float jawClearanceM         = 0.035f;  // initial gap added on top of pair width + pad thickness; enough room
                                           // for BOTH fingers to approach symmetrically (a tight start contacts one
                                           // side first and kicks the free object out)
    float alignWeight           = 1.0f;    // reward perfectly-opposed normals (force closure)
    float comPerpWeight         = 0.0f;    // penalise grasp-line distance from the CoM (anti-gravity-torque), normalised; tuned raises this
    float verticalPenaltyWeight = 0.0f;    // penalise VERTICAL closing axes: a tabletop parallel-jaw gripper cannot
                                           // slide its lower jaw UNDER a grounded object, so a top/bottom grasp never
                                           // seats -- the heuristic must grip from the sides (closing axis horizontal)
    float widthWeight           = 0.0f;    // mild penalty on wide grasps (snugger = more stable)
    // --- V2 terms (each maps to a CHARACTERISED failure mode; 0 = the term is off, i.e. V1 behaviour) ---
    float rimMinSpanM           = 0.0f;    // >0 enables RIM / thin-wall pinch grasps (targets NO_ANTIPODAL_GRASP on
                                           // open thin shells -- bowls/cups): admit pairs with pairDist in
                                           // [rimMinSpanM, minJawSpanM) that the normal pass rejects as too thin
    float rimClearanceM         = 0.012f;  // (smaller) initial gap for a rim pinch -- the inner jaw enters the cavity
    float aboveComWeight        = 0.0f;    // penalise grasps BELOW the CoM (targets DRIFT_ROTATE): a lifted object
                                           // gripped above its CoM hangs as a stable pendulum; gripped below it is
                                           // top-heavy and tips/drifts out of the success window
    int   maxGrasps             = 3;       // distinct grasps returned per object
    float diversityM            = 0.02f;   // min grasp-centre separation between returned grasps
};

PlannerParams baselinePlannerParams();   // loose normals, few samples, CoM-blind  -> a modest success rate
PlannerParams tunedPlannerParams();      // tight normals, more samples, CoM-aware  -> the improved rate (V1)
PlannerParams tunedV2PlannerParams();    // V1 + rim/thin-wall grasps + snug-fit + stronger CoM (targets the failure modes)

// Plan up to params.maxGrasps spatially-distinct antipodal grasps for `mesh`, best-scored first. Pure CPU
// geometry; returns specs in the object's resting frame (centreOffset relative to the resting CoM), ready for
// runGripperSim. May return fewer than maxGrasps (or none) when the object affords few antipodal pairs -- those
// missing attempts are honestly counted as planner failures by the gate.
std::vector<GraspSpec> planAntipodal(const RenderableMeshComponent& mesh, const MeshMetrics& mm, const PlannerParams& p);

// Negative control: a uniformly-random grasp (random centre in the AABB, uniform-on-SO(3) approach, random
// span), deterministically seeded. No antipodal reasoning -> should score far below the heuristic.
GraspSpec randomGrasp(const MeshMetrics& mm, unsigned seed);

} // namespace krs::grasp
