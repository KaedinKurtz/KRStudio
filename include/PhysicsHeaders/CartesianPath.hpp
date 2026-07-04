#pragma once
// ===========================================================================
// CARTESIAN PATH -- parametric SE(3) trajectory in TASK space (krs::path).
//
// A CartesianPath is pose(s) for s in [0,1]: a position curve x(s) PLUS an
// orientation curve R(s), decoupled so each is interpolated by its own correct
// scheme (positions on R^3, orientations on the unit-quaternion 3-sphere). This
// is the geometry the "revolve" and "path sweep" tools author; the motion stack
// (SerialChain::ik) then follows poseAt(s) to drive the arm. Everything here is
// pure Eigen / CPU -- no arm, no joints -- so it gates without a robot.
//
// WHY quaternion + SLERP, not Euler/matrix-lerp: matrix or Euler interpolation
// shears the frame and is path-dependent; unit-quaternion SLERP is the unique
// constant-angular-velocity shortest geodesic between two orientations. We force
// the shortest arc by flipping q to the hemisphere of its predecessor (a quat and
// its negation are the SAME rotation, so this is free) -- without it a 200-degree
// SLERP would wind the long way and the wrist would flip.
//
// PARAMETERIZATION: s is a NORMALIZED curve parameter, NOT arc length -- equal ds
// steps are not equal metres on a Catmull-Rom curve. arcLengthReparam() builds an
// s(u) map (u = normalized arc length) so poseAtArc(u) advances at ~constant
// Cartesian speed, which is what a real feed-rate controller needs.
//
// All SI: metres / radians.
// ===========================================================================
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <vector>
#include "RobotDynamics.hpp"   // krs::dyn::Pose (Matrix3d R + Vector3d p)

namespace krs::path {

// A control pose: position + orientation (orientation as a quaternion so the
// interpolator never sees a non-orthonormal matrix; normalized on ingest).
struct Waypoint {
    Eigen::Vector3d    p = Eigen::Vector3d::Zero();
    Eigen::Quaterniond q = Eigen::Quaterniond::Identity();
    Waypoint() = default;
    Waypoint(const Eigen::Vector3d& pos, const Eigen::Quaterniond& quat) : p(pos), q(quat) {}
};

// Position interpolation between waypoints.
//   Linear     : piecewise straight lines (C0) -- exact endpoints, corners at knots.
//   CatmullRom : C1 spline that PASSES THROUGH every waypoint (interpolating, not
//                approximating); the tool's default so edited waypoints are honored.
enum class PosInterp { Linear, CatmullRom };

class CartesianPath {
public:
    CartesianPath() = default;
    explicit CartesianPath(std::vector<Waypoint> wps, PosInterp posMode = PosInterp::CatmullRom);

    int  size()  const { return int(wps_.size()); }
    bool empty() const { return wps_.empty(); }
    const std::vector<Waypoint>& waypoints() const { return wps_; }
    PosInterp posMode() const { return posMode_; }

    // The knot parameter of waypoint i: s_i = i/(n-1), uniform in [0,1] (chord-
    // length knots are overkill for the gate; uniform makes "passes through wp i
    // at s_i" exactly checkable). Single-waypoint path: s_0 = 0.
    double knot(int i) const;

    // --- core evaluation ---------------------------------------------------
    // Pose at curve parameter s (clamped to [0,1]). Degenerate cases are safe:
    //   0 waypoints -> identity pose; 1 waypoint -> that waypoint's pose (no crash).
    krs::dyn::Pose poseAt(double s) const;
    Eigen::Vector3d    positionAt(double s) const;
    Eigen::Quaterniond orientationAt(double s) const;   // unit, shortest-arc

    // Unit tangent of the POSITION curve at s (direction of travel). Falls back to
    // a finite difference when the analytic derivative vanishes; zero vector only
    // for a genuinely stationary (single-point / coincident) path.
    Eigen::Vector3d tangentAt(double s) const;

    // --- arc length --------------------------------------------------------
    // Total Cartesian length of the position curve (adaptive polyline, `samples`
    // segments). Orientation is not counted -- this is metres of travel.
    double arcLength(int samples = 512) const;

    // Build the s(u) reparam table (u = normalized arc length in [0,1]); enables
    // poseAtArc/positionAtArc. Idempotent; call once after editing waypoints.
    void arcLengthReparam(int samples = 512);
    bool hasArcTable() const { return !arcS_.empty(); }
    // Curve parameter s for a given normalized arc length u in [0,1].
    double sOfArc(double u) const;
    // Pose / position at normalized arc length u (constant-speed sampling). If the
    // arc table was never built, falls back to poseAt(u) (parameter-space).
    krs::dyn::Pose  poseAtArc(double u) const;
    Eigen::Vector3d positionAtArc(double u) const;

    // --- factories ---------------------------------------------------------
    // Straight line a->b with SLERP a.q->b.q (2-waypoint linear path).
    static CartesianPath linear(const Waypoint& a, const Waypoint& b);
    static CartesianPath linear(const Eigen::Vector3d& a, const Eigen::Vector3d& b);

    // Circular arc of `radius` about `axis` centred at `center`, swept from
    // `startAngle` to `endAngle` (radians, signed). The EE orientation is carried
    // by the frame (tangent, radial, axis) so it "banks" along the arc. `segments`
    // control poses are laid down and interpolated (Linear position keeps them ON
    // the circle to fp; enough segments make the chord error negligible).
    static CartesianPath arc(const Eigen::Vector3d& center, double radius,
                             const Eigen::Vector3d& axis, double startAngle, double endAngle,
                             int segments = 64);

    // REVOLVE primitive: the EE sweeps a full circle (`turns` revolutions) of
    // `radius` about the line through `pointOnAxis` along `axis`. turns=1 is closed
    // (last pose == first). Implemented as arc(0 .. 2*pi*turns).
    static CartesianPath revolveAround(const Eigen::Vector3d& pointOnAxis, const Eigen::Vector3d& axis,
                                       double radius, double turns = 1.0, int segmentsPerTurn = 64);

    // PATH SWEEP primitive: interpolate an authored waypoint list (the tool edits
    // these). CatmullRom => passes through every waypoint.
    static CartesianPath fromWaypoints(const std::vector<Waypoint>& wps,
                                       PosInterp posMode = PosInterp::CatmullRom);

private:
    // Locate s in [0,1] -> (segment index i, local t in [0,1] within [s_i, s_{i+1}]).
    void locate(double s, int& i, double& t) const;
    Eigen::Vector3d catmullRom(int i, double t) const;   // position on segment i

    std::vector<Waypoint> wps_;
    PosInterp posMode_ = PosInterp::CatmullRom;
    // Arc-length table: arcU_[k] = cumulative normalized arc length at s=arcSofU knot;
    // stored as parallel (s, u) samples for monotone inverse lookup.
    std::vector<double> arcS_;   // increasing s samples in [0,1]
    std::vector<double> arcU_;   // matching normalized arc length u in [0,1]
};

// GATE (declared here; env hook KRS_CARTESIANPATH_SELFTEST wired by the main loop).
// Asserts: linear endpoints match; an arc stays on its circle (radius invariant)
// and sweeps the requested angle; a 1-turn revolve is closed; a Catmull-Rom path
// passes THROUGH its waypoints at their knots; arc-length reparam yields ~constant
// speed; NEG-CTRL: a degenerate single-waypoint path returns that pose (no crash),
// and parameter-space sampling on a curved path is provably NON-uniform in speed
// (so the reparam is doing real work). Pure CPU/Eigen.
bool runCartesianPathGate();

} // namespace krs::path
