// CartesianPath.cpp -- see CartesianPath.hpp. Parametric SE(3) task-space path:
// R^3 position curve (linear / Catmull-Rom) + shortest-arc quaternion SLERP
// orientation, plus arc-length reparameterization. Pure Eigen / CPU.
#include "CartesianPath.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace krs::path {

using krs::dyn::Pose;

namespace {
constexpr double kPi = 3.14159265358979323846;

// A quaternion and its negation are the SAME rotation; pick the sign that keeps
// the dot with `ref` >= 0 so consecutive control quats live on one hemisphere and
// SLERP takes the SHORTEST arc (no 350-degree wind, no wrist flip).
inline Eigen::Quaterniond hemisphere(const Eigen::Quaterniond& q, const Eigen::Quaterniond& ref) {
    return (q.dot(ref) < 0.0) ? Eigen::Quaterniond(-q.w(), -q.x(), -q.y(), -q.z()) : q;
}

// Shortest-arc SLERP between two (already same-hemisphere) unit quats. Eigen's
// slerp is great but does not force the hemisphere itself, so we do that first.
inline Eigen::Quaterniond slerpShort(const Eigen::Quaterniond& a, const Eigen::Quaterniond& b, double t) {
    return a.slerp(t, hemisphere(b, a)).normalized();
}

// Orthonormal frame from a forward tangent + a preferred "up"/axis; used to give
// the arc/revolve EE a well-defined orientation that banks along the curve.
inline Eigen::Matrix3d frameFrom(const Eigen::Vector3d& fwd, const Eigen::Vector3d& up) {
    Eigen::Vector3d x = fwd;
    if (x.norm() < 1e-12) x = Eigen::Vector3d::UnitX();
    x.normalize();
    Eigen::Vector3d z = up - up.dot(x) * x;            // Gram-Schmidt the axis against fwd
    if (z.norm() < 1e-9) {                              // fwd parallel to up -> pick any orthogonal
        z = x.unitOrthogonal();
    }
    z.normalize();
    Eigen::Vector3d y = z.cross(x);
    Eigen::Matrix3d R;
    R.col(0) = x; R.col(1) = y; R.col(2) = z;
    return R;
}
} // namespace

// ---------------------------------------------------------------------------
CartesianPath::CartesianPath(std::vector<Waypoint> wps, PosInterp posMode)
    : posMode_(posMode) {
    wps_ = std::move(wps);
    // Normalize every quat and flip to a single continuous hemisphere so SLERP
    // between neighbours is always the short way (done once, on ingest).
    for (auto& w : wps_) {
        if (w.q.norm() < 1e-15) w.q = Eigen::Quaterniond::Identity();
        w.q.normalize();
    }
    for (size_t i = 1; i < wps_.size(); ++i)
        wps_[i].q = hemisphere(wps_[i].q, wps_[i - 1].q);
}

double CartesianPath::knot(int i) const {
    const int n = int(wps_.size());
    if (n <= 1) return 0.0;
    return double(i) / double(n - 1);
}

void CartesianPath::locate(double s, int& i, double& t) const {
    const int n = int(wps_.size());
    s = std::clamp(s, 0.0, 1.0);
    if (n <= 1) { i = 0; t = 0.0; return; }
    const double f = s * double(n - 1);          // segment coordinate in [0, n-1]
    int seg = int(std::floor(f));
    if (seg >= n - 1) seg = n - 2;               // s==1 lands in the last segment at t=1
    if (seg < 0) seg = 0;
    i = seg;
    t = f - double(seg);
    t = std::clamp(t, 0.0, 1.0);
}

