// Constraint.cpp -- see Constraint.hpp. krs::constraint: the CORE of the
// Fusion-360-style assembly-constraint suite (anchors, snap solve, DOF table,
// JSON round-trip) + the headless CONSTRAINT gate.
//
// Snap math mirrors the proven robot-builder mate path:
//   - krs::rbuild::RobotGraph::mateTransformConcentric (RobotBuilder.hpp:352):
//     shortest-arc (Rodrigues) axis alignment + mate-point coincidence, with
//     the antiparallel case rotating pi about an arbitrary perpendicular;
//   - krs::robot::snapMateSubtree (RobotInstance.cpp:400): the moved side is
//     LEFT-MULTIPLIED by a world rigid transform (rotation+translation only);
//   - RobotBuilderPanel::onDefineFromFeatures faceFrame (RobotBuilderPanel.cpp
//     :894): cylinder anchors take the rim nearest the click as the frame
//     origin; RBJoint::orthonormalizeFrame (RobotBuilder.hpp:102): the
//     reference-axis fallback basis.
#include "Constraint.hpp"

#include "Scene.hpp"        // gate registry host (same pattern as Measure.cpp)

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace krs::constraint {

// ===========================================================================
// double-precision rigid-snap helpers
// ===========================================================================
namespace {

inline glm::dvec3 dv(const glm::vec3& v) { return glm::dvec3(v); }

// Any unit vector perpendicular to (unit) d -- mirrors the seed choice of
// RBJoint::orthonormalizeFrame (RobotBuilder.hpp:106).
glm::dvec3 anyPerp(const glm::dvec3& d) {
    const glm::dvec3 seed = (std::abs(d.x) < 0.9) ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
    return glm::normalize(seed - glm::dot(seed, d) * d);
}
glm::vec3 anyPerpF(const glm::vec3& d) { return glm::vec3(anyPerp(glm::dvec3(d))); }

// Shortest-arc rotation FROM -> TO (unit inputs): the quaternion twin of
// mateTransformConcentric's Rodrigues block. Antiparallel inputs rotate pi
// about an arbitrary perpendicular (the Eigen unitOrthogonal analog).
glm::dquat shortestArc(const glm::dvec3& from, const glm::dvec3& to) {
    const double c = glm::dot(from, to);
    if (c > 1.0 - 1e-14) return glm::dquat(1.0, 0.0, 0.0, 0.0);
    if (c < -1.0 + 1e-12) return glm::angleAxis(glm::pi<double>(), anyPerp(from));
    return glm::angleAxis(std::acos(std::clamp(c, -1.0, 1.0)),
                          glm::normalize(glm::cross(from, to)));
}

// MINIMAL rotation making FROM colinear with TO: aligns to the NEARER of +-TO,
// so an obtuse pair flips sense (the smallest arc -- Concentric/Parallel/
// PinSlot semantics; Revolute uses the sign-preserving shortestArc instead).
glm::dquat minimalColinear(const glm::dvec3& from, const glm::dvec3& to) {
    return (glm::dot(from, to) >= 0.0) ? shortestArc(from, to) : shortestArc(from, -to);
}

// Left-multiply a WORLD rigid transform (rotate by R about the origin, then
// translate by t) onto the body's TransformComponent -- translation+rotation
// only, scale NEVER touched (snapMateSubtree's contract). An exactly-identity
// R skips the quaternion write so orientation-preserving snaps (Ball,
// Coincident, Tangent) leave the stored rotation bit-identical.
void applyWorldRigid(TransformComponent& tc, const glm::dquat& R, const glm::dvec3& t) {
    const bool hasRot = !(R.w == 1.0 && R.x == 0.0 && R.y == 0.0 && R.z == 0.0);
    if (hasRot) {
        const glm::dquat q0(tc.rotation);
        tc.rotation = glm::quat(glm::normalize(R * q0));
    }
    tc.translation = glm::vec3(R * dv(tc.translation) + t);
}

} // namespace

// ===========================================================================
// Anchors
// ===========================================================================
Anchor makeAnchor(entt::registry& reg, const krs::sel::Selection& sel) {
    Anchor a;
    a.body   = sel.entity;
    a.key    = sel.faceKey;          // edge picks carry edgeKey here (Selection contract)
    a.faceId = sel.faceId;
    a.edgeId = sel.edgeId;
    a.radius = sel.radius;

    using FT = krs::sel::FeatureType;
    glm::vec3 pW = sel.axisPos, zW = sel.axisDir;
    switch (sel.type) {
    case FT::Plane:
        a.from = AnchorSource::PlaneFace; pW = sel.axisPos; zW = sel.normal; break;
    case FT::Cylinder:
    case FT::Cone:
        a.from = AnchorSource::CylFace;
        // The rim nearest the click = the face end the operator pointed at
        // (mirrors RobotBuilderPanel faceFrame); untrimmed/synthetic faces
        // (both rims ~equal) anchor on the analytic axis point.
        pW = (glm::distance(sel.axisEnd0, sel.axisEnd1) > 1e-5f)
             ? (glm::distance(sel.hitPoint, sel.axisEnd0) <= glm::distance(sel.hitPoint, sel.axisEnd1)
                ? sel.axisEnd0 : sel.axisEnd1)
             : sel.axisPos;
        zW = sel.axisDir;
        break;
    case FT::Sphere:
        a.from = AnchorSource::SphereFace; pW = sel.axisPos; zW = sel.axisDir; break;
    case FT::EdgeCircle:
        a.from = AnchorSource::CircleEdge; pW = sel.axisPos; zW = sel.axisDir; break;
    case FT::EdgeLine:
        a.from = AnchorSource::LineEdge;   pW = sel.axisPos; zW = sel.axisDir; break;
    case FT::Vertex:
        a.from = AnchorSource::Vertex;     pW = sel.hitPoint; zW = sel.normal; break;
    default:
        a.from = AnchorSource::PlaneFace;  pW = sel.hitPoint; zW = sel.normal; break;
    }

    glm::mat4 M(1.0f);
    if (reg.valid(sel.entity))
        if (const auto* xf = reg.try_get<TransformComponent>(sel.entity)) M = xf->getTransform();
    // WORLD -> body-LOCAL. Directions invert the resolveFace convention
    // (world = normalize(inverseTranspose(M) * local)), whose exact inverse is
    // transpose(mat3(M)); positions go through the full inverse.
    const glm::mat4 invM = glm::inverse(M);
    const glm::mat3 Lt = glm::transpose(glm::mat3(M));
    a.pos = glm::vec3(invM * glm::vec4(pW, 1.0f));
    const glm::vec3 z = Lt * zW;
    a.axisZ = (glm::dot(z, z) > 1e-12f) ? glm::normalize(z) : glm::vec3(0, 0, 1);
    // Deterministic reference axis: a picked face carries no roll cue, so take
    // a stable perpendicular (the UI may overwrite axisX for slot directions).
    a.axisX = anyPerpF(a.axisZ);
    return a;
}

AnchorWorldFrame anchorWorldFrame(entt::registry& reg, const Anchor& a) {
    AnchorWorldFrame f;
    if (a.body == entt::null || !reg.valid(a.body)) return f;

    if (a.key != 0) {
        // DURABLE-KEY validation + re-find: the faceId/edgeId hints may be
        // stale after re-import / array reorder; the geometry-invariant key is
        // the truth. A keyed anchor whose key exists NOWHERE on the body is
        // STALE -> invalid (never guess a wrong face).
        bool found = false;
        if (const auto* fc = reg.try_get<BRepFaceComponent>(a.body)) {
            if (a.faceId >= 0 && a.faceId < int(fc->faces.size())
                && fc->faces[std::size_t(a.faceId)].faceKey == a.key) found = true;
            if (!found)
                for (const auto& bf : fc->faces) if (bf.faceKey == a.key) { found = true; break; }
        }
        if (!found)
            if (const auto* ec = reg.try_get<BRepEdgeComponent>(a.body)) {
                if (a.edgeId >= 0 && a.edgeId < int(ec->edges.size())
                    && ec->edges[std::size_t(a.edgeId)].edgeKey == a.key) found = true;
                if (!found)
                    for (const auto& be : ec->edges) if (be.edgeKey == a.key) { found = true; break; }
            }
        if (!found) return f;
    } else {
        // Un-keyed (synthetic/datum) anchor: range-check the hints IF set.
        if (a.faceId >= 0) {
            const auto* fc = reg.try_get<BRepFaceComponent>(a.body);
            if (!fc || a.faceId >= int(fc->faces.size())) return f;
        }
        if (a.edgeId >= 0) {
            const auto* ec = reg.try_get<BRepEdgeComponent>(a.body);
            if (!ec || a.edgeId >= int(ec->edges.size())) return f;
        }
    }

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(a.body)) M = xf->getTransform();
    const glm::mat3 N = glm::mat3(glm::inverseTranspose(M));   // resolveFace's direction map
    f.pos = glm::vec3(M * glm::vec4(a.pos, 1.0f));
    const glm::vec3 z = N * a.axisZ;
    f.z = (glm::dot(z, z) > 1e-12f) ? glm::normalize(z) : glm::vec3(0, 0, 1);
    glm::vec3 x = N * a.axisX;
    x -= glm::dot(x, f.z) * f.z;                               // keep the frame orthonormal
    f.x = (glm::dot(x, x) > 1e-12f) ? glm::normalize(x) : anyPerpF(f.z);
    f.radius = a.radius;
    f.valid = true;
    return f;
}

