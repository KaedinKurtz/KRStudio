#pragma once
// GraspFilter.hpp -- the mesh-validity + graspability FILTER (real-metric, meters). A large uncurated library
// re-introduces contamination (broken / mis-scaled / non-graspable meshes); this filter decides which objects
// are admissible BEFORE any grasping, so the generalized success rate is over VALID objects only and the
// rejected fraction is a separate DATASET-QUALITY statistic, NOT counted as grasp failures. Shared by GATE
// FILTER (reports the rejection breakdown) and GATE GENERALIZE (grasps only the survivors).
#include "GraspMesh.hpp"

namespace krs::grasp {

// rejection reasons (F_VALID = admissible). Checked in this order; the first failing test wins.
enum FilterResult { F_VALID = 0, F_LOADFAIL, F_DEGENERATE, F_NONWATERTIGHT, F_OUT_OF_SCALE, F_NON_GRASPABLE, F_COUNT };
inline const char* filterName(int r) {
    static const char* n[F_COUNT] = { "VALID", "LOAD_FAIL", "DEGENERATE", "NON_WATERTIGHT", "OUT_OF_SCALE", "NON_GRASPABLE" };
    return (r >= 0 && r < F_COUNT) ? n[r] : "?";
}

// graspable-scale band (longest axis, m), max graspable width (shortest axis must fit the jaw), and the
// boundary tolerance for a cookable collider / reliable mass. Justified: a parallel jaw that opens to ~0.165 m
// (minus pads/clearance) can grip a width up to ~0.12 m; below 2 cm or above 40 cm is not a tabletop grasp.
constexpr double kFilterScaleMinM = 0.02;   // sub-centimetre (a screw) -> reject
constexpr double kFilterScaleMaxM = 0.40;   // building / furniture scale -> reject
constexpr double kFilterMaxWidthM = 0.12;   // shortest axis must fit the gripper on at least one axis
constexpr double kFilterMaxBoundary = 0.10; // > 10% open edges -> unreliable collider / mass

// classify a LOADED mesh's metrics (caller maps a load failure to F_LOADFAIL separately).
inline FilterResult classifyGraspable(const MeshMetrics& mm) {
    if (!mm.finite || mm.nTris < 100 || mm.volume < 1e-7)                     return F_DEGENERATE;     // broken / empty
    if (mm.longest < kFilterScaleMinM || mm.longest > kFilterScaleMaxM)       return F_OUT_OF_SCALE;   // mis-scaled
    if (mm.shortest > kFilterMaxWidthM)                                       return F_NON_GRASPABLE;  // too fat to grip
    if (mm.signedVolume <= 0.0 || mm.boundaryFrac > kFilterMaxBoundary)       return F_NONWATERTIGHT;  // not cookable
    return F_VALID;
}

} // namespace krs::grasp
