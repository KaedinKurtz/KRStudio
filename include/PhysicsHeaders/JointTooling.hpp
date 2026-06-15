#pragma once
// JointTooling.hpp -- SINGLE SOURCE OF TRUTH for deriving a joint frame from selected B-Rep features
// (Phase 3 GATE J). A revolute joint mated to two coaxial cylindrical bores has its axis = the bores'
// common axis; the origin is any point on that line. The derivation reads the EXACT analytic
// parameters carried by BRepFace (GATE F), so the frame is correct to machine precision -- not a mesh
// fit. A non-coaxial / non-cylindrical pair is REJECTED (no valid revolute), which is the validation
// the tooling and GATE J's negative control rely on. The result is meant to be written straight into
// the canonical krs::dyn::RobotArticSpec (rule 6: one articulation graph), never a parallel rep.

#include <glm/glm.hpp>
#include "components.hpp"   // BRepFace

namespace krs::joint {

struct JointFrame {
    glm::vec3 axisPos{ 0.0f };           // a point on the joint axis (world/part metres)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f }; // unit axis direction
};

// Derive a revolute joint frame from two cylindrical bore features. Returns false (no joint) if
// either feature is not a cylinder, the axes are not parallel, or the axis lines are not collinear
// (perpendicular offset > coaxTol). `coaxAngleErr`/`coaxOffset` report the parallelism + collinearity
// residuals for the caller's diagnostics.
inline bool deriveRevoluteFromBores(const BRepFace& a, const BRepFace& b, JointFrame& out,
                                    double coaxTol = 1.0e-4, double* coaxAngleErr = nullptr,
                                    double* coaxOffset = nullptr)
{
    if (a.type != 1 || b.type != 1) return false;        // both must be cylinders
    glm::vec3 da = glm::normalize(a.axisDir);
    glm::vec3 db = glm::normalize(b.axisDir);
    if (glm::dot(da, db) < 0.0f) db = -db;               // align sense
    const double angErr = 1.0 - double(glm::dot(da, db)); // 0 when perfectly parallel
    if (coaxAngleErr) *coaxAngleErr = angErr;
    if (angErr > coaxTol) return false;                  // axes not parallel
    const glm::vec3 dir = glm::normalize(da + db);       // mated axis (average of the two)
    // collinearity: perpendicular distance from b's axis point to a's axis line.
    const glm::vec3 w = b.axisPos - a.axisPos;
    const glm::vec3 perp = w - glm::dot(w, dir) * dir;
    const double off = double(glm::length(perp));
    if (coaxOffset) *coaxOffset = off;
    if (off > coaxTol) return false;                     // axis lines offset -> not coaxial
    out.axisDir = dir;
    // origin: midpoint of the two bore reference points, projected onto the common line.
    const glm::vec3 mid = 0.5f * (a.axisPos + b.axisPos);
    out.axisPos = a.axisPos + glm::dot(mid - a.axisPos, dir) * dir;
    return true;
}

} // namespace krs::joint