// ===========================================================================
// ctx singleton (mirror worldState())
// ===========================================================================
ConstraintGraphComponent& constraintGraph(entt::registry& reg) {
    auto* g = reg.ctx().find<ConstraintGraphComponent>();
    return g ? *g : reg.ctx().emplace<ConstraintGraphComponent>();
}

// ===========================================================================
// SNAP -- move body B rigidly so the relation holds with body A fixed.
// ===========================================================================
bool applyConstraintSnap(entt::registry& reg, Constraint& c) {
    if (c.suppressed) return false;
    const AnchorWorldFrame A = anchorWorldFrame(reg, c.a);
    const AnchorWorldFrame B = anchorWorldFrame(reg, c.b);
    if (!A.valid || !B.valid) return false;
    if (c.a.body == c.b.body) return false;                    // cannot mate a body to itself
    // Robots are kinematically OWNED: their chain is the single writer of
    // their motion (the free-move lockout rule, components.hpp:29).
    if (reg.any_of<RobotSubcomponentComponent, RobotRootComponent>(c.b.body)) return false;
    auto* tc = reg.try_get<TransformComponent>(c.b.body);
    if (!tc) return false;

    const glm::dvec3 pA = dv(A.pos), pB = dv(B.pos);
    const glm::dvec3 zA = glm::normalize(dv(A.z)), zB = glm::normalize(dv(B.z));
    const glm::dvec3 xA = glm::normalize(dv(A.x)), xB = glm::normalize(dv(B.x));
    const glm::dvec3 yA = glm::cross(zA, xA), yB = glm::cross(zB, xB);
    const double rA = double(A.radius), rB = double(B.radius);

    glm::dquat R(1.0, 0.0, 0.0, 0.0);   // world rotation (exact identity when untouched)
    glm::dvec3 d(0.0);                  // world translation applied AFTER the pivot rotation
    bool pivotB = false;                // rotate about B's anchor point (it stays put)

    switch (c.type) {
    case CType::Coincident:
        if (c.a.from == AnchorSource::PlaneFace)
            d = -glm::dot(pB - pA, zA) * zA;                   // point-on-plane along the normal
        else
            d = pA - pB;                                       // point anchors: exact coincide
        break;

    case CType::Concentric:
    case CType::Cylindrical: {                                 // Cylindrical positions like Concentric
        R = minimalColinear(zB, zA); pivotB = true;
        const glm::dvec3 w = pB - pA;
        d = -(w - glm::dot(w, zA) * zA);                       // kill the perpendicular offset only
        break;
    }

    case CType::Parallel:
        R = minimalColinear(zB, zA); pivotB = true;            // smallest arc; obtuse ends anti-parallel
        break;

    case CType::Perpendicular: {
        const double th = std::acos(std::clamp(glm::dot(zA, zB), -1.0, 1.0));
        glm::dvec3 n = glm::cross(zB, zA);
        const double nl = glm::length(n);
        n = (nl > 1e-12) ? n / nl : anyPerp(zA);               // parallel axes: any perpendicular works
        R = glm::angleAxis(th - glm::half_pi<double>(), n);    // minimal: rotate by (theta - 90deg)
        pivotB = true;
        break;
    }

    case CType::Tangent: {
        const bool aPlane = (c.a.from == AnchorSource::PlaneFace);
        const bool bPlane = (c.b.from == AnchorSource::PlaneFace);
        if (aPlane && bPlane) return false;                    // plane-plane tangency is meaningless
        if (aPlane) {                                          // B = the cylinder: slide along A's normal
            const double s = glm::dot(pB - pA, zA);            //   (distance measured at B's axis anchor)
            const double target = (s >= 0.0 ? 1.0 : -1.0) * rB;
            d = (target - s) * zA;                             // same side preserved
        } else if (bPlane) {                                   // B = the plane: slide along B's own normal
            const double s = glm::dot(pA - pB, zB);
            const double target = (s >= 0.0 ? 1.0 : -1.0) * rA;
            d = (s - target) * zB;                             // moving B by delta*zB changes s by -delta
        } else {                                               // cylinder-cylinder EXTERNAL tangency
            const glm::dvec3 w = pB - pA;
            glm::dvec3 n = glm::cross(zA, zB);
            if (glm::length(n) < 1e-9) {                       // parallel axes: radial direction
                const glm::dvec3 perp = w - glm::dot(w, zA) * zA;
                const double dist = glm::length(perp);
                const glm::dvec3 dir = (dist > 1e-12) ? perp / dist : anyPerp(zA);
                d = (rA + rB - dist) * dir;
            } else {                                           // skew axes: the common perpendicular
                n = glm::normalize(n);
                const double s = glm::dot(w, n);
                const double target = (s >= 0.0 ? 1.0 : -1.0) * (rA + rB);
                d = (target - s) * n;
            }
        }
        break;
    }

    case CType::Flush: {
        R = shortestArc(zB, -zA); pivotB = true;               // mating faces: normals ANTI-parallel
        d = -glm::dot(pB - pA, zA) * zA;                       // + coplanar
        break;
    }

    case CType::Distance: {
        if (c.a.from == AnchorSource::PlaneFace) {             // Flush with a signed gap along A's normal
            R = shortestArc(zB, -zA); pivotB = true;
            d = (c.offset - glm::dot(pB - pA, zA)) * zA;
        } else {                                               // point anchors: offset along A's axis
            d = (pA + c.offset * zA) - pB;
        }
        break;
    }

    case CType::Angle: {
        const double th = std::acos(std::clamp(glm::dot(zA, zB), -1.0, 1.0));
        glm::dvec3 n = glm::cross(zB, zA);
        const double nl = glm::length(n);
        n = (nl > 1e-12) ? n / nl : anyPerp(zA);               // the common perpendicular
        R = glm::angleAxis(th - glm::radians(c.angleDeg), n);
        pivotB = true;
        break;
    }

    case CType::Rigid:
    case CType::Slider: {
        // Full basis alignment: R maps B's anchor basis onto A's.
        const glm::dmat3 BA{ xA, yA, zA };
        const glm::dmat3 BB{ xB, yB, zB };
        R = glm::quat_cast(BA * glm::transpose(BB));
        pivotB = true;
        if (c.type == CType::Rigid) {
            d = pA - pB;                                       // full frame lock
        } else {
            const glm::dvec3 w = pB - pA;                      // Slider: colinear + roll locked,
            d = -(w - glm::dot(w, zA) * zA);                   // along-axis offset preserved (tz open)
        }
        break;
    }

    case CType::Revolute:
        // Concentric + anchor planes coincident: EXACTLY mateTransformConcentric
        // (sign-preserving shortest arc + mate-point coincidence); roll stays free.
        R = shortestArc(zB, zA);
        pivotB = true;
        d = pA - pB;
        break;

    case CType::Ball:
        d = pA - pB;                                           // points coincide, orientation untouched
        break;

    case CType::PinSlot: {
        // Concentric PROJECTED: axes parallel (minimal), then move B's anchor
        // into A's SLOT PLANE (span of A's Z and X through A's origin) -- the
        // in-slot offset along A's X survives (that is the slot travel).
        R = minimalColinear(zB, zA); pivotB = true;
        const glm::dvec3 w = pB - pA;
        d = -glm::dot(w, yA) * yA;
        break;
    }
    }

    glm::dvec3 t = d;
    if (pivotB) t += pB - R * pB;                              // rotate about B's anchor point
    applyWorldRigid(*tc, R, t);
    return true;
}