// Catmull-Rom position on segment i (between wps_[i] and wps_[i+1]) at local t.
// Uniform (centripetal not needed for the gate); endpoints use a reflected phantom
// control so the curve still passes exactly through the first/last waypoint.
Eigen::Vector3d CartesianPath::catmullRom(int i, double t) const {
    const int n = int(wps_.size());
    const Eigen::Vector3d& p1 = wps_[i].p;
    const Eigen::Vector3d& p2 = wps_[i + 1].p;
    const Eigen::Vector3d p0 = (i - 1 >= 0)   ? wps_[i - 1].p : (2.0 * p1 - p2);   // reflect
    const Eigen::Vector3d p3 = (i + 2 < n)    ? wps_[i + 2].p : (2.0 * p2 - p1);   // reflect
    const double t2 = t * t, t3 = t2 * t;
    // Standard uniform Catmull-Rom basis (tension 0.5). At t=0 -> p1, t=1 -> p2 exactly.
    return 0.5 * ((2.0 * p1) +
                  (-p0 + p2) * t +
                  (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

Eigen::Vector3d CartesianPath::positionAt(double s) const {
    const int n = int(wps_.size());
    if (n == 0) return Eigen::Vector3d::Zero();
    if (n == 1) return wps_[0].p;
    int i; double t; locate(s, i, t);
    if (posMode_ == PosInterp::Linear)
        return (1.0 - t) * wps_[i].p + t * wps_[i + 1].p;
    return catmullRom(i, t);
}

Eigen::Quaterniond CartesianPath::orientationAt(double s) const {
    const int n = int(wps_.size());
    if (n == 0) return Eigen::Quaterniond::Identity();
    if (n == 1) return wps_[0].q;
    int i; double t; locate(s, i, t);
    return slerpShort(wps_[i].q, wps_[i + 1].q, t);   // C0 across knots; exact at knots
}

Pose CartesianPath::poseAt(double s) const {
    Pose out;
    out.p = positionAt(s);
    out.R = orientationAt(s).toRotationMatrix();
    return out;
}

Eigen::Vector3d CartesianPath::tangentAt(double s) const {
    const int n = int(wps_.size());
    if (n <= 1) return Eigen::Vector3d::Zero();
    // Central finite difference in parameter space (robust across the knot corners
    // of the linear mode and the interior of a Catmull-Rom segment alike).
    const double h = 1e-4;
    const double sa = std::clamp(s - h, 0.0, 1.0);
    const double sb = std::clamp(s + h, 0.0, 1.0);
    Eigen::Vector3d d = positionAt(sb) - positionAt(sa);
    if (d.norm() < 1e-12) {                 // stationary sample -> widen the stencil once
        d = positionAt(std::min(1.0, s + 1e-2)) - positionAt(std::max(0.0, s - 1e-2));
    }
    const double nrm = d.norm();
    return (nrm > 1e-12) ? Eigen::Vector3d(d / nrm) : Eigen::Vector3d::Zero();
}

// ---- arc length ------------------------------------------------------------
double CartesianPath::arcLength(int samples) const {
    if (int(wps_.size()) <= 1 || samples < 1) return 0.0;
    double L = 0.0;
    Eigen::Vector3d prev = positionAt(0.0);
    for (int k = 1; k <= samples; ++k) {
        const double s = double(k) / double(samples);
        const Eigen::Vector3d cur = positionAt(s);
        L += (cur - prev).norm();
        prev = cur;
    }
    return L;
}

void CartesianPath::arcLengthReparam(int samples) {
    arcS_.clear(); arcU_.clear();
    if (int(wps_.size()) <= 1 || samples < 1) { arcS_ = {0.0}; arcU_ = {0.0}; return; }
    arcS_.reserve(samples + 1);
    arcU_.reserve(samples + 1);
    std::vector<double> cum(samples + 1, 0.0);
    Eigen::Vector3d prev = positionAt(0.0);
    arcS_.push_back(0.0);
    for (int k = 1; k <= samples; ++k) {
        const double s = double(k) / double(samples);
        const Eigen::Vector3d cur = positionAt(s);
        cum[k] = cum[k - 1] + (cur - prev).norm();
        prev = cur;
        arcS_.push_back(s);
    }
    const double total = cum[samples];
    for (int k = 0; k <= samples; ++k)
        arcU_.push_back(total > 1e-15 ? cum[k] / total : double(k) / double(samples));
}

double CartesianPath::sOfArc(double u) const {
    u = std::clamp(u, 0.0, 1.0);
    if (arcU_.size() < 2) return u;                    // no table -> identity map
    // Binary search the monotone arcU_ for the bracketing samples, then lerp s.
    const auto it = std::lower_bound(arcU_.begin(), arcU_.end(), u);
    if (it == arcU_.begin()) return arcS_.front();
    if (it == arcU_.end())   return arcS_.back();
    const size_t hi = size_t(it - arcU_.begin());
    const size_t lo = hi - 1;
    const double du = arcU_[hi] - arcU_[lo];
    const double w = (du > 1e-15) ? (u - arcU_[lo]) / du : 0.0;
    return arcS_[lo] + w * (arcS_[hi] - arcS_[lo]);
}

Pose CartesianPath::poseAtArc(double u) const {
    return arcU_.size() < 2 ? poseAt(u) : poseAt(sOfArc(u));
}
Eigen::Vector3d CartesianPath::positionAtArc(double u) const {
    return arcU_.size() < 2 ? positionAt(u) : positionAt(sOfArc(u));
}

// ---- factories -------------------------------------------------------------
CartesianPath CartesianPath::linear(const Waypoint& a, const Waypoint& b) {
    return CartesianPath({a, b}, PosInterp::Linear);
}
CartesianPath CartesianPath::linear(const Eigen::Vector3d& a, const Eigen::Vector3d& b) {
    return linear(Waypoint(a, Eigen::Quaterniond::Identity()),
                  Waypoint(b, Eigen::Quaterniond::Identity()));
}

CartesianPath CartesianPath::arc(const Eigen::Vector3d& center, double radius,
                                 const Eigen::Vector3d& axis, double startAngle, double endAngle,
                                 int segments) {
    Eigen::Vector3d n = axis;
    if (n.norm() < 1e-12) n = Eigen::Vector3d::UnitZ();
    n.normalize();
    // Build an in-plane basis (u,v) orthogonal to the axis; the point at angle a is
    // center + radius*(cos a * u + sin a * v). u is any axis-orthogonal unit vector.
    const Eigen::Vector3d u = n.unitOrthogonal();
    const Eigen::Vector3d v = n.cross(u);
    if (segments < 1) segments = 1;
    std::vector<Waypoint> wps;
    wps.reserve(segments + 1);
    for (int k = 0; k <= segments; ++k) {
        const double a = startAngle + (endAngle - startAngle) * (double(k) / double(segments));
        const Eigen::Vector3d radial = std::cos(a) * u + std::sin(a) * v;        // unit
        const Eigen::Vector3d p = center + radius * radial;
        const Eigen::Vector3d tangent = (-std::sin(a) * u + std::cos(a) * v);    // d/da, unit
        // EE frame: x = travel tangent, z = revolve axis -> banks along the arc.
        const Eigen::Matrix3d R = frameFrom(tangent, n);
        wps.emplace_back(p, Eigen::Quaterniond(R).normalized());
    }
    // Linear position keeps the control poses EXACTLY on the circle (chord sampling);
    // with enough segments the max chord deviation radius*(1-cos(dTheta/2)) is tiny.
    return CartesianPath(std::move(wps), PosInterp::Linear);
}

CartesianPath CartesianPath::revolveAround(const Eigen::Vector3d& pointOnAxis, const Eigen::Vector3d& axis,
                                           double radius, double turns, int segmentsPerTurn) {
    const double total = 2.0 * kPi * turns;
    int segs = int(std::lround(std::abs(turns) * std::max(1, segmentsPerTurn)));
    if (segs < 1) segs = 1;
    return arc(pointOnAxis, radius, axis, 0.0, total, segs);
}

CartesianPath CartesianPath::fromWaypoints(const std::vector<Waypoint>& wps, PosInterp posMode) {
    return CartesianPath(wps, posMode);
}

// ===========================================================================
// GATE CARTESIANPATH
// ===========================================================================
bool runCartesianPathGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[cpath] GATE CARTESIANPATH -- linear endpoints / arc-on-circle / closed revolve / spline-through-waypoints / arc-length constant speed\n");
    bool pass = true;

    // 1) LINEAR: endpoints match exactly, orientation SLERPs end to end.
    {
        const Eigen::Vector3d a(0, 0, 0), b(2, 1, -0.5);
        Eigen::Quaterniond qa = Eigen::Quaterniond::Identity();
        Eigen::Quaterniond qb(Eigen::AngleAxisd(1.2, Eigen::Vector3d(0, 0, 1)));
        CartesianPath L = CartesianPath::linear(Waypoint(a, qa), Waypoint(b, qb));
        const Pose p0 = L.poseAt(0.0), p1 = L.poseAt(1.0), pm = L.poseAt(0.5);
        const bool endsOk = (p0.p - a).norm() < 1e-12 && (p1.p - b).norm() < 1e-12;
        const bool midOk  = (pm.p - 0.5 * (a + b)).norm() < 1e-12;                 // straight line midpoint
        // orientation at ends equals the requested quats (as rotation matrices).
        const bool oriOk = (p0.R - qa.toRotationMatrix()).norm() < 1e-9 &&
                           (p1.R - qb.toRotationMatrix()).norm() < 1e-9;
        const bool ok = endsOk && midOk && oriOk;
        printf("[cpath]   linear: endpoints match=%s  midpoint-on-line=%s  end-orientations=%s  %s\n",
               endsOk ? "yes" : "NO", midOk ? "yes" : "NO", oriOk ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // 2) ARC: every sampled point is at `radius` from center and in the axis plane;
    //    the swept angle from start radial to end radial matches the request.
    {
        const Eigen::Vector3d center(1, 2, 3);
        const Eigen::Vector3d axis(0, 0, 1);
        const double radius = 0.75, a0 = 0.3, a1 = 0.3 + 1.7;   // sweep 1.7 rad
        const int segs = 128;
        CartesianPath A = CartesianPath::arc(center, radius, axis, a0, a1, segs);
        // At the CONTROL KNOTS the point is EXACTLY on the circle (fp-exact); between
        // knots a piecewise-linear arc rides a chord that dips inside by the bounded
        // sagitta radius*(1-cos(dTheta/2)) -- so we assert exact-at-knots AND that the
        // worst intermediate deviation stays within that analytic chord bound (proving
        // the path is a real circle approximation, not drifting off it).
        const int nWp = A.size();
        double maxKnotRadErr = 0.0, maxAnyRadErr = 0.0, maxPlaneErr = 0.0;
        for (int i = 0; i < nWp; ++i) {                        // exact circle at each knot
            const Eigen::Vector3d rel = A.waypoints()[i].p - center;
            maxKnotRadErr = std::max(maxKnotRadErr, std::abs(rel.norm() - radius));
            maxPlaneErr   = std::max(maxPlaneErr, std::abs(rel.dot(axis.normalized())));
        }
        for (int k = 0; k <= 400; ++k) {                       // dense intermediate sampling
            const double s = double(k) / 400.0;
            const Eigen::Vector3d rel = A.positionAt(s) - center;
            maxAnyRadErr = std::max(maxAnyRadErr, std::abs(rel.norm() - radius));
            maxPlaneErr  = std::max(maxPlaneErr, std::abs(rel.dot(axis.normalized())));
        }
        const double dTheta = (a1 - a0) / double(segs);
        const double chordBound = radius * (1.0 - std::cos(0.5 * dTheta)) + 1e-9;  // sagitta
        // swept angle between the start and end radial vectors.
        const Eigen::Vector3d r0 = A.positionAt(0.0) - center;
        const Eigen::Vector3d r1 = A.positionAt(1.0) - center;
        const double sweep = std::atan2(r0.cross(r1).dot(axis.normalized()), r0.dot(r1));
        const bool knotOk  = maxKnotRadErr < 1e-12;            // knots are fp-exact on the circle
        const bool chordOk = maxAnyRadErr <= chordBound;       // intermediate stays within sagitta bound
        const bool planeOk = maxPlaneErr < 1e-12;
        const bool sweepOk = std::abs(sweep - (a1 - a0)) < 1e-6;
        const bool ok = knotOk && chordOk && planeOk && sweepOk;
        printf("[cpath]   arc: knotRadErr=%.2e(exact) maxRadErr=%.2e(<=chord %.2e) planeErr=%.2e sweep=%.4f(want %.4f) rad  %s\n",
               maxKnotRadErr, maxAnyRadErr, chordBound, maxPlaneErr, sweep, a1 - a0, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // 3) REVOLVE (1 turn): closed -- last pose == first pose (position AND orientation).
    {
        CartesianPath R = CartesianPath::revolveAround(Eigen::Vector3d(0, 0, 0),
                                                       Eigen::Vector3d(0, 0, 1), 0.5, 1.0, 90);
        const Pose s0 = R.poseAt(0.0), s1 = R.poseAt(1.0);
        const double dp = (s0.p - s1.p).norm();
        const double dR = (s0.R - s1.R).norm();
        // NEG-CTRL within this test: the halfway point is NOT the start (proves the
        // path actually goes around, not a degenerate stay-put).
        const Pose sh = R.poseAt(0.5);
        const double dHalf = (s0.p - sh.p).norm();
        const bool closed = dp < 1e-9 && dR < 1e-9;
        const bool sweptRound = dHalf > 0.9;                   // ~diameter (2*0.5) apart at half turn
        const bool ok = closed && sweptRound;
        printf("[cpath]   revolve(1 turn): |p0-p1|=%.2e |R0-R1|=%.2e (closed) ; half-turn offset=%.3f(>0.9)  %s\n",
               dp, dR, dHalf, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // 4) CATMULL-ROM PASSES THROUGH each waypoint at its knot s_i (interpolating).
    {
        std::vector<Waypoint> w;
        auto Q = [](double ang, const Eigen::Vector3d& ax) {
            return Eigen::Quaterniond(Eigen::AngleAxisd(ang, ax.normalized()));
        };
        w.emplace_back(Eigen::Vector3d(0, 0, 0),      Q(0.0, Eigen::Vector3d(0, 0, 1)));
        w.emplace_back(Eigen::Vector3d(1, 0.7, 0.2),  Q(0.5, Eigen::Vector3d(0, 1, 0)));
        w.emplace_back(Eigen::Vector3d(2, -0.3, 1.0), Q(1.1, Eigen::Vector3d(1, 0, 0)));
        w.emplace_back(Eigen::Vector3d(3, 0.4, 0.1),  Q(1.6, Eigen::Vector3d(0, 0, 1)));
        CartesianPath C = CartesianPath::fromWaypoints(w, PosInterp::CatmullRom);
        double maxWpErr = 0.0, maxOriErr = 0.0;
        for (int i = 0; i < int(w.size()); ++i) {
            const double s = C.knot(i);
            maxWpErr  = std::max(maxWpErr,  (C.positionAt(s) - w[i].p).norm());
            // orientation at a knot must equal that waypoint's rotation.
            const Eigen::Matrix3d dR = C.orientationAt(s).toRotationMatrix() - w[i].q.toRotationMatrix();
            maxOriErr = std::max(maxOriErr, dR.norm());
        }
        const bool ok = maxWpErr < 1e-9 && maxOriErr < 1e-9;
        printf("[cpath]   catmull-rom: max through-waypoint pos err=%.2e ori err=%.2e  %s\n",
               maxWpErr, maxOriErr, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // 5) ARC-LENGTH REPARAM => ~constant Cartesian speed. On a CURVED Catmull-Rom
    //    path, equal ds steps are NON-uniform in metres; equal du (arc) steps are.
    {
        std::vector<Waypoint> w;
        w.emplace_back(Eigen::Vector3d(0, 0, 0),   Eigen::Quaterniond::Identity());
        w.emplace_back(Eigen::Vector3d(1, 2, 0),   Eigen::Quaterniond::Identity());
        w.emplace_back(Eigen::Vector3d(3, 2.2, 0), Eigen::Quaterniond::Identity());
        w.emplace_back(Eigen::Vector3d(4, 0, 0),   Eigen::Quaterniond::Identity());
        CartesianPath C = CartesianPath::fromWaypoints(w, PosInterp::CatmullRom);
        C.arcLengthReparam(2000);
        const int N = 50;
        auto speedSpread = [&](bool byArc) {
            double lo = 1e30, hi = 0.0;
            Eigen::Vector3d prev = byArc ? C.positionAtArc(0.0) : C.positionAt(0.0);
            for (int k = 1; k <= N; ++k) {
                const double u = double(k) / double(N);
                const Eigen::Vector3d cur = byArc ? C.positionAtArc(u) : C.positionAt(u);
                const double step = (cur - prev).norm();
                lo = std::min(lo, step); hi = std::max(hi, step);
                prev = cur;
            }
            return hi / std::max(1e-12, lo);      // 1.0 == perfectly constant speed
        };
        const double arcSpread = speedSpread(true);
        const double paramSpread = speedSpread(false);
        const bool arcConst = arcSpread < 1.05;            // arc-length: within 5% step-to-step
        const bool paramVaries = paramSpread > 1.3;        // NEG-CTRL: param-space is genuinely non-uniform
        const bool ok = arcConst && paramVaries;
        printf("[cpath]   arc-length: step ratio(hi/lo) arc=%.3f(<1.05) vs param=%.3f(>1.30 non-uniform)  %s\n",
               arcSpread, paramSpread, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // 6) NEG-CTRL: degenerate single-waypoint path -> poseAt returns THAT pose for
    //    any s, arcLength==0, no crash. And an empty path returns identity safely.
    {
        Eigen::Quaterniond q(Eigen::AngleAxisd(0.8, Eigen::Vector3d(1, 1, 0).normalized()));
        const Eigen::Vector3d p(0.4, -0.2, 0.9);
        CartesianPath One = CartesianPath::fromWaypoints({ Waypoint(p, q) });
        const Pose a = One.poseAt(0.0), b = One.poseAt(0.5), c = One.poseAt(1.0);
        const bool held = (a.p - p).norm() < 1e-12 && (b.p - p).norm() < 1e-12 && (c.p - p).norm() < 1e-12 &&
                          (a.R - q.toRotationMatrix()).norm() < 1e-9;
        const bool lenZero = std::abs(One.arcLength()) < 1e-12;
        const bool tangZero = One.tangentAt(0.5).norm() < 1e-12;      // stationary -> zero tangent, no NaN
        CartesianPath Empty;
        const Pose e = Empty.poseAt(0.7);
        const bool emptyOk = (e.p.norm() < 1e-12) && (e.R - Eigen::Matrix3d::Identity()).norm() < 1e-12;
        const bool ok = held && lenZero && tangZero && emptyOk;
        printf("[cpath]   NEG-CTRL degenerate: 1-wp holds pose=%s len0=%s tangent0=%s ; empty->identity=%s  %s\n",
               held ? "yes" : "NO", lenZero ? "yes" : "NO", tangZero ? "yes" : "NO",
               emptyOk ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    printf("[cpath] %s\n", pass ? "ALL PASS (linear/arc/revolve/spline-through-waypoints + arc-length constant-speed reparam; degenerate paths handled)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::path
