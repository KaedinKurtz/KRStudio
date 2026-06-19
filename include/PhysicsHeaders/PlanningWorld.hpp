#pragma once
// ===========================================================================
// OMPL sprint, Phase 1 — analytic collision world for motion planning (krs::plan).
//
// A deterministic, pure-CPU/Eigen capsule-robot-vs-primitive-obstacle model:
// the textbook motion-planning collision representation (what FCL computes, but
// closed-form and reproducible, with NO dependency on the live stateful PhysX
// scene or OpenVDB). The OMPL state-validity checker is FK (krs::dyn::SerialChain)
// + this query. Every "penetration" returned is a REAL metre value (a closed-form
// distance), so the PLAN gates assert measured numbers, not flags.
//
// Robot links are capsules expressed in their body-local frame; at a config q,
// SerialChain::fk places them in the world. Obstacles are Sphere / HalfSpace /
// oriented Box. Distances:
//   * point-segment          (sphere vs capsule)              — exact
//   * point/segment-halfspace (floor/wall vs capsule)         — exact (linear)
//   * point-OBB + convex 1-D line-search (box vs capsule)     — exact to ~1e-8
//   * segment-segment        (self-collision, non-adjacent)   — exact (Ericson)
// All SI: metres / radians.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include "RobotDynamics.hpp"