// ===========================================================================
// DOF metadata -- see the table + honesty notes in Constraint.hpp.
// ===========================================================================
DofSpec lockedDof(CType t) {
    DofSpec d;
    switch (t) {
    case CType::Coincident:    d.tx = d.ty = d.tz = true; break;              // point form
    case CType::Concentric:    d.tx = d.ty = d.rx = d.ry = true; break;
    case CType::Parallel:      d.rx = d.ry = true; break;
    case CType::Perpendicular: d.ry = true; break;                            // one rotation (convention)
    case CType::Tangent:       d.tz = true; break;                            // one translation (contact normal)
    case CType::Flush:         d.tz = d.rx = d.ry = true; break;
    case CType::Distance:      d.tz = d.rx = d.ry = true; break;              // like Flush
    case CType::Angle:         d.ry = true; break;                            // one rotation (convention)
    case CType::Rigid:         d.tx = d.ty = d.tz = d.rx = d.ry = d.rz = true; break;
    case CType::Revolute:      d.tx = d.ty = d.tz = d.rx = d.ry = true; break;   // rz open
    case CType::Slider:        d.tx = d.ty = d.rx = d.ry = d.rz = true; break;   // tz open
    case CType::Cylindrical:   d.tx = d.ty = d.rx = d.ry = true; break;          // tz+rz open
    case CType::Ball:          d.tx = d.ty = d.tz = true; break;                 // rotations open
    case CType::PinSlot:       d.tx = d.ty = d.rx = d.ry = true; break;          // tz+rz open (slot approx.)
    }
    return d;
}

// ===========================================================================
// Human strings
// ===========================================================================
const char* cTypeName(CType t) {
    static const char* names[kCTypeCount] = {
        "Coincident", "Concentric", "Parallel", "Perpendicular", "Tangent", "Flush",
        "Distance", "Angle", "Rigid", "Revolute", "Slider", "Cylindrical", "Ball", "PinSlot"
    };
    const int i = int(t);
    return (i >= 0 && i < kCTypeCount) ? names[i] : "Unknown";
}

std::string describe(const Constraint& c) {
    auto side = [](const Anchor& a) {
        char b[48];
        if (a.edgeId >= 0)
            std::snprintf(b, sizeof b, "e%u/edge%d", unsigned(entt::to_integral(a.body)), a.edgeId);
        else if (a.faceId >= 0)
            std::snprintf(b, sizeof b, "e%u/face%d", unsigned(entt::to_integral(a.body)), a.faceId);
        else
            std::snprintf(b, sizeof b, "e%u/datum", unsigned(entt::to_integral(a.body)));
        return std::string(b);
    };
    char buf[64];
    std::string s = cTypeName(c.type);
    std::snprintf(buf, sizeof buf, " #%llu  ", static_cast<unsigned long long>(c.id));
    s += buf;
    s += side(c.a) + " <-> " + side(c.b);
    if (c.type == CType::Distance) { std::snprintf(buf, sizeof buf, "  offset=%.4g m", c.offset); s += buf; }
    if (c.type == CType::Angle)    { std::snprintf(buf, sizeof buf, "  angle=%.4g deg", c.angleDeg); s += buf; }
    if (c.suppressed) s += "  [suppressed]";
    if (c.driven)     s += "  [driven]";
    return s;
}

// ===========================================================================
// JSON -- entities as raw uint32 (ksave remaps on load); 64-bit keys as hex
// strings (QJson numbers are doubles: a 64-bit hash would lose bits).
// ===========================================================================
namespace {

const char* kSourceNames[6] = { "PlaneFace", "CylFace", "CircleEdge", "LineEdge", "Vertex", "SphereFace" };

QJsonArray vecToJson(const glm::vec3& v) {
    return QJsonArray{ double(v.x), double(v.y), double(v.z) };
}
bool vecFromJson(const QJsonValue& jv, glm::vec3& out) {
    if (!jv.isArray()) return false;
    const QJsonArray a = jv.toArray();
    if (a.size() != 3) return false;
    for (int i = 0; i < 3; ++i) if (!a[i].isDouble()) return false;
    out = glm::vec3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
    return true;
}

QJsonObject anchorToJson(const Anchor& a) {
    QJsonObject o;
    o["body"]   = double(entt::to_integral(a.body));     // raw entt uint32 (exact in a double)
    o["key"]    = QString("%1").arg(qulonglong(a.key), 16, 16, QChar('0'));
    o["faceId"] = a.faceId;
    o["edgeId"] = a.edgeId;
    o["pos"]    = vecToJson(a.pos);
    o["axisZ"]  = vecToJson(a.axisZ);
    o["axisX"]  = vecToJson(a.axisX);
    o["radius"] = double(a.radius);
    const int fi = int(a.from);
    o["from"]   = (fi >= 0 && fi < 6) ? kSourceNames[fi] : "PlaneFace";
    return o;
}
bool anchorFromJson(const QJsonValue& jv, Anchor& out) {
    if (!jv.isObject()) return false;
    const QJsonObject o = jv.toObject();
    if (!o.contains("body") || !o.contains("key") || !o["key"].isString()) return false;
    Anchor a;
    a.body = entt::entity(std::uint32_t(o["body"].toDouble()));
    bool ok = false;
    a.key = o["key"].toString().toULongLong(&ok, 16);
    if (!ok) return false;
    a.faceId = o["faceId"].toInt(-1);
    a.edgeId = o["edgeId"].toInt(-1);
    if (!vecFromJson(o["pos"], a.pos) || !vecFromJson(o["axisZ"], a.axisZ)
        || !vecFromJson(o["axisX"], a.axisX)) return false;
    a.radius = float(o["radius"].toDouble(0.0));
    const QString from = o["from"].toString();
    int fi = -1;
    for (int i = 0; i < 6; ++i) if (from == QLatin1String(kSourceNames[i])) { fi = i; break; }
    if (fi < 0) return false;
    a.from = AnchorSource(fi);
    out = a;
    return true;
}

} // namespace

QJsonObject toJson(const Constraint& c) {
    QJsonObject o;
    o["id"]         = qint64(c.id);
    o["type"]       = cTypeName(c.type);
    o["a"]          = anchorToJson(c.a);
    o["b"]          = anchorToJson(c.b);
    o["offset"]     = c.offset;
    o["angleDeg"]   = c.angleDeg;
    o["suppressed"] = c.suppressed;
    o["driven"]     = c.driven;
    return o;
}

bool fromJson(const QJsonObject& o, Constraint& out) {
    if (!o.contains("type") || !o["type"].isString()) return false;
    const QString tn = o["type"].toString();
    int ti = -1;
    for (int i = 0; i < kCTypeCount; ++i)
        if (tn == QLatin1String(cTypeName(CType(i)))) { ti = i; break; }
    if (ti < 0) return false;
    Constraint c;
    c.type = CType(ti);
    c.id   = std::uint64_t(o["id"].toDouble(0.0));
    if (!anchorFromJson(o["a"], c.a) || !anchorFromJson(o["b"], c.b)) return false;
    c.offset     = o["offset"].toDouble(0.0);
    c.angleDeg   = o["angleDeg"].toDouble(0.0);
    c.suppressed = o["suppressed"].toBool(false);
    c.driven     = o["driven"].toBool(false);
    out = c;
    return true;
}

QJsonObject toJson(const ConstraintGraphComponent& g) {
    QJsonObject o;
    o["format"]    = "kconstraint/1";
    o["nextId"]    = qint64(g.nextId);
    o["showIcons"] = g.showIcons;
    QJsonArray arr;
    for (const auto& c : g.constraints) arr.push_back(toJson(c));
    o["constraints"] = arr;
    return o;
}