namespace krs::plan {

// --- closed-form primitive distances --------------------------------------

// Squared distance from point p to segment [a,b], plus the clamped parameter t.
inline double pointSegDist2(const Eigen::Vector3d& p, const Eigen::Vector3d& a,
                            const Eigen::Vector3d& b, double& t) {
    const Eigen::Vector3d ab = b - a;
    const double denom = ab.squaredNorm();
    t = (denom > 1e-18) ? ((p - a).dot(ab) / denom) : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const Eigen::Vector3d c = a + t * ab;
    return (p - c).squaredNorm();
}
inline double pointSegDist(const Eigen::Vector3d& p, const Eigen::Vector3d& a, const Eigen::Vector3d& b) {
    double t; return std::sqrt(pointSegDist2(p, a, b, t));
}

// Closest-distance between segments [p1,q1] and [p2,q2]  (Ericson, Real-Time
// Collision Detection §5.1.9 — robust to parallel/degenerate segments).
inline double segSegDist(const Eigen::Vector3d& p1, const Eigen::Vector3d& q1,
                         const Eigen::Vector3d& p2, const Eigen::Vector3d& q2) {
    const Eigen::Vector3d d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const double a = d1.squaredNorm(), e = d2.squaredNorm(), f = d2.dot(r);
    const double eps = 1e-18;
    double s, t;
    if (a <= eps && e <= eps) return r.norm();                 // both points
    if (a <= eps) { s = 0.0; t = std::clamp(f / e, 0.0, 1.0); }
    else {
        const double c = d1.dot(r);
        if (e <= eps) { t = 0.0; s = std::clamp(-c / a, 0.0, 1.0); }
        else {
            const double b = d1.dot(d2), denom = a * e - b * b;
            s = (denom > eps) ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0.0)      { t = 0.0; s = std::clamp(-c / a, 0.0, 1.0); }
            else if (t > 1.0) { t = 1.0; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    const Eigen::Vector3d c1 = p1 + d1 * s, c2 = p2 + d2 * t;
    return (c1 - c2).norm();
}

// --- obstacles -------------------------------------------------------------
// Each obstacle exposes segDist(a,b): the SIGNED distance from segment [a,b] to
// the obstacle SURFACE (negative when the segment passes inside the obstacle).
// A capsule of radius rc collides iff segDist < rc; penetration = max(0, rc-segDist).

enum class ObstacleType { Sphere, HalfSpace, Box };

struct Obstacle {
    ObstacleType type = ObstacleType::Sphere;
    // Sphere: c = centre, r = radius.
    // HalfSpace: n = outward unit normal of the SOLID side; pt = a point on the plane;
    //            the obstacle occupies { x : n.(x-pt) < 0 }  (e.g. floor n=+Z is the half BELOW the plane).
    // Box: c = centre, R columns = box axes (unit), half = half-extents.
    Eigen::Vector3d c = Eigen::Vector3d::Zero();
    Eigen::Vector3d n = Eigen::Vector3d::UnitZ();
    Eigen::Vector3d pt = Eigen::Vector3d::Zero();
    Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
    Eigen::Vector3d half = Eigen::Vector3d::Ones();
    double r = 0.5;

    static Obstacle sphere(const Eigen::Vector3d& center, double radius) {
        Obstacle o; o.type = ObstacleType::Sphere; o.c = center; o.r = radius; return o;
    }
    static Obstacle halfSpace(const Eigen::Vector3d& normal, const Eigen::Vector3d& pointOnPlane) {
        Obstacle o; o.type = ObstacleType::HalfSpace; o.n = normal.normalized(); o.pt = pointOnPlane; return o;
    }
    static Obstacle box(const Eigen::Vector3d& center, const Eigen::Vector3d& halfExtents,
                        const Eigen::Matrix3d& orient = Eigen::Matrix3d::Identity()) {
        Obstacle o; o.type = ObstacleType::Box; o.c = center; o.half = halfExtents; o.R = orient; return o;
    }

    // Exterior distance from point p to the box surface (0 if inside).
    double pointBoxDist(const Eigen::Vector3d& p) const {
        const Eigen::Vector3d local = R.transpose() * (p - c);
        Eigen::Vector3d d = local.cwiseAbs() - half;        // >0 component => outside on that axis
        d = d.cwiseMax(0.0);
        return d.norm();
    }

    double segDist(const Eigen::Vector3d& a, const Eigen::Vector3d& b) const {
        switch (type) {
            case ObstacleType::Sphere:    return pointSegDist(c, a, b) - r;
            case ObstacleType::HalfSpace: return std::min(n.dot(a - pt), n.dot(b - pt));  // linear along the segment
            case ObstacleType::Box: {
                // Exterior segment-to-OBB distance: dist(point(t),box) is convex in
                // t (distance to a convex set ∘ affine map) — golden-section finds
                // the global min on [0,1]. Exact while the axis stays exterior.
                const double gr = 0.6180339887498949;
                double lo = 0.0, hi = 1.0;
                double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo);
                auto val = [&](double t){ return pointBoxDist(a + t * (b - a)); };
                double f1 = val(x1), f2 = val(x2);
                for (int it = 0; it < 48; ++it) {
                    if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = val(x1); }
                    else         { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = val(x2); }
                }
                return std::min(f1, f2);
            }
        }
        return 1e30;
    }
};

// --- robot capsule model ---------------------------------------------------
struct LinkCapsule {
    int body = 0;                         // SerialChain body index this capsule rides on
    Eigen::Vector3d a = Eigen::Vector3d::Zero();   // endpoints in the body-local frame
    Eigen::Vector3d b = Eigen::Vector3d::Zero();
    double radius = 0.05;
};

// Detailed penetration report (for the gate's measured numbers).
struct CollisionReport {
    double maxPenetration = 0.0;     // metres, max over all (link,obstacle) + self pairs
    double maxScenePen = 0.0;        // link-vs-obstacle only
    double maxSelfPen = 0.0;         // link-vs-link only
    double minClearance = 1e30;      // signed; negative => deepest penetration
};

class CollisionWorld {
public:
    std::vector<LinkCapsule> capsules;
    std::vector<Obstacle>    obstacles;
    bool selfCollision = true;
    double tolerance = 1e-6;         // metres; valid iff maxPenetration < tolerance

    // SELF-COLLISION MATRIX (Phase 1): capsule-index pairs DISABLED from self-collision
    // checking (in addition to kinematic neighbours). The generated disable matrix
    // populates this, so the planner's validity check skips always/never/adjacent pairs
    // but STILL checks the kept (sometimes-colliding) real-risk pairs. Empty by default
    // (no change to existing planning behaviour).
    std::set<std::pair<int, int>> disabledSelfPairs;
    bool selfPairDisabled(int i, int j) const {
        if (disabledSelfPairs.empty()) return false;
        const auto key = (i < j) ? std::make_pair(i, j) : std::make_pair(j, i);
        return disabledSelfPairs.count(key) != 0;
    }

    // World-space capsule endpoints for capsule k at config q (FK poses precomputed).
    void worldCapsule(const std::vector<krs::dyn::Pose>& poses, int k,
                      Eigen::Vector3d& A, Eigen::Vector3d& B) const {
        const LinkCapsule& lc = capsules[k];
        const krs::dyn::Pose& P = poses[lc.body];
        A = P.R * lc.a + P.p;
        B = P.R * lc.b + P.p;
    }

    // Two links are kinematic neighbours (share a joint) -> their capsules are
    // never tested for self-collision (they legitimately touch at the joint).
    static bool adjacent(const krs::dyn::SerialChain& chain, int ba, int bb) {
        if (ba == bb) return true;
        return chain.joint(ba).parent == bb || chain.joint(bb).parent == ba;
    }

    CollisionReport query(const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q) const {
        std::vector<krs::dyn::Pose> poses;
        chain.fk(q, poses);
        CollisionReport rep;
        const int nc = int(capsules.size());
        std::vector<Eigen::Vector3d> A(nc), B(nc);
        for (int k = 0; k < nc; ++k) worldCapsule(poses, k, A[k], B[k]);

        // link vs obstacle
        for (int k = 0; k < nc; ++k) {
            const double rc = capsules[k].radius;
            for (const Obstacle& o : obstacles) {
                const double clear = o.segDist(A[k], B[k]) - rc;
                rep.minClearance = std::min(rep.minClearance, clear);
                const double pen = std::max(0.0, -clear);
                rep.maxScenePen = std::max(rep.maxScenePen, pen);
            }
        }
        // self-collision (non-adjacent link pairs)
        if (selfCollision) {
            for (int i = 0; i < nc; ++i)
                for (int j = i + 1; j < nc; ++j) {
                    if (adjacent(chain, capsules[i].body, capsules[j].body)) continue;
                    if (selfPairDisabled(i, j)) continue;   // matrix-disabled (always/never)
                    const double clear = segSegDist(A[i], B[i], A[j], B[j])
                                         - capsules[i].radius - capsules[j].radius;
                    rep.minClearance = std::min(rep.minClearance, clear);
                    rep.maxSelfPen = std::max(rep.maxSelfPen, std::max(0.0, -clear));
                }
        }
        rep.maxPenetration = std::max(rep.maxScenePen, rep.maxSelfPen);
        return rep;
    }

    double maxPenetration(const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q) const {
        return query(chain, q).maxPenetration;
    }
    bool valid(const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q) const {
        return maxPenetration(chain, q) < tolerance;
    }
};

// --- joint limits (position + velocity) ------------------------------------
struct JointLimits {
    Eigen::VectorXd qLower, qUpper, vMax;     // size nq
};

// Min position-limit margin over all waypoints/dofs (negative => some dof out of range).
inline double positionMargin(const std::vector<Eigen::VectorXd>& path, const JointLimits& lim) {
    double m = 1e30;
    for (const auto& q : path)
        for (int i = 0; i < q.size(); ++i)
            m = std::min(m, std::min(q[i] - lim.qLower[i], lim.qUpper[i] - q[i]));
    return m;
}

// Constant-speed time parameterization: each segment's duration is the longest
// per-dof traverse time |Δq_i|/vMax_i (so the fastest dof rides exactly at vMax).
inline std::vector<double> timeParameterize(const std::vector<Eigen::VectorXd>& path,
                                            const JointLimits& lim, double speedScale = 1.0) {
    std::vector<double> t(path.size(), 0.0);
    for (size_t k = 1; k < path.size(); ++k) {
        const Eigen::VectorXd dq = path[k] - path[k - 1];
        double seg = 1e-6;
        for (int i = 0; i < dq.size(); ++i)
            seg = std::max(seg, std::abs(dq[i]) / std::max(1e-9, lim.vMax[i]));
        t[k] = t[k - 1] + seg / std::max(1e-9, speedScale);
    }
    return t;
}

// Max velocity-limit violation ratio (max_i,seg |Δq_i|/Δt / vMax_i). <=1 => respected.
inline double velocityRatio(const std::vector<Eigen::VectorXd>& path,
                            const std::vector<double>& times, const JointLimits& lim) {
    double ratio = 0.0;
    for (size_t k = 1; k < path.size(); ++k) {
        const double dt = std::max(1e-9, times[k] - times[k - 1]);
        const Eigen::VectorXd dq = path[k] - path[k - 1];
        for (int i = 0; i < dq.size(); ++i)
            ratio = std::max(ratio, (std::abs(dq[i]) / dt) / std::max(1e-9, lim.vMax[i]));
    }
    return ratio;
}

// Total joint-space path length (sum of consecutive L2 deltas).
inline double pathLength(const std::vector<Eigen::VectorXd>& path) {
    double L = 0.0;
    for (size_t k = 1; k < path.size(); ++k) L += (path[k] - path[k - 1]).norm();
    return L;
}

} // namespace krs::plan