bool fromJson(const QJsonObject& o, ConstraintGraphComponent& out) {
    if (!o["format"].toString().startsWith(QLatin1String("kconstraint/"))) return false;
    if (!o["constraints"].isArray()) return false;
    ConstraintGraphComponent g;
    g.nextId    = std::uint64_t(o["nextId"].toDouble(1.0));
    g.showIcons = o["showIcons"].toBool(true);
    const QJsonArray arr = o["constraints"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr[i].isObject()) return false;
        Constraint c;
        if (!fromJson(arr[i].toObject(), c)) return false;
        g.constraints.push_back(c);
    }
    out = std::move(g);
    return true;
}

// ===========================================================================
// CONSTRAINT gate (env KRS_CONSTRAINT_SELFTEST) -- headless, pure CPU,
// synthetic B-Rep entities in a Scene registry, real NEG-CTRLs. Every check
// prints measured numbers; the summary line is ALL PASS / FAILURES PRESENT.
// Mirrors the Measure.cpp gate style.
// ===========================================================================
namespace {

struct CSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[constraint]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

entt::entity gateBody(entt::registry& reg, const glm::vec3& t, const glm::quat& q) {
    const auto e = reg.create();
    reg.emplace<TransformComponent>(e, t, q, glm::vec3(1.0f));
    return e;
}
int addPlaneFace(entt::registry& reg, entt::entity e, const glm::vec3& pL, const glm::vec3& nL) {
    auto& fc = reg.get_or_emplace<BRepFaceComponent>(e);
    BRepFace f; f.type = 0; f.axisPos = pL; f.normal = glm::normalize(nL);
    f.faceKey = computeFaceKey(f);
    fc.faces.push_back(f);
    return int(fc.faces.size()) - 1;
}
int addCylFace(entt::registry& reg, entt::entity e, const glm::vec3& pL, const glm::vec3& dL,
               float r, float halfLen) {
    auto& fc = reg.get_or_emplace<BRepFaceComponent>(e);
    BRepFace f; f.type = 1; f.axisPos = pL; f.axisDir = glm::normalize(dL); f.radius = r;
    f.axisEnd0 = pL - halfLen * f.axisDir; f.axisEnd1 = pL + halfLen * f.axisDir;
    f.faceKey = computeFaceKey(f);
    fc.faces.push_back(f);
    return int(fc.faces.size()) - 1;
}
int addCircleEdge(entt::registry& reg, entt::entity e, const glm::vec3& cL, const glm::vec3& nL, float r) {
    auto& ec = reg.get_or_emplace<BRepEdgeComponent>(e);
    BRepEdge be; be.kind = BRepEdge::Circle; be.center = cL; be.axisDir = glm::normalize(nL);
    be.radius = r; be.closed = true;
    be.p0 = be.p1 = cL + r * anyPerpF(be.axisDir);
    be.polyline = { be.p0, be.p1 };
    be.edgeKey = computeEdgeKey(be);
    ec.edges.push_back(be);
    return int(ec.edges.size()) - 1;
}
int addLineEdge(entt::registry& reg, entt::entity e, const glm::vec3& p0L, const glm::vec3& p1L) {
    auto& ec = reg.get_or_emplace<BRepEdgeComponent>(e);
    BRepEdge be; be.kind = BRepEdge::Line; be.p0 = p0L; be.p1 = p1L;
    be.axisDir = glm::normalize(p1L - p0L);
    be.polyline = { p0L, p1L };
    be.edgeKey = computeEdgeKey(be);
    ec.edges.push_back(be);
    return int(ec.edges.size()) - 1;
}

// Synthetic EDGE Selection mirroring the documented resolveEdge contract
// (SelectionService.hpp:51-54; the production resolver lands with the edge-
// picking sprint): EdgeCircle -> axisPos=world centre, axisDir=plane normal;
// EdgeLine -> axisEnd0/1=endpoints, axisPos=midpoint. faceKey carries edgeKey.
krs::sel::Selection edgeSelection(entt::registry& reg, entt::entity e, int edgeId) {
    krs::sel::Selection s;
    const auto& be = reg.get<BRepEdgeComponent>(e).edges[std::size_t(edgeId)];
    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
    const glm::mat3 N = glm::mat3(glm::inverseTranspose(M));
    s.valid = true; s.entity = e; s.faceId = -1; s.edgeId = edgeId;
    s.faceKey = be.edgeKey;
    if (be.kind == BRepEdge::Circle) {
        s.type = krs::sel::FeatureType::EdgeCircle;
        s.axisPos = glm::vec3(M * glm::vec4(be.center, 1.0f));
        s.axisDir = glm::normalize(N * be.axisDir);
        s.radius = be.radius;
    } else {
        s.type = krs::sel::FeatureType::EdgeLine;
        s.axisEnd0 = glm::vec3(M * glm::vec4(be.p0, 1.0f));
        s.axisEnd1 = glm::vec3(M * glm::vec4(be.p1, 1.0f));
        s.axisDir = glm::normalize(N * be.axisDir);
        s.axisPos = 0.5f * (s.axisEnd0 + s.axisEnd1);
    }
    s.hitPoint = s.axisPos;
    return s;
}
krs::sel::Selection vertSelection(entt::entity e, const glm::vec3& worldPt) {
    krs::sel::Selection s;
    s.valid = true; s.entity = e; s.faceId = -1;
    s.type = krs::sel::FeatureType::Vertex;
    s.hitPoint = worldPt;
    return s;
}

// gate measurement helpers (double precision) --------------------------------
double axisCross(const AnchorWorldFrame& A, const AnchorWorldFrame& B) {
    return glm::length(glm::cross(glm::dvec3(A.z), glm::dvec3(B.z)));   // |sin(angle error)|
}
double axisDot(const AnchorWorldFrame& A, const AnchorWorldFrame& B) {
    return glm::dot(glm::dvec3(A.z), glm::dvec3(B.z));
}
double perpDistToAxis(const AnchorWorldFrame& A, const glm::vec3& p) {
    const glm::dvec3 w = glm::dvec3(p) - glm::dvec3(A.pos);
    const glm::dvec3 z = glm::normalize(glm::dvec3(A.z));
    return glm::length(w - glm::dot(w, z) * z);
}
double pointDist(const AnchorWorldFrame& A, const AnchorWorldFrame& B) {
    return glm::length(glm::dvec3(B.pos) - glm::dvec3(A.pos));
}
double planeGap(const AnchorWorldFrame& A, const AnchorWorldFrame& B) {   // signed, along A's normal
    return glm::dot(glm::dvec3(B.pos) - glm::dvec3(A.pos), glm::normalize(glm::dvec3(A.z)));
}
double angleDegBetween(const AnchorWorldFrame& A, const AnchorWorldFrame& B) {
    return glm::degrees(std::acos(std::clamp(axisDot(A, B), -1.0, 1.0)));
}

// standard gate poses (rotated + translated so every snap exercises real
// world-frame math; magnitudes ~0.3 m keep float representation error < 1e-7)
glm::quat rotA() { return glm::angleAxis(glm::radians(20.0f),  glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))); }
glm::quat rotB() { return glm::angleAxis(glm::radians(-40.0f), glm::normalize(glm::vec3(1.0f, 0.5f, -0.3f))); }
glm::vec3 posA() { return { 0.05f, -0.02f, 0.10f }; }
glm::vec3 posB() { return { 0.35f,  0.22f, -0.15f }; }

Constraint mk(CType t, const Anchor& a, const Anchor& b) {
    Constraint c; c.type = t; c.a = a; c.b = b; return c;
}

} // namespace

bool runConstraintGate() {
    std::printf("[constraint] ============ CONSTRAINT GATE (krs::constraint) ============\n");
    CSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();
    const double kAxisTol = 1e-5, kDistTol = 1e-6;

    // ---- (1) COINCIDENT, plane form: B's vertex point onto A's plane along the normal.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addPlaneFace(reg, eA, { 0.02f, 0.03f, 0.05f }, { 0, 0, 1 });
        const glm::vec3 vLocal(0.05f, 0.02f, 0.08f);
        const glm::vec3 vWorld = glm::vec3(reg.get<TransformComponent>(eB).getTransform() * glm::vec4(vLocal, 1.0f));
        Constraint c = mk(CType::Coincident,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, vertSelection(eB, vWorld)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double gap = std::abs(planeGap(A, B));
        std::printf("[constraint] coincident-plane: snapped=%d |point-plane gap|=%.3e m (want <1e-6)\n", int(ok), gap);
        S.check("SNAP-COINCIDENT-PLANE  vertex onto plane along the normal", ok && gap < kDistTol);
    }
    // ---- (2) COINCIDENT, point form: two vertex anchors coincide exactly.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const glm::vec3 pAW = glm::vec3(reg.get<TransformComponent>(eA).getTransform() * glm::vec4(0.01f, 0.02f, 0.03f, 1.0f));
        const glm::vec3 pBW = glm::vec3(reg.get<TransformComponent>(eB).getTransform() * glm::vec4(-0.02f, 0.05f, 0.01f, 1.0f));
        Constraint c = mk(CType::Coincident,
                          makeAnchor(reg, vertSelection(eA, pAW)),
                          makeAnchor(reg, vertSelection(eB, pBW)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double d = pointDist(A, B);
        std::printf("[constraint] coincident-point: snapped=%d |pB-pA|=%.3e m (want <1e-6)\n", int(ok), d);
        S.check("SNAP-COINCIDENT-POINT  point anchors coincide exactly", ok && d < kDistTol);
    }
    // ---- (3) CONCENTRIC: cylinder face (A) vs CIRCLE-EDGE anchor (B) -- the edge-anchor path.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.05f);
        const int gB = addCircleEdge(reg, eB, { 0.04f, 0.0f, 0.02f }, glm::normalize(glm::vec3(1, 1, 0)), 0.02f);
        Constraint c = mk(CType::Concentric,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, edgeSelection(reg, eB, gB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double mis = axisCross(A, B), pd = perpDistToAxis(A, B.pos);
        std::printf("[constraint] concentric(edge B): snapped=%d |cross(zA,zB)|=%.3e (want <1e-5) perpDist=%.3e m (want <1e-6)\n",
                    int(ok), mis, pd);
        S.check("SNAP-CONCENTRIC        circle-edge axis colinear with bore axis", ok && mis < kAxisTol && pd < kDistTol);
    }
    // ---- (4) PARALLEL, obtuse pair: minimal arc ends ANTI-parallel; B's anchor point stays put.
    {
        auto eA = gateBody(reg, { 0.0f, 0.0f, 0.1f }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, { 0.3f, 0.1f, 0.0f }, glm::quat(1, 0, 0, 0));
        const int fA = addPlaneFace(reg, eA, { 0, 0, 0.05f }, { 0, 0, 1 });
        const int gB = addLineEdge(reg, eB, { 0, 0, 0 }, glm::vec3(0.866f, 0.0f, -0.5f) * 0.2f);  // 120 deg to nA
        Constraint c = mk(CType::Parallel,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, edgeSelection(reg, eB, gB)));
        const auto B0 = anchorWorldFrame(reg, c.b);
        const double dot0 = axisDot(anchorWorldFrame(reg, c.a), B0);
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double mis = axisCross(A, B), dot1 = axisDot(A, B);
        const double moved = glm::length(glm::dvec3(B.pos) - glm::dvec3(B0.pos));
        std::printf("[constraint] parallel: snapped=%d dot before=%.6f after=%.9f (want -1: minimal arc)"
                    " |cross|=%.3e anchorMoved=%.3e m\n", int(ok), dot0, dot1, mis, moved);
        S.check("SNAP-PARALLEL          obtuse pair -> anti-parallel (smallest arc), pivot at B's anchor",
                ok && dot0 < 0.0 && mis < kAxisTol && dot1 < 0.0 && moved < kDistTol);
    }
    // ---- (5) PERPENDICULAR: 30 deg apart -> dot(axes) == 0.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.01f, 0.04f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, glm::normalize(glm::vec3(0.5f, 0.0f, 0.866f)), 0.01f, 0.04f);
        Constraint c = mk(CType::Perpendicular,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const double ang0 = angleDegBetween(anchorWorldFrame(reg, c.a), anchorWorldFrame(reg, c.b));
        const bool ok = applyConstraintSnap(reg, c);
        const double dot1 = std::abs(axisDot(anchorWorldFrame(reg, c.a), anchorWorldFrame(reg, c.b)));
        std::printf("[constraint] perpendicular: snapped=%d angle before=%.4f deg |dot| after=%.3e (want <1e-5)\n",
                    int(ok), ang0, dot1);
        S.check("SNAP-PERPENDICULAR     minimal rotation to dot(axes)=0", ok && dot1 < kAxisTol);
    }
    // ---- (6) TANGENT, plane(A)-cylinder(B): axis-to-plane distance == rB, side preserved.
    {
        auto eA = gateBody(reg, { 0, 0, 0 }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, { 0.2f, 0.31f, 0.05f }, glm::quat(1, 0, 0, 0));
        const int fA = addPlaneFace(reg, eA, { 0, 0.1f, 0 }, { 0, 1, 0 });
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, { 1, 0, 0 }, 0.03f, 0.06f);
        Constraint c = mk(CType::Tangent,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double s = planeGap(A, B);
        const double err = std::abs(std::abs(s) - 0.03);
        std::printf("[constraint] tangent plane-cyl: snapped=%d axis-plane dist=%.9f m (want 0.030000000, same side +) err=%.3e\n",
                    int(ok), s, err);
        S.check("SNAP-TANGENT-PLANE     cylinder rests on the plane (dist == r, side kept)",
                ok && err < kDistTol && s > 0.0);
    }
    // ---- (7) TANGENT, cylinder-cylinder EXTERNAL: axis distance == rA + rB.
    {
        auto eA = gateBody(reg, { 0, 0, 0 }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, { 0.12f, 0.05f, 0.02f }, glm::quat(1, 0, 0, 0));
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.03f, 0.05f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.04f);
        Constraint c = mk(CType::Tangent,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double dist = perpDistToAxis(A, B.pos);
        std::printf("[constraint] tangent cyl-cyl: snapped=%d axis distance=%.9f m (want 0.050000000 = rA+rB)\n",
                    int(ok), dist);
        S.check("SNAP-TANGENT-CYLCYL    external tangency (axis distance == rA+rB)",
                ok && std::abs(dist - 0.05) < kDistTol);
    }
    // ---- (8) FLUSH: normals anti-parallel + coplanar (the mating-faces relation).
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addPlaneFace(reg, eA, { 0.01f, 0.0f, 0.06f }, { 0, 0, 1 });
        const int fB = addPlaneFace(reg, eB, { 0.0f, 0.02f, -0.04f }, glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
        Constraint c = mk(CType::Flush,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double anti = 1.0 + axisDot(A, B), gap = std::abs(planeGap(A, B));
        std::printf("[constraint] flush: snapped=%d 1+dot(nA,nB)=%.3e (want <1e-5) |coplanar gap|=%.3e m (want <1e-6)\n",
                    int(ok), anti, gap);
        S.check("SNAP-FLUSH             normals ANTI-parallel + coplanar", ok && anti < kAxisTol && gap < kDistTol);
    }
    // ---- (9) DISTANCE: Flush with a signed 0.025 m gap along A's normal.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addPlaneFace(reg, eA, { 0.01f, 0.0f, 0.06f }, { 0, 0, 1 });
        const int fB = addPlaneFace(reg, eB, { 0.0f, 0.02f, -0.04f }, glm::normalize(glm::vec3(-0.3f, 0.4f, 1.0f)));
        Constraint c = mk(CType::Distance,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        c.offset = 0.025;
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double anti = 1.0 + axisDot(A, B), gap = planeGap(A, B);
        std::printf("[constraint] distance: snapped=%d gap=%.9f m (want 0.025000000 signed) 1+dot=%.3e\n",
                    int(ok), gap, anti);
        S.check("SNAP-DISTANCE          plane gap == offset (signed), normals anti-parallel",
                ok && anti < kAxisTol && std::abs(gap - 0.025) < kDistTol);
    }
    // ---- (10) ANGLE: axes to exactly 35 deg about the common perpendicular.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.01f, 0.04f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, glm::normalize(glm::vec3(1, 0.2f, 0.4f)), 0.01f, 0.04f);
        Constraint c = mk(CType::Angle,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        c.angleDeg = 35.0;
        const double ang0 = angleDegBetween(anchorWorldFrame(reg, c.a), anchorWorldFrame(reg, c.b));
        const bool ok = applyConstraintSnap(reg, c);
        const double ang1 = angleDegBetween(anchorWorldFrame(reg, c.a), anchorWorldFrame(reg, c.b));
        std::printf("[constraint] angle: snapped=%d angle before=%.4f deg after=%.7f deg (want 35.0000000)\n",
                    int(ok), ang0, ang1);
        S.check("SNAP-ANGLE             axis angle == angleDeg exactly", ok && std::abs(ang1 - 35.0) < 1e-3);
    }
    // ---- (11) RIGID: full frame lock (origin + Z + X all match).
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0.02f, 0.0f, 0.01f }, { 0, 0, 1 }, 0.015f, 0.03f);
        const int fB = addCylFace(reg, eB, { 0.0f, 0.03f, 0.0f }, { 0, 1, 0 }, 0.015f, 0.03f);
        Constraint c = mk(CType::Rigid,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double d = pointDist(A, B);
        const double ez = 1.0 - axisDot(A, B);
        const double ex = 1.0 - glm::dot(glm::dvec3(A.x), glm::dvec3(B.x));
        std::printf("[constraint] rigid: snapped=%d |pB-pA|=%.3e m 1-dot(z)=%.3e 1-dot(x)=%.3e\n", int(ok), d, ez, ex);
        S.check("SNAP-RIGID             B's anchor frame locked onto A's (pos + Z + X)",
                ok && d < kDistTol && ez < kAxisTol && ex < kAxisTol);
    }
    // ---- (12) REVOLUTE: concentric + mate points coincide, SAME axis sense even
    //           from an obtuse start (mateTransformConcentric semantics).
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0.02f }, { 0, 0, 1 }, 0.02f, 0.05f);
        const int fB = addCylFace(reg, eB, { 0.01f, 0, 0 }, { 0, 0, -1 }, 0.02f, 0.05f);   // opposed sense
        Constraint c = mk(CType::Revolute,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double d = pointDist(A, B), sameSense = axisDot(A, B);
        std::printf("[constraint] revolute: snapped=%d |pB-pA|=%.3e m dot(zA,zB)=%.9f (want +1: sign-preserving)\n",
                    int(ok), d, sameSense);
        S.check("SNAP-REVOLUTE          coaxial + points coincide + axis sense preserved",
                ok && d < kDistTol && (1.0 - sameSense) < kAxisTol);
    }
    // ---- (13) SLIDER: axes colinear + roll locked; the along-axis offset is PRESERVED (tz open).
    {
        auto eA = gateBody(reg, { 0.0f, 0.0f, 0.1f }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.01f, 0.05f);
        const int fB = addCylFace(reg, eB, { 0.02f, 0.0f, 0.0f }, glm::normalize(glm::vec3(0.3f, 1, 0.2f)), 0.01f, 0.05f);
        Constraint c = mk(CType::Slider,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const auto A0 = anchorWorldFrame(reg, c.a), B0 = anchorWorldFrame(reg, c.b);
        const double sExp = glm::dot(glm::dvec3(B0.pos) - glm::dvec3(A0.pos), glm::normalize(glm::dvec3(A0.z)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double mis = 1.0 - axisDot(A, B);
        const double rollErr = 1.0 - glm::dot(glm::dvec3(A.x), glm::dvec3(B.x));
        const double pd = perpDistToAxis(A, B.pos);
        const double s = planeGap(A, B);
        std::printf("[constraint] slider: snapped=%d 1-dot(z)=%.3e 1-dot(x)=%.3e perpDist=%.3e m"
                    " along-axis=%.9f m (expected %.9f, PRESERVED, |exp|>0.01)\n",
                    int(ok), mis, rollErr, pd, s, sExp);
        S.check("SNAP-SLIDER            colinear + roll locked, along-axis offset preserved (tz open)",
                ok && mis < kAxisTol && rollErr < kAxisTol && pd < kDistTol
                   && std::abs(s - sExp) < kDistTol && std::abs(sExp) > 0.01);
    }
    // ---- (14) CYLINDRICAL: positions like Concentric (coaxial, along-axis + roll free).
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0.01f, 0 }, { 0, 0, 1 }, 0.012f, 0.05f);
        const int fB = addCylFace(reg, eB, { 0.03f, 0, 0.01f }, { 0, 1, 0 }, 0.012f, 0.05f);
        Constraint c = mk(CType::Cylindrical,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double mis = axisCross(A, B), pd = perpDistToAxis(A, B.pos);
        std::printf("[constraint] cylindrical: snapped=%d |cross|=%.3e perpDist=%.3e m\n", int(ok), mis, pd);
        S.check("SNAP-CYLINDRICAL       coaxial (tz + rz stay open)", ok && mis < kAxisTol && pd < kDistTol);
    }
    // ---- (15) BALL: points coincide, orientation BIT-identical (never touched).
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const glm::vec3 pAW = glm::vec3(reg.get<TransformComponent>(eA).getTransform() * glm::vec4(0.02f, 0.0f, 0.04f, 1.0f));
        const glm::vec3 pBW = glm::vec3(reg.get<TransformComponent>(eB).getTransform() * glm::vec4(0.0f, -0.03f, 0.01f, 1.0f));
        Constraint c = mk(CType::Ball,
                          makeAnchor(reg, vertSelection(eA, pAW)),
                          makeAnchor(reg, vertSelection(eB, pBW)));
        const glm::quat q0 = reg.get<TransformComponent>(eB).rotation;
        const bool ok = applyConstraintSnap(reg, c);
        const glm::quat q1 = reg.get<TransformComponent>(eB).rotation;
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double d = pointDist(A, B);
        const bool rotUntouched = (q0.w == q1.w && q0.x == q1.x && q0.y == q1.y && q0.z == q1.z);
        std::printf("[constraint] ball: snapped=%d |pB-pA|=%.3e m rotationUntouched=%d\n", int(ok), d, int(rotUntouched));
        S.check("SNAP-BALL              points coincide, orientation untouched (rx/ry/rz open)",
                ok && d < kDistTol && rotUntouched);
    }
    // ---- (16) PINSLOT: axes parallel + B in A's slot plane; the in-slot X offset SURVIVES.
    {
        auto eA = gateBody(reg, { 0, 0, 0 }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, { 0.15f, 0.08f, 0.03f }, rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.01f, 0.03f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, glm::normalize(glm::vec3(0.2f, 0.3f, 1)), 0.01f, 0.03f);
        Constraint c = mk(CType::PinSlot,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        c.a.axisX = { 1, 0, 0 };                              // the SLOT direction (UI-authored datum)
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const glm::dvec3 w = glm::dvec3(B.pos) - glm::dvec3(A.pos);
        const glm::dvec3 yA = glm::cross(glm::dvec3(A.z), glm::dvec3(A.x));
        const double mis = axisCross(A, B);
        const double offPlane = std::abs(glm::dot(w, yA));
        const double alongSlot = glm::dot(w, glm::dvec3(A.x));
        std::printf("[constraint] pinslot: snapped=%d |cross|=%.3e off-slot-plane=%.3e m (want <1e-6)"
                    " along-slot=%.6f m (preserved, want >0.01)\n", int(ok), mis, offPlane, alongSlot);
        S.check("SNAP-PINSLOT           axes parallel, anchor in the slot plane, slot travel preserved",
                ok && mis < kAxisTol && offPlane < kDistTol && std::abs(alongSlot) > 0.01);
    }

    // ---- (17) SUPPRESSED: refused, ZERO motion.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addPlaneFace(reg, eA, { 0, 0, 0.02f }, { 0, 0, 1 });
        const int fB = addPlaneFace(reg, eB, { 0, 0, 0.02f }, { 0, 1, 0 });
        Constraint c = mk(CType::Flush,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        c.suppressed = true;
        const TransformComponent before = reg.get<TransformComponent>(eB);
        const bool refused = !applyConstraintSnap(reg, c);
        const TransformComponent& after = reg.get<TransformComponent>(eB);
        const bool still = (before.translation == after.translation)
                        && (before.rotation.w == after.rotation.w && before.rotation.x == after.rotation.x
                            && before.rotation.y == after.rotation.y && before.rotation.z == after.rotation.z);
        std::printf("[constraint] suppressed: refused=%d motion=%d (want refused, none)\n", int(refused), int(!still));
        S.check("REFUSE-SUPPRESSED      suppressed constraint -> false, zero motion", refused && still);
    }
    // ---- (18) ROBOT MEMBER: b.body kinematically owned -> refused, zero motion.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        auto eC = gateBody(reg, posB() + glm::vec3(0.1f), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.04f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, { 0, 1, 0 }, 0.02f, 0.04f);
        const int fC = addCylFace(reg, eC, { 0, 0, 0 }, { 1, 0, 0 }, 0.02f, 0.04f);
        reg.emplace<RobotSubcomponentComponent>(eB, 7);
        reg.emplace<RobotRootComponent>(eC, RobotRootComponent{ "gate-bot", 7 });
        Constraint c1 = mk(CType::Concentric,
                           makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                           makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        Constraint c2 = mk(CType::Concentric,
                           makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                           makeAnchor(reg, krs::sel::resolveFace(reg, eC, fC)));
        const glm::vec3 tB = reg.get<TransformComponent>(eB).translation;
        const glm::vec3 tC = reg.get<TransformComponent>(eC).translation;
        const bool r1 = !applyConstraintSnap(reg, c1);
        const bool r2 = !applyConstraintSnap(reg, c2);
        const bool still = (tB == reg.get<TransformComponent>(eB).translation)
                        && (tC == reg.get<TransformComponent>(eC).translation);
        std::printf("[constraint] robot-member: subcomponent refused=%d root refused=%d motion=%d\n",
                    int(r1), int(r2), int(!still));
        S.check("REFUSE-ROBOT-MEMBER    RobotSubcomponent/RobotRoot b.body -> false, zero motion",
                r1 && r2 && still);
    }
    // ---- (19) KEY RE-FIND (faces): scramble the face array; the stale faceId hint is
    //           overruled by the durable faceKey and the snap still lands.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.05f);
        addCylFace(reg, eB, { 0.08f, 0.0f, 0.0f }, { 1, 0, 0 }, 0.01f, 0.03f);            // face 0 (decoy)
        const int fB = addCylFace(reg, eB, { 0.0f, 0.05f, 0.02f }, { 0, 1, 0 }, 0.02f, 0.04f);   // face 1 (ours)
        Constraint c = mk(CType::Concentric,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        auto& faces = reg.get<BRepFaceComponent>(eB).faces;
        std::swap(faces[0], faces[1]);                       // re-import reorder: faceId 1 now the decoy
        const auto stale = anchorWorldFrame(reg, c.b);
        const bool ok = applyConstraintSnap(reg, c);
        const auto A = anchorWorldFrame(reg, c.a), B = anchorWorldFrame(reg, c.b);
        const double mis = axisCross(A, B), pd = perpDistToAxis(A, B.pos);
        std::printf("[constraint] key-refind-face: staleHint resolved=%d snapped=%d |cross|=%.3e perpDist=%.3e m\n",
                    int(stale.valid), int(ok), mis, pd);
        S.check("KEY-REFIND-FACE        faceId scramble -> re-found by faceKey, snap still lands",
                stale.valid && ok && mis < kAxisTol && pd < kDistTol);
    }
    // ---- (20) KEY RE-FIND (edges): same contract for edgeKey.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const glm::vec3 pAW = glm::vec3(reg.get<TransformComponent>(eA).getTransform() * glm::vec4(0.01f, 0.01f, 0.0f, 1.0f));
        addCircleEdge(reg, eB, { 0.07f, 0.0f, 0.0f }, { 1, 0, 0 }, 0.008f);               // edge 0 (decoy)
        const int gB = addCircleEdge(reg, eB, { 0.0f, 0.04f, 0.01f }, { 0, 1, 0 }, 0.015f);   // edge 1 (ours)
        Constraint c = mk(CType::Ball,
                          makeAnchor(reg, vertSelection(eA, pAW)),
                          makeAnchor(reg, edgeSelection(reg, eB, gB)));
        auto& edges = reg.get<BRepEdgeComponent>(eB).edges;
        std::swap(edges[0], edges[1]);
        const bool ok = applyConstraintSnap(reg, c);
        const double d = pointDist(anchorWorldFrame(reg, c.a), anchorWorldFrame(reg, c.b));
        std::printf("[constraint] key-refind-edge: snapped=%d |pB-pA|=%.3e m\n", int(ok), d);
        S.check("KEY-REFIND-EDGE        edgeId scramble -> re-found by edgeKey, snap still lands",
                ok && d < kDistTol);
    }
    // ---- (21) NEG-CTRL stale key: a key that exists NOWHERE -> refused, zero motion.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.04f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, { 0, 1, 0 }, 0.02f, 0.04f);
        Constraint c = mk(CType::Concentric,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        c.b.key = 0xDEADBEEFCAFEF00Dull;                     // stale: not on the body
        const glm::vec3 tB = reg.get<TransformComponent>(eB).translation;
        const bool refused = !applyConstraintSnap(reg, c);
        const bool still = (tB == reg.get<TransformComponent>(eB).translation);
        std::printf("[constraint] neg stale-key: refused=%d motion=%d\n", int(refused), int(!still));
        S.check("NEG-CTRL STALE-KEY     unknown key -> anchor unresolvable, refused, no crash", refused && still);
    }
    // ---- (22) NEG-CTRL dead entity: b.body destroyed -> refused, no crash.
    {
        auto eA = gateBody(reg, posA(), rotA());
        auto eB = gateBody(reg, posB(), rotB());
        const int fA = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.04f);
        const int fB = addCylFace(reg, eB, { 0, 0, 0 }, { 0, 1, 0 }, 0.02f, 0.04f);
        Constraint c = mk(CType::Concentric,
                          makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                          makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB)));
        reg.destroy(eB);
        const bool refused = !applyConstraintSnap(reg, c);
        std::printf("[constraint] neg dead-entity: refused=%d\n", int(refused));
        S.check("NEG-CTRL DEAD-ENTITY   destroyed b.body -> refused, no crash", refused);
    }

    // ---- (23) JSON round-trip: full graph through TEXT and back, field-exact.
    {
        ConstraintGraphComponent g;
        Constraint c1;
        c1.id = 3; c1.type = CType::Distance; c1.offset = 0.0125; c1.suppressed = true;
        c1.a.body = entt::entity(12); c1.a.key = 0xF123456789ABCDEFull; c1.a.faceId = 4;
        c1.a.pos = { 0.125f, -0.5f, 0.75f }; c1.a.axisZ = { 0, 0, 1 }; c1.a.axisX = { 1, 0, 0 };
        c1.a.radius = 0.033f; c1.a.from = AnchorSource::PlaneFace;
        c1.b.body = entt::entity(99); c1.b.key = 0x8000000000000001ull; c1.b.edgeId = 2; c1.b.faceId = -1;
        c1.b.pos = { -0.25f, 0.0625f, 0.5f }; c1.b.axisZ = { 0, 1, 0 }; c1.b.axisX = { 0, 0, 1 };
        c1.b.radius = 0.01f; c1.b.from = AnchorSource::CircleEdge;
        Constraint c2;
        c2.id = 7; c2.type = CType::Revolute; c2.angleDeg = 12.5; c2.driven = true;
        c2.a.body = entt::entity(1); c2.a.from = AnchorSource::CylFace; c2.a.faceId = 0; c2.a.key = 42;
        c2.b.body = entt::entity(2); c2.b.from = AnchorSource::CylFace; c2.b.faceId = 1; c2.b.key = 43;
        g.constraints = { c1, c2 };
        g.nextId = 8; g.showIcons = false;

        const QByteArray text = QJsonDocument(toJson(g)).toJson();
        ConstraintGraphComponent h;
        const bool parsed = fromJson(QJsonDocument::fromJson(text).object(), h);
        auto anchorEq = [](const Anchor& x, const Anchor& y) {
            return x.body == y.body && x.key == y.key && x.faceId == y.faceId && x.edgeId == y.edgeId
                && x.pos == y.pos && x.axisZ == y.axisZ && x.axisX == y.axisX
                && x.radius == y.radius && x.from == y.from;
        };
        auto eq = [&](const Constraint& x, const Constraint& y) {
            return x.id == y.id && x.type == y.type && anchorEq(x.a, y.a) && anchorEq(x.b, y.b)
                && x.offset == y.offset && x.angleDeg == y.angleDeg
                && x.suppressed == y.suppressed && x.driven == y.driven;
        };
        const bool same = parsed && h.constraints.size() == 2 && h.nextId == 8 && !h.showIcons
                       && eq(h.constraints[0], c1) && eq(h.constraints[1], c2);
        std::printf("[constraint] json: parsed=%d n=%zu nextId=%llu keyA=%016llx (64-bit hex string survives)\n",
                    int(parsed), h.constraints.size(), static_cast<unsigned long long>(h.nextId),
                    static_cast<unsigned long long>(parsed && !h.constraints.empty() ? h.constraints[0].a.key : 0));
        S.check("JSON-ROUNDTRIP         graph -> text -> graph, every field exact (incl. 64-bit keys)", same);

        Constraint junk;
        const bool n1 = !fromJson(QJsonObject{ { "type", "Bogus" } }, junk);
        QJsonObject noAnchors; noAnchors["type"] = "Rigid";
        const bool n2 = !fromJson(noAnchors, junk);
        std::printf("[constraint] json neg: unknownType rejected=%d missingAnchors rejected=%d\n", int(n1), int(n2));
        S.check("NEG-CTRL JSON          unknown type / missing anchors -> fromJson false", n1 && n2);
    }

    // ---- (24) DOF TABLE audit: the exact contract the PxD6 layer maps.
    {
        struct Row { CType t; bool tx, ty, tz, rx, ry, rz; };
        static const Row want[kCTypeCount] = {
            { CType::Coincident,    true,  true,  true,  false, false, false },
            { CType::Concentric,    true,  true,  false, true,  true,  false },
            { CType::Parallel,      false, false, false, true,  true,  false },
            { CType::Perpendicular, false, false, false, false, true,  false },
            { CType::Tangent,       false, false, true,  false, false, false },
            { CType::Flush,         false, false, true,  true,  true,  false },
            { CType::Distance,      false, false, true,  true,  true,  false },
            { CType::Angle,         false, false, false, false, true,  false },
            { CType::Rigid,         true,  true,  true,  true,  true,  true  },
            { CType::Revolute,      true,  true,  true,  true,  true,  false },
            { CType::Slider,        true,  true,  false, true,  true,  true  },
            { CType::Cylindrical,   true,  true,  false, true,  true,  false },
            { CType::Ball,          true,  true,  true,  false, false, false },
            { CType::PinSlot,       true,  true,  false, true,  true,  false },
        };
        bool all = true;
        for (const auto& w : want) {
            const DofSpec d = lockedDof(w.t);
            const bool row = d.tx == w.tx && d.ty == w.ty && d.tz == w.tz
                          && d.rx == w.rx && d.ry == w.ry && d.rz == w.rz;
            std::printf("[constraint] dof %-13s locks[%s%s%s%s%s%s] %s\n", cTypeName(w.t),
                        d.tx ? "tx " : "-- ", d.ty ? "ty " : "-- ", d.tz ? "tz " : "-- ",
                        d.rx ? "rx " : "-- ", d.ry ? "ry " : "-- ", d.rz ? "rz" : "--",
                        row ? "ok" : "MISMATCH");
            all = all && row;
        }
        S.check("DOF-TABLE              all 14 types match the contract (Revolute frees rz, Slider tz, ...)", all);
    }

    // ---- (25) CHAINED linkage: A-B revolute then B-C revolute -> a consistent 2-joint chain.
    {
        auto eA = gateBody(reg, { 0, 0, 0 }, glm::quat(1, 0, 0, 0));
        auto eB = gateBody(reg, { 0.4f, 0.1f, 0.0f }, glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(1, 2, 3))));
        auto eC = gateBody(reg, { -0.2f, 0.3f, 0.25f }, glm::angleAxis(glm::radians(60.0f), glm::normalize(glm::vec3(-1, 1, 0.5f))));
        const int fA  = addCylFace(reg, eA, { 0, 0, 0 }, { 0, 0, 1 }, 0.02f, 0.05f);
        const int fB0 = addCylFace(reg, eB, { -0.1f, 0.0f, 0.0f }, { 1, 0, 0 }, 0.02f, 0.04f);
        const int fB1 = addCylFace(reg, eB, { 0.1f, 0.05f, 0.0f }, { 0, 1, 0 }, 0.015f, 0.04f);
        const int fC  = addCylFace(reg, eC, { 0.03f, 0.0f, 0.02f }, { 0, 0, 1 }, 0.015f, 0.04f);
        Constraint cAB = mk(CType::Revolute,
                            makeAnchor(reg, krs::sel::resolveFace(reg, eA, fA)),
                            makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB0)));
        const bool ok1 = applyConstraintSnap(reg, cAB);      // B snaps onto A
        Constraint cBC = mk(CType::Revolute,
                            makeAnchor(reg, krs::sel::resolveFace(reg, eB, fB1)),   // authored POST-snap
                            makeAnchor(reg, krs::sel::resolveFace(reg, eC, fC)));
        const bool ok2 = applyConstraintSnap(reg, cBC);      // C snaps onto B; B (the A-side) stays
        const auto A  = anchorWorldFrame(reg, cAB.a), B0 = anchorWorldFrame(reg, cAB.b);
        const auto B1 = anchorWorldFrame(reg, cBC.a), C  = anchorWorldFrame(reg, cBC.b);
        const double dBC = pointDist(B1, C), zBC = 1.0 - axisDot(B1, C);
        const double dAB = pointDist(A, B0), zAB = 1.0 - axisDot(A, B0);
        std::printf("[constraint] chained: snap1=%d snap2=%d C-vs-B: |dp|=%.3e 1-dot=%.3e"
                    "  A-vs-B intact: |dp|=%.3e 1-dot=%.3e\n", int(ok1), int(ok2), dBC, zBC, dAB, zAB);
        S.check("CHAINED-REVOLUTE       C coaxial+coincident on its B anchor after sequential snaps",
                ok1 && ok2 && dBC < kDistTol && zBC < kAxisTol);
        S.check("CHAINED-UPSTREAM       the A-B joint survives the B-C snap (B was the fixed side)",
                dAB < kDistTol && zAB < kAxisTol);
    }

    // ---- (26) ctx singleton accessor + describe() smoke.
    {
        auto& g1 = constraintGraph(reg);
        auto& g2 = constraintGraph(reg);
        Constraint c; c.id = g2.nextId++; c.type = CType::Concentric;
        c.a.body = entt::entity(3); c.a.faceId = 1;
        c.b.body = entt::entity(5); c.b.edgeId = 0;
        g2.constraints.push_back(c);
        const std::string s = describe(c);
        std::printf("[constraint] ctx: same instance=%d n=%zu nextId=%llu describe='%s'\n",
                    int(&g1 == &g2), g1.constraints.size(),
                    static_cast<unsigned long long>(g1.nextId), s.c_str());
        S.check("CTX-SINGLETON          constraintGraph() get-or-emplace + id allocation + describe()",
                &g1 == &g2 && g1.constraints.size() == 1 && g1.nextId == 2
                && s.find("Concentric") != std::string::npos && s.find("#1") != std::string::npos);
    }

    const bool pass = (S.pass == S.total);
    std::printf("[constraint] %d/%d checks\n", S.pass, S.total);
    std::printf("[constraint] %s\n", pass
        ? "ALL PASS (every CType snapped + verified analytically; suppressed/robot/stale-key/dead-entity"
          " refusals with zero motion; key re-find over face+edge scrambles; JSON text round-trip exact;"
          " DOF table audited; chained revolute linkage consistent)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::constraint
