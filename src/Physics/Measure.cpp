// Measure.cpp -- see Measure.hpp. krs::measure: the measurement math behind the
// Measure tool (world-space diameter/length/area of a picked B-Rep feature, and
// feature-to-feature distance), plus the headless MEASURE gate.
#include "Measure.hpp"

#include "components.hpp"   // TransformComponent, RenderableMeshComponent, BRepFace(Component), Vertex
#include "Scene.hpp"        // gate registry host (same pattern as the other gates)

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace krs::measure {

namespace {

// Two unit vectors spanning the plane perpendicular to (unit) d -- same basis
// construction the selection indicator uses (SelectionService.hpp).
void perpBasis(const glm::vec3& d, glm::vec3& u, glm::vec3& v) {
    u = glm::cross(d, glm::vec3(0.0f, 0.0f, 1.0f));
    if (glm::dot(u, u) < 1e-8f) u = glm::cross(d, glm::vec3(0.0f, 1.0f, 0.0f));
    u = glm::normalize(u);
    v = glm::normalize(glm::cross(d, u));
}

// WORLD-space surface area of the triangles whose triFace == faceId, plus the
// area-weighted centroid (left untouched when the face has no triangles, so the
// caller's fallback anchor survives). Double accumulation; area is NEVER
// fabricated -- a face no triangle references reports exactly 0.
double faceWorldArea(const RenderableMeshComponent& mesh, const glm::mat4& M,
                     int faceId, glm::dvec3& centroid) {
    const std::size_t nTri = std::min(mesh.indices.size() / 3, mesh.triFace.size());
    const std::size_t nVerts = mesh.vertices.size();
    double area = 0.0;
    glm::dvec3 cAcc(0.0);
    for (std::size_t t = 0; t < nTri; ++t) {
        if (mesh.triFace[t] != faceId) continue;
        const unsigned i0 = mesh.indices[3 * t + 0];
        const unsigned i1 = mesh.indices[3 * t + 1];
        const unsigned i2 = mesh.indices[3 * t + 2];
        if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) continue;   // malformed tri: skip, no crash
        const glm::dvec3 a(glm::vec3(M * glm::vec4(mesh.vertices[i0].position, 1.0f)));
        const glm::dvec3 b(glm::vec3(M * glm::vec4(mesh.vertices[i1].position, 1.0f)));
        const glm::dvec3 c(glm::vec3(M * glm::vec4(mesh.vertices[i2].position, 1.0f)));
        const double triA = 0.5 * glm::length(glm::cross(b - a, c - a));
        area += triA;
        cAcc += triA * ((a + b + c) / 3.0);
    }
    if (area > 0.0) centroid = cAcc / area;
    return area;
}

} // namespace

// ---------------------------------------------------------------------------
// Measurement::text() -- kind + ONLY the applicable (non-zero) fields, SI
// meters, 4 significant figures. The UI converts units; this never does.
// ---------------------------------------------------------------------------
std::string Measurement::text() const {
    if (!ok) return "invalid";
    std::string s = kind.empty() ? std::string("feature") : kind;
    char buf[48];
    if (diameterM > 0.0) { std::snprintf(buf, sizeof buf, "  D=%.4g m",   diameterM); s += buf; }
    if (lengthM   > 0.0) { std::snprintf(buf, sizeof buf, "  L=%.4g m",   lengthM);   s += buf; }
    if (areaM2    > 0.0) { std::snprintf(buf, sizeof buf, "  A=%.4g m^2", areaM2);    s += buf; }
    return s;
}

// ---------------------------------------------------------------------------
// measureFeature -- ONE feature, WORLD space (see Measure.hpp for the rules).
// ---------------------------------------------------------------------------
Measurement measureFeature(entt::registry& reg, entt::entity e, int faceId) {
    Measurement m;
    if (!reg.valid(e) || !reg.all_of<BRepFaceComponent>(e)) return m;       // no B-Rep -> ok=false
    const auto& faces = reg.get<BRepFaceComponent>(e).faces;
    if (faceId < 0 || faceId >= int(faces.size())) return m;                // out of range -> ok=false
    const BRepFace& bf = faces[std::size_t(faceId)];

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
    const glm::mat3 L(M);                                   // linear part (rotation*scale) for directions
    const glm::vec3 wPos = glm::vec3(M * glm::vec4(bf.axisPos, 1.0f));

    // AREA: sum of this face's WORLD-space triangle areas. Zero triangles -> 0,
    // never fabricated. Centroid falls back to the transformed axisPos.
    glm::dvec3 centroid(wPos);
    if (const auto* mesh = reg.try_get<RenderableMeshComponent>(e))
        m.areaM2 = faceWorldArea(*mesh, M, faceId, centroid);

    switch (bf.type) {
    case 0: {                                               // PLANE: area only
        m.kind = "plane";
        m.a = m.b = (m.areaM2 > 0.0) ? glm::vec3(centroid) : wPos;  // area-weighted world centroid
        break;
    }
    case 1:                                                 // CYLINDER
    case 2: {                                               // CONE (base diameter from the ref radius)
        m.kind = (bf.type == 1) ? "cylinder" : "cone";
        // DIAMETER: 2*radius scaled by the MEAN of the two linear-map scale factors
        // PERPENDICULAR to the axis (non-uniform scale -> elliptic section; the mean
        // is the honest single-number report -- documented in Measure.hpp).
        const glm::vec3 dLocal = (glm::dot(bf.axisDir, bf.axisDir) > 1e-12f)
                                 ? glm::normalize(bf.axisDir) : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 u, v;
        perpBasis(dLocal, u, v);
        const double su = double(glm::length(L * u));
        const double sv = double(glm::length(L * v));
        m.diameterM = 2.0 * double(bf.radius) * 0.5 * (su + sv);
        // LENGTH: world rim-to-rim (axisEnd0..axisEnd1). BOTH-zero rims are the
        // untrimmed/synthetic sentinel (components.hpp) -> no length claim.
        const bool rims = glm::dot(bf.axisEnd0, bf.axisEnd0) > 1e-12f
                       || glm::dot(bf.axisEnd1, bf.axisEnd1) > 1e-12f;
        if (rims) {
            m.a = glm::vec3(M * glm::vec4(bf.axisEnd0, 1.0f));
            m.b = glm::vec3(M * glm::vec4(bf.axisEnd1, 1.0f));
            m.lengthM = glm::length(glm::dvec3(m.b) - glm::dvec3(m.a));
        } else {
            m.a = m.b = wPos;
        }
        break;
    }
    case 3: {                                               // SPHERE: diameter + centre anchor
        m.kind = "sphere";
        // ellipsoid approximation under non-uniform scale: mean of the three axis scales.
        const double s = (double(glm::length(L[0])) + double(glm::length(L[1]))
                          + double(glm::length(L[2]))) / 3.0;
        m.diameterM = 2.0 * double(bf.radius) * s;
        m.a = m.b = wPos;
        break;
    }
    default: {                                              // OTHER: anchor + area only
        m.kind = "feature";
        m.a = m.b = (m.areaM2 > 0.0) ? glm::vec3(centroid) : wPos;
        break;
    }
    }
    m.ok = true;
    return m;
}

// ---------------------------------------------------------------------------
// measureDistance -- anchor-to-anchor distance between two resolved features.
// Anchor = midpoint(a,b) of the feature measurement: cylinder/cone -> midpoint
// of the two rim centres (a==b==axisPos when rims unset, so the midpoint
// degrades gracefully), plane -> area-weighted centroid, sphere -> centre.
// ---------------------------------------------------------------------------
Measurement measureDistance(entt::registry& reg, entt::entity ea, int faceIdA,
                            entt::entity eb, int faceIdB) {
    Measurement m;
    m.kind = "distance";
    const Measurement fa = measureFeature(reg, ea, faceIdA);
    const Measurement fb = measureFeature(reg, eb, faceIdB);
    if (!fa.ok || !fb.ok) return m;                          // either side unresolved -> ok=false
    m.a = 0.5f * (fa.a + fa.b);
    m.b = 0.5f * (fb.a + fb.b);
    m.lengthM = glm::length(glm::dvec3(m.b) - glm::dvec3(m.a));
    m.ok = true;
    return m;
}

// ===========================================================================
// MEASURE MODE (Onshape-style) -- pair/single readout over viewport selections.
// ===========================================================================
namespace {

double angleDegBetween(const glm::dvec3& a, const glm::dvec3& b) {
    const double c = std::abs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(std::acos(std::clamp(c, 0.0, 1.0)));      // 0..90, |dot| folds the sense
}

double pointSegDist(const glm::dvec3& p, const glm::dvec3& a, const glm::dvec3& b) {
    const glm::dvec3 ab = b - a;
    const double l2 = glm::dot(ab, ab);
    if (l2 < 1e-18) return glm::length(p - a);
    const double t = std::clamp(glm::dot(p - a, ab) / l2, 0.0, 1.0);
    return glm::length(p - (a + t * ab));
}

double pointTriDist(const glm::dvec3& p, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    // region-classified closest point on a triangle (Ericson, Real-Time Collision Detection 5.1.5)
    const glm::dvec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return glm::length(p - a);
    const glm::dvec3 bp = p - b;
    const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return glm::length(p - b);
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return glm::length(p - (a + (d1 / (d1 - d3)) * ab));
    const glm::dvec3 cp = p - c;
    const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return glm::length(p - c);
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return glm::length(p - (a + (d2 / (d2 - d6)) * ac));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return glm::length(p - (b + w * (c - b)));
    }
    const double denom = 1.0 / (va + vb + vc);
    return glm::length(p - (a + ab * (vb * denom) + ac * (vc * denom)));
}

// Min distance between two SEGMENTS (closest points, clamped -- Ericson 5.1.9).
double segSegDist(const glm::dvec3& p1, const glm::dvec3& q1, const glm::dvec3& p2, const glm::dvec3& q2) {
    const glm::dvec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const double a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    double s = 0.0, t = 0.0;
    if (a <= 1e-18 && e <= 1e-18) return glm::length(p1 - p2);
    if (a <= 1e-18) { t = std::clamp(f / e, 0.0, 1.0); }
    else {
        const double c = glm::dot(d1, r);
        if (e <= 1e-18) { s = std::clamp(-c / a, 0.0, 1.0); }
        else {
            const double b = glm::dot(d1, d2), denom = a * e - b * b;
            if (denom > 1e-18) s = std::clamp((b * f - c * e) / denom, 0.0, 1.0);
            t = (b * s + f) / e;
            if (t < 0.0)      { t = 0.0; s = std::clamp(-c / a, 0.0, 1.0); }
            else if (t > 1.0) { t = 1.0; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    return glm::length((p1 + d1 * s) - (p2 + d2 * t));
}

// World triangles + world vertices of one plane face (via triFace), appended to out*.
void gatherFaceTris(entt::registry& reg, const krs::sel::Selection& s,
                    std::vector<glm::dvec3>& outVerts, std::vector<std::array<glm::dvec3, 3>>& outTris) {
    if (!reg.valid(s.entity) || s.faceId < 0) return;
    const auto* mesh = reg.try_get<RenderableMeshComponent>(s.entity);
    if (!mesh) return;
    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(s.entity)) M = xf->getTransform();
    const std::size_t nTri = std::min(mesh->indices.size() / 3, mesh->triFace.size());
    for (std::size_t t = 0; t < nTri; ++t) {
        if (mesh->triFace[t] != s.faceId) continue;
        std::array<glm::dvec3, 3> tri;
        bool bad = false;
        for (int k = 0; k < 3; ++k) {
            const unsigned idx = mesh->indices[3 * t + std::size_t(k)];
            if (idx >= mesh->vertices.size()) { bad = true; break; }
            tri[std::size_t(k)] = glm::dvec3(glm::vec3(M * glm::vec4(mesh->vertices[idx].position, 1.0f)));
        }
        if (bad) continue;
        outTris.push_back(tri);
        outVerts.push_back(tri[0]); outVerts.push_back(tri[1]); outVerts.push_back(tri[2]);
    }
}

double minPointsToTris(const std::vector<glm::dvec3>& pts,
                       const std::vector<std::array<glm::dvec3, 3>>& tris) {
    double best = 1e300;
    for (const auto& p : pts)
        for (const auto& t : tris)
            best = std::min(best, pointTriDist(p, t[0], t[1], t[2]));
    return best;
}

// One measure ITEM (a groupId's features) classified + reduced to the math primitives.
struct MItem {
    enum class Kind { PlaneGroup, Line, Sphere, Vertex, Other } kind = Kind::Other;
    std::vector<krs::sel::Selection> feats;
    // line (cylinder/cone axis)
    glm::dvec3 p0{ 0 }, p1{ 0 };  bool finite = false;   // rim-to-rim segment when trimmed
    glm::dvec3 dir{ 0, 0, 1 };
    double diameter = 0.0, length = 0.0;
    // plane group
    glm::dvec3 normal{ 0, 0, 1 }, centroid{ 0 };
    double area = 0.0;
    bool coplanar = true;
    std::vector<glm::dvec3> verts;                       // world tessellation vertices (min-dist samples)
    std::vector<std::array<glm::dvec3, 3>> tris;         // world triangles (min-dist targets)
    // sphere
    glm::dvec3 center{ 0 };
    double radius = 0.0;
    // vertex
    glm::dvec3 pt{ 0 };
};

// Surface sample points of a line item's cylinder (rim rings + axial stations) for
// sampled surface-to-face min distances.
std::vector<glm::dvec3> cylinderSurfaceSamples(const MItem& it, int around = 16, int axial = 3) {
    std::vector<glm::dvec3> pts;
    const glm::dvec3 d = it.dir;
    glm::dvec3 u = glm::cross(d, glm::dvec3(0, 0, 1));
    if (glm::dot(u, u) < 1e-12) u = glm::cross(d, glm::dvec3(0, 1, 0));
    u = glm::normalize(u);
    const glm::dvec3 v = glm::normalize(glm::cross(d, u));
    const double r = 0.5 * it.diameter;
    for (int a = 0; a < axial; ++a) {
        const double f = (axial == 1) ? 0.5 : double(a) / double(axial - 1);
        const glm::dvec3 c = it.p0 + f * (it.p1 - it.p0);
        for (int i = 0; i < around; ++i) {
            const double ang = 6.283185307179586 * double(i) / double(around);
            pts.push_back(c + r * (std::cos(ang) * u + std::sin(ang) * v));
        }
    }
    pts.push_back(it.p0); pts.push_back(it.p1);
    return pts;
}

MItem buildItem(entt::registry& reg, std::vector<krs::sel::Selection> feats, std::vector<std::string>& notes) {
    MItem it;
    it.feats = std::move(feats);
    if (it.feats.empty()) return it;
    const krs::sel::Selection& f0 = it.feats.front();

    if (f0.type == krs::sel::FeatureType::Vertex) {
        it.kind = MItem::Kind::Vertex;
        it.pt = glm::dvec3(f0.hitPoint);
        return it;
    }
    if (f0.type == krs::sel::FeatureType::Sphere) {
        it.kind = MItem::Kind::Sphere;
        it.center = glm::dvec3(f0.axisPos);
        const Measurement m = measureFeature(reg, f0.entity, f0.faceId);
        it.radius = m.ok ? 0.5 * m.diameterM : double(f0.radius);
        it.diameter = 2.0 * it.radius;
        return it;
    }
    if (f0.type == krs::sel::FeatureType::Cylinder || f0.type == krs::sel::FeatureType::Cone) {
        it.kind = MItem::Kind::Line;
        it.dir = glm::normalize(glm::dvec3(f0.axisDir));
        const Measurement m = measureFeature(reg, f0.entity, f0.faceId);
        it.diameter = m.ok ? m.diameterM : 2.0 * double(f0.radius);
        it.length = m.ok ? m.lengthM : 0.0;
        it.finite = glm::distance(f0.axisEnd0, f0.axisEnd1) > 1e-6f;
        if (it.finite) { it.p0 = glm::dvec3(f0.axisEnd0); it.p1 = glm::dvec3(f0.axisEnd1); }
        else {
            // untrimmed/synthetic bore: no rim extents -> measure against the AXIS LINE (1 m stub
            // for the segment math; pair readouts note the infinite-line fallback).
            it.p0 = glm::dvec3(f0.axisPos) - 0.5 * it.dir;
            it.p1 = glm::dvec3(f0.axisPos) + 0.5 * it.dir;
        }
        if (f0.type == krs::sel::FeatureType::Cone)
            notes.push_back("cone measured by its reference (base) diameter + axis");
        return it;
    }

    // PLANE GROUP (also the honest fallback for Other faces: area + samples only).
    it.kind = (f0.type == krs::sel::FeatureType::Plane) ? MItem::Kind::PlaneGroup : MItem::Kind::Other;
    it.normal = glm::normalize(glm::dvec3(f0.normal));
    glm::dvec3 cAcc(0.0);
    double aAcc = 0.0;
    for (const auto& f : it.feats) {
        const Measurement m = measureFeature(reg, f.entity, f.faceId);
        if (m.ok && m.areaM2 > 0.0) {
            aAcc += m.areaM2;
            cAcc += m.areaM2 * glm::dvec3(m.a);          // per-face area-weighted centroid
        }
        gatherFaceTris(reg, f, it.verts, it.tris);
        if (f.type == krs::sel::FeatureType::Plane) {
            if (angleDegBetween(glm::dvec3(f.normal), it.normal) > 0.5) it.coplanar = false;
            else if (std::abs(glm::dot(it.normal, glm::dvec3(f.axisPos) - glm::dvec3(f0.axisPos))) > 1e-3)
                it.coplanar = false;                     // parallel but offset -> not one plane
        }
    }
    it.area = aAcc;
    it.centroid = (aAcc > 0.0) ? cAcc / aAcc : glm::dvec3(f0.hitPoint);
    if (it.feats.size() > 1 && !it.coplanar)
        notes.push_back("selected faces are NOT coplanar: area is the plain sum; distances use all faces");
    return it;
}

const char* itemName(const MItem& it) {
    switch (it.kind) {
        case MItem::Kind::PlaneGroup: return "face";
        case MItem::Kind::Line:       return "bore/axis";
        case MItem::Kind::Sphere:     return "sphere";
        case MItem::Kind::Vertex:     return "vertex";
        default:                      return "feature";
    }
}

// Per-item (single) readout lines, prefixed for the two-item case.
void appendItemLines(const MItem& it, const std::string& prefix, Readout& out) {
    using U = ReadoutLine::Unit;
    switch (it.kind) {
    case MItem::Kind::Line:
        out.lines.push_back({ prefix + "Diameter", it.diameter, U::Length });
        if (it.length > 0.0) out.lines.push_back({ prefix + "Length", it.length, U::Length });
        else out.notes.push_back(prefix.empty() ? "untrimmed bore: no length extent"
                                                : prefix + "untrimmed bore: no length extent");
        break;
    case MItem::Kind::PlaneGroup:
    case MItem::Kind::Other:
        out.lines.push_back({ prefix + "Area", it.area, U::Area });
        if (it.feats.size() > 1)
            out.lines.push_back({ prefix + "Faces summed", double(it.feats.size()), U::Count });
        break;
    case MItem::Kind::Sphere:
        out.lines.push_back({ prefix + "Diameter", it.diameter, U::Length });
        break;
    case MItem::Kind::Vertex:
        out.lines.push_back({ prefix + "X", it.pt.x, U::Length });
        out.lines.push_back({ prefix + "Y", it.pt.y, U::Length });
        out.lines.push_back({ prefix + "Z", it.pt.z, U::Length });
        break;
    }
}

constexpr double kParallelDeg = 0.1;   // below this the pair reads as parallel (angle still shown truthfully)

// The PAIR readout (the Onshape split the user described).
void appendPairLines(entt::registry& /*reg*/, const MItem& A, const MItem& B, Readout& out) {
    using U = ReadoutLine::Unit;
    using K = MItem::Kind;
    const MItem* a = &A; const MItem* b = &B;
    auto ordered = [&](K k1, K k2) {                     // canonical order so one branch handles both
        if (A.kind == k1 && B.kind == k2) { a = &A; b = &B; return true; }
        if (A.kind == k2 && B.kind == k1) { a = &B; b = &A; return true; }
        return false;
    };

    if (ordered(K::Line, K::Line)) {
        const double ang = angleDegBetween(a->dir, b->dir);
        out.lines.push_back({ "Angle", ang, U::Angle });
        if (ang < kParallelDeg) {
            const glm::dvec3 w = b->p0 - a->p0;
            out.lines.push_back({ "Parallel distance", glm::length(w - glm::dot(w, a->dir) * a->dir), U::Length });
        } else if (a->finite && b->finite) {
            out.lines.push_back({ "Min distance", segSegDist(a->p0, a->p1, b->p0, b->p1), U::Length });
        } else {
            const glm::dvec3 n = glm::cross(a->dir, b->dir);
            out.lines.push_back({ "Min distance (lines)",
                                  std::abs(glm::dot(b->p0 - a->p0, n)) / glm::length(n), U::Length });
            out.notes.push_back("an untrimmed bore has no extent: infinite-line distance");
        }
        return;
    }
    if (ordered(K::PlaneGroup, K::PlaneGroup)) {
        const double ang = angleDegBetween(a->normal, b->normal);
        out.lines.push_back({ "Angle", ang, U::Angle });
        if (ang < kParallelDeg)
            out.lines.push_back({ "Plane gap (along normal)",
                                  std::abs(glm::dot(a->normal, b->centroid - a->centroid)), U::Length });
        const double dAB = minPointsToTris(a->verts, b->tris);
        const double dBA = minPointsToTris(b->verts, a->tris);
        out.lines.push_back({ "Min distance (faces)", std::min(dAB, dBA), U::Length });
        return;
    }
    if (ordered(K::PlaneGroup, K::Line)) {
        out.lines.push_back({ "Angle (line to plane)",
                              90.0 - angleDegBetween(b->dir, a->normal), U::Angle });
        const std::vector<glm::dvec3> surf = cylinderSurfaceSamples(*b);
        double best = minPointsToTris(surf, a->tris);
        // reverse direction: face vertices against the bore WALL, but only where they project
        // inside the axial extent (off the ends the exact wall distance is rim-ish -- the sampled
        // rim rings above already cover that; subtracting r there would UNDER-report).
        for (const auto& p : a->verts) {
            const glm::dvec3 ab = b->p1 - b->p0;
            const double l2 = glm::dot(ab, ab);
            if (l2 < 1e-18) continue;
            const double t = glm::dot(p - b->p0, ab) / l2;
            if (t < 0.0 || t > 1.0) continue;
            const double perp = glm::length(p - (b->p0 + t * ab));
            best = std::min(best, std::abs(perp - 0.5 * b->diameter));
        }
        out.lines.push_back({ "Min distance", std::max(0.0, best), U::Length });
        out.notes.push_back("cylinder-to-face min distance is sampled on the bore surface");
        return;
    }
    if (ordered(K::PlaneGroup, K::Sphere)) {
        const double dc = minPointsToTris({ b->center }, a->tris);
        out.lines.push_back({ "Centre to face", dc, U::Length });
        out.lines.push_back({ "Min distance (surface)", std::max(0.0, dc - b->radius), U::Length });
        return;
    }
    if (ordered(K::PlaneGroup, K::Vertex)) {
        out.lines.push_back({ "Min distance", minPointsToTris({ b->pt }, a->tris), U::Length });
        return;
    }
    if (ordered(K::Line, K::Sphere)) {
        const double dAxis = pointSegDist(b->center, a->p0, a->p1);
        out.lines.push_back({ "Centre to axis", dAxis, U::Length });
        out.lines.push_back({ "Min distance (surfaces)",
                              std::max(0.0, dAxis - b->radius - 0.5 * a->diameter), U::Length });
        return;
    }
    if (ordered(K::Line, K::Vertex)) {
        out.lines.push_back({ "Distance to axis", pointSegDist(b->pt, a->p0, a->p1), U::Length });
        return;
    }
    if (ordered(K::Sphere, K::Sphere)) {
        const double dc = glm::length(b->center - a->center);
        out.lines.push_back({ "Centre distance", dc, U::Length });
        out.lines.push_back({ "Min distance (surfaces)", std::max(0.0, dc - a->radius - b->radius), U::Length });
        return;
    }
    if (ordered(K::Sphere, K::Vertex)) {
        const double dc = glm::length(b->pt - a->center);
        out.lines.push_back({ "Centre distance", dc, U::Length });
        out.lines.push_back({ "Min distance (surface)", std::max(0.0, dc - a->radius), U::Length });
        return;
    }
    if (ordered(K::Vertex, K::Vertex)) {
        out.lines.push_back({ "Distance", glm::length(b->pt - a->pt), U::Length });
        return;
    }
    // Other-vs-anything fallback: centroid/sample distance, honestly labelled.
    auto anchor = [](const MItem& m) {
        switch (m.kind) {
            case K::Vertex: return m.pt;
            case K::Sphere: return m.center;
            case K::Line:   return 0.5 * (m.p0 + m.p1);
            default:        return m.centroid;
        }
    };
    out.lines.push_back({ "Anchor distance", glm::length(anchor(A) - anchor(B)), U::Length });
    out.notes.push_back("pair kind not fully supported: anchor-to-anchor distance shown");
}

} // namespace

Readout measureSelections(entt::registry& reg, const std::vector<krs::sel::Selection>& sel) {
    Readout out;
    // group by groupId, order of FIRST APPEARANCE (buffer order == click order)
    std::vector<std::vector<krs::sel::Selection>> groups;
    std::vector<int> ids;
    for (const auto& s : sel) {
        if (!s.valid) continue;
        std::size_t gi = 0;
        for (; gi < ids.size(); ++gi) if (ids[gi] == s.groupId) break;
        if (gi == ids.size()) { ids.push_back(s.groupId); groups.emplace_back(); }
        groups[gi].push_back(s);
    }
    if (groups.empty()) return out;

    std::vector<MItem> items;
    items.reserve(groups.size());
    for (auto& g : groups) items.push_back(buildItem(reg, std::move(g), out.notes));

    if (items.size() >= 2) {
        appendPairLines(reg, items[items.size() - 2], items[items.size() - 1], out);
        appendItemLines(items[items.size() - 2], "A · ", out);
        appendItemLines(items[items.size() - 1], "B · ", out);
    } else {
        appendItemLines(items[0], "", out);
    }
    out.ok = !out.lines.empty();
    return out;
}

// ===========================================================================
// MEASURE gate (env KRS_MEASURE_SELFTEST) -- headless, pure CPU, synthetic
// entities in a Scene registry, real NEG-CTRLs. Every check prints a measured
// number; the summary line is ALL PASS / FAILURES PRESENT.
// ===========================================================================
namespace {

struct MSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[measure]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

} // namespace

bool runMeasureGate() {
    std::printf("[measure] ================ MEASURE GATE (krs::measure) ================\n");
    MSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();

    // ---- (1) unit QUAD scaled (2,3,1): world area EXACTLY 6 m^2, centroid at the
    //          transformed centre -- the world-space proof. ------------------------
    auto eQuad = reg.create();
    reg.emplace<TransformComponent>(eQuad, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(2.0f, 3.0f, 1.0f));
    {
        BRepFace f; f.type = 0; f.normal = { 0, 0, 1 }; f.axisPos = { 0.5f, 0.5f, 0.0f };
        reg.emplace<BRepFaceComponent>(eQuad).faces.push_back(f);
        auto& mesh = reg.emplace<RenderableMeshComponent>(eQuad);
        const glm::vec3 P[4] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };
        for (const auto& p : P) { Vertex vx; vx.position = p; vx.normal = { 0,0,1 }; mesh.vertices.push_back(vx); }
        mesh.indices = { 0, 1, 2, 0, 2, 3 };
        mesh.triFace = { 0, 0 };
    }
    const Measurement q = measureFeature(reg, eQuad, 0);
    std::printf("[measure] quad(scale 2,3,1): ok=%d kind=%s area=%.9f m^2 (want 6.000000000)"
                " centroid=(%.6f, %.6f, %.6f) (want 1, 1.5, 0)\n",
                int(q.ok), q.kind.c_str(), q.areaM2, double(q.a.x), double(q.a.y), double(q.a.z));
    S.check("QUAD-AREA-WORLD   scaled(2,3,1) unit quad -> area == 6.000 m^2 exact",
            q.ok && q.kind == "plane" && std::abs(q.areaM2 - 6.0) < 1e-6);
    S.check("QUAD-CENTROID     area-weighted centroid at the transformed centre (1, 1.5, 0)",
            q.ok && glm::length(q.a - glm::vec3(1.0f, 1.5f, 0.0f)) < 1e-5f);

    // ---- (2) CYLINDER r=0.012, rims (0,0,0)..(0,0,0.06): D/L exact under identity,
    //          then under uniform scale 2. Its mesh's only triangle belongs to face 1,
    //          so face 0 (the cylinder) has ZERO triangles -> the no-fabricated-area
    //          NEG-CTRL rides on the same entity. -----------------------------------
    auto eCyl = reg.create();
    reg.emplace<TransformComponent>(eCyl, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    {
        auto& bc = reg.emplace<BRepFaceComponent>(eCyl);
        BRepFace cyl; cyl.type = 1; cyl.radius = 0.012f;
        cyl.axisPos = { 0, 0, 0 }; cyl.axisDir = { 0, 0, 1 };
        cyl.axisEnd0 = { 0, 0, 0 }; cyl.axisEnd1 = { 0, 0, 0.06f };
        bc.faces.push_back(cyl);
        BRepFace dummy; dummy.type = 0; bc.faces.push_back(dummy);   // face 1 owns the only triangle
        auto& mesh = reg.emplace<RenderableMeshComponent>(eCyl);
        Vertex v0, v1, v2; v0.position = { 0,0,0 }; v1.position = { 1,0,0 }; v2.position = { 0,1,0 };
        mesh.vertices = { v0, v1, v2 };
        mesh.indices = { 0, 1, 2 };
        mesh.triFace = { 1 };                                        // NEVER references face 0
    }
    const Measurement c1 = measureFeature(reg, eCyl, 0);
    std::printf("[measure] cyl identity: ok=%d kind=%s D=%.9f m (want 0.024) L=%.9f m (want 0.060) area=%.9f (want 0)\n",
                int(c1.ok), c1.kind.c_str(), c1.diameterM, c1.lengthM, c1.areaM2);
    S.check("CYL-DIAMETER      identity transform -> D == 0.0240 m",
            c1.ok && c1.kind == "cylinder" && std::abs(c1.diameterM - 0.024) < 1e-7);
    S.check("CYL-LENGTH        rim-to-rim (axisEnd0..axisEnd1) -> L == 0.0600 m",
            c1.ok && std::abs(c1.lengthM - 0.060) < 1e-7);
    S.check("NEG-CTRL          zero-triangle face -> area == 0 (never fabricated), ok stays true",
            c1.ok && c1.areaM2 == 0.0);

    reg.get<TransformComponent>(eCyl).scale = glm::vec3(2.0f);       // uniform world scale 2
    const Measurement c2 = measureFeature(reg, eCyl, 0);
    std::printf("[measure] cyl scale 2: D=%.9f m (want 0.048) L=%.9f m (want 0.120)\n",
                c2.diameterM, c2.lengthM);
    S.check("CYL-WORLD-SCALE   uniform scale 2 -> D == 0.0480 m and L == 0.1200 m",
            c2.ok && std::abs(c2.diameterM - 0.048) < 1e-7 && std::abs(c2.lengthM - 0.120) < 1e-7);

    // ---- (3) tessellated DISK (64-triangle fan, r=0.5): area within 1% of pi/4,
    //          and strictly UNDER it (inscribed polygon) -- honest, not exact. -----
    auto eDisk = reg.create();
    reg.emplace<TransformComponent>(eDisk, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    {
        BRepFace f; f.type = 0; f.normal = { 0, 0, 1 };
        reg.emplace<BRepFaceComponent>(eDisk).faces.push_back(f);
        auto& mesh = reg.emplace<RenderableMeshComponent>(eDisk);
        const int N = 64; const float r = 0.5f;
        Vertex ctr; ctr.position = { 0, 0, 0 }; mesh.vertices.push_back(ctr);
        for (int i = 0; i < N; ++i) {
            const float ang = 6.28318530717958647692f * float(i) / float(N);
            Vertex vx; vx.position = { r * std::cos(ang), r * std::sin(ang), 0.0f };
            mesh.vertices.push_back(vx);
        }
        for (int i = 0; i < N; ++i) {
            mesh.indices.push_back(0);
            mesh.indices.push_back(unsigned(1 + i));
            mesh.indices.push_back(unsigned(1 + (i + 1) % N));
            mesh.triFace.push_back(0);
        }
    }
    const Measurement dk = measureFeature(reg, eDisk, 0);
    const double diskWant = 3.14159265358979323846 * 0.25;
    const double diskRel = std::abs(dk.areaM2 - diskWant) / diskWant;
    std::printf("[measure] disk 64-tri fan r=0.5: area=%.9f m^2 (pi/4=%.9f) relErr=%.3e (tolerance 1e-2)\n",
                dk.areaM2, diskWant, diskRel);
    S.check("DISK-AREA-1PCT    64-triangle fan r=0.5 -> area within 1% of pi/4",
            dk.ok && diskRel < 0.01);
    S.check("DISK-AREA-HONEST  tessellated area UNDER-estimates pi/4 (inscribed, not fabricated-exact)",
            dk.ok && dk.areaM2 < diskWant && diskRel > 1e-6);

    // ---- (4) DISTANCE between two spheres (r=0.1) placed via TransformComponent at
    //          (0,0,0) and (3,4,0): exactly 5.000 m (3-4-5). ----------------------
    auto eSA = reg.create();
    reg.emplace<TransformComponent>(eSA, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    { BRepFace f; f.type = 3; f.radius = 0.1f; f.axisPos = { 0,0,0 }; reg.emplace<BRepFaceComponent>(eSA).faces.push_back(f); }
    auto eSB = reg.create();
    reg.emplace<TransformComponent>(eSB, glm::vec3(3.0f, 4.0f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    { BRepFace f; f.type = 3; f.radius = 0.1f; f.axisPos = { 0,0,0 }; reg.emplace<BRepFaceComponent>(eSB).faces.push_back(f); }
    const Measurement sd = measureDistance(reg, eSA, 0, eSB, 0);
    std::printf("[measure] sphere-sphere: ok=%d kind=%s dist=%.9f m (want 5.000000000) a=(%.3f,%.3f,%.3f) b=(%.3f,%.3f,%.3f)\n",
                int(sd.ok), sd.kind.c_str(), sd.lengthM,
                double(sd.a.x), double(sd.a.y), double(sd.a.z),
                double(sd.b.x), double(sd.b.y), double(sd.b.z));
    S.check("SPHERE-DISTANCE   centres (0,0,0)->(3,4,0) via TransformComponent -> 5.000 m exact",
            sd.ok && sd.kind == "distance" && std::abs(sd.lengthM - 5.0) < 1e-9);
    const Measurement sm = measureFeature(reg, eSA, 0);
    std::printf("[measure] sphere feature: kind=%s D=%.9f m (want 0.2)\n", sm.kind.c_str(), sm.diameterM);
    S.check("SPHERE-DIAMETER   r=0.1 -> D == 0.200 m",
            sm.ok && sm.kind == "sphere" && std::abs(sm.diameterM - 0.2) < 1e-7);

    // ---- (5) text(): the kind + ONLY the applicable fields. ---------------------
    const std::string tq = q.text(), tc = c1.text(), td = sd.text();
    std::printf("[measure] text(): plane='%s' | cylinder='%s' | distance='%s'\n",
                tq.c_str(), tc.c_str(), td.c_str());
    const auto npos = std::string::npos;
    S.check("TEXT-PLANE        contains 'plane' + A=, and NO D=/L=",
            tq.find("plane") != npos && tq.find("A=") != npos
            && tq.find("D=") == npos && tq.find("L=") == npos);
    S.check("TEXT-CYLINDER     contains 'cylinder' + D= + L=, and NO A= (zero-triangle face)",
            tc.find("cylinder") != npos && tc.find("D=") != npos
            && tc.find("L=") != npos && tc.find("A=") == npos);
    S.check("TEXT-DISTANCE     contains 'distance' + L=",
            td.find("distance") != npos && td.find("L=") != npos);

    // ---- NEG-CTRLs: bad inputs return ok=false and never crash. -----------------
    const Measurement n1 = measureFeature(reg, eQuad, 5);
    S.check("NEG-CTRL          faceId out of range (5 of 1) -> ok=false, no crash", !n1.ok);
    const Measurement n2 = measureFeature(reg, eQuad, -1);
    S.check("NEG-CTRL          negative faceId -> ok=false", !n2.ok);
    auto eBare = reg.create();
    reg.emplace<TransformComponent>(eBare, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    const Measurement n3 = measureFeature(reg, eBare, 0);
    S.check("NEG-CTRL          entity WITHOUT BRepFaceComponent -> ok=false", !n3.ok);
    const Measurement n4 = measureDistance(reg, eSA, 0, eBare, 0);
    S.check("NEG-CTRL          distance with one unresolvable feature -> ok=false", !n4.ok);

    // ---- (7) MEASURE-MODE pair math (measureSelections over synthetic Selections). ----
    auto mkLine = [](glm::vec3 p0, glm::vec3 p1, float r, int gid) {
        krs::sel::Selection s; s.valid = true; s.type = krs::sel::FeatureType::Cylinder;
        s.groupId = gid; s.axisPos = p0; s.axisDir = glm::normalize(p1 - p0);
        s.axisEnd0 = p0; s.axisEnd1 = p1; s.radius = r; s.hitPoint = p0;
        return s;   // entity=null: diameter falls back to 2r, length to the rims -- pure math
    };
    auto mkVertex = [](glm::vec3 p, int gid) {
        krs::sel::Selection s; s.valid = true; s.type = krs::sel::FeatureType::Vertex;
        s.groupId = gid; s.hitPoint = p; s.faceId = -1;
        return s;
    };
    auto findLine = [](const Readout& r, const char* label) -> const ReadoutLine* {
        for (const auto& l : r.lines) if (l.label == label) return &l;
        return nullptr;
    };

    // (7a) SKEW segments: A along X at origin, B along Y at z=0.5 -> angle 90, min dist 0.5.
    {
        const Readout r = measureSelections(reg, {
            mkLine({ 0, 0, 0 }, { 1, 0, 0 }, 0.01f, 1),
            mkLine({ 0, -0.5f, 0.5f }, { 0, 0.5f, 0.5f }, 0.01f, 2) });
        const auto* ang = findLine(r, "Angle");
        const auto* d = findLine(r, "Min distance");
        std::printf("[measure] skew lines: angle=%.6f deg (want 90) minDist=%.9f m (want 0.5)\n",
                    ang ? ang->value : -1.0, d ? d->value : -1.0);
        S.check("PAIR-SKEW-LINES   perpendicular skew segments -> angle 90.00, min distance 0.500",
                r.ok && ang && std::abs(ang->value - 90.0) < 1e-6 && d && std::abs(d->value - 0.5) < 1e-9);
    }
    // (7b) PARALLEL lines: both along X, 0.25 m apart -> angle reads a truthful 0,
    //      and the readout is the PARALLEL-AXIS distance (same math, honest display name).
    {
        const Readout r = measureSelections(reg, {
            mkLine({ 0, 0, 0 }, { 1, 0, 0 }, 0.01f, 1),
            mkLine({ 0.3f, 0.25f, 0 }, { 1.3f, 0.25f, 0 }, 0.01f, 2) });
        const auto* ang = findLine(r, "Angle");
        const auto* d = findLine(r, "Parallel distance");
        std::printf("[measure] parallel lines: angle=%.9f deg (want 0) parallelDist=%.9f m (want 0.25)\n",
                    ang ? ang->value : -1.0, d ? d->value : -1.0);
        S.check("PAIR-PARALLEL     parallel lines -> angle 0.000 + parallel-axis distance 0.250",
                r.ok && ang && ang->value < 1e-6 && d && std::abs(d->value - 0.25) < 1e-9);
    }
    // (7c) PLANE GROUP area sum: the (2,3,1)-scaled quad twice (two faces, one group) -> 12 m^2,
    //      via a second face on the same entity sharing the same triangles' plane.
    {
        krs::sel::Selection f1; f1.valid = true; f1.type = krs::sel::FeatureType::Plane;
        f1.entity = eQuad; f1.faceId = 0; f1.groupId = 7;
        f1.normal = { 0, 0, 1 }; f1.axisPos = { 1, 1.5f, 0 }; f1.hitPoint = f1.axisPos;
        krs::sel::Selection f2 = f1;                       // same face selected twice = same plane,
        const Readout r = measureSelections(reg, { f1, f2 });   // areas SUM over the group's faces
        const auto* area = findLine(r, "Area");
        std::printf("[measure] plane group (2 faces): area=%.9f m^2 (want 12 = 2 x 6)\n",
                    area ? area->value : -1.0);
        S.check("GROUP-AREA-SUM    shift-extended face group -> areas sum (2 x 6.0 = 12.0 m^2)",
                r.ok && area && std::abs(area->value - 12.0) < 1e-6);
    }
    // (7d) PARALLEL PLANES gap: two single-face groups on parallel planes 0.75 apart.
    //      Build a second quad entity at z=0.75 (identity scale, unit quad).
    {
        auto eQ2 = reg.create();
        reg.emplace<TransformComponent>(eQ2, glm::vec3(0.0f, 0.0f, 0.75f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        BRepFace f; f.type = 0; f.normal = { 0, 0, 1 }; f.axisPos = { 0.5f, 0.5f, 0.0f };
        reg.emplace<BRepFaceComponent>(eQ2).faces.push_back(f);
        auto& mesh = reg.emplace<RenderableMeshComponent>(eQ2);
        const glm::vec3 P[4] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };
        for (const auto& p : P) { Vertex vx; vx.position = p; vx.normal = { 0,0,1 }; mesh.vertices.push_back(vx); }
        mesh.indices = { 0, 1, 2, 0, 2, 3 };
        mesh.triFace = { 0, 0 };

        krs::sel::Selection a1; a1.valid = true; a1.type = krs::sel::FeatureType::Plane;
        a1.entity = eQuad; a1.faceId = 0; a1.groupId = 1;
        a1.normal = { 0, 0, 1 }; a1.axisPos = { 1, 1.5f, 0 }; a1.hitPoint = a1.axisPos;
        krs::sel::Selection b1; b1.valid = true; b1.type = krs::sel::FeatureType::Plane;
        b1.entity = eQ2; b1.faceId = 0; b1.groupId = 2;
        b1.normal = { 0, 0, 1 }; b1.axisPos = { 0.5f, 0.5f, 0.75f }; b1.hitPoint = b1.axisPos;
        const Readout r = measureSelections(reg, { a1, b1 });
        const auto* ang = findLine(r, "Angle");
        const auto* gap = findLine(r, "Plane gap (along normal)");
        const auto* md = findLine(r, "Min distance (faces)");
        std::printf("[measure] parallel planes: angle=%.9f (want 0) gap=%.9f (want 0.75) minFace=%.9f (want 0.75)\n",
                    ang ? ang->value : -1.0, gap ? gap->value : -1.0, md ? md->value : -1.0);
        S.check("PAIR-PLANES-GAP   parallel planes -> angle 0 + normal gap 0.750 + face min distance 0.750",
                r.ok && ang && ang->value < 1e-5 && gap && std::abs(gap->value - 0.75) < 1e-6
                     && md && std::abs(md->value - 0.75) < 1e-6);
    }
    // (7e) VERTEX-VERTEX: (0,0,0) -> (3,4,0) = 5 m exactly, and single-vertex coordinates.
    {
        const Readout r = measureSelections(reg, { mkVertex({ 0, 0, 0 }, 1), mkVertex({ 3, 4, 0 }, 2) });
        const auto* d = findLine(r, "Distance");
        std::printf("[measure] vertex-vertex: dist=%.9f m (want 5)\n", d ? d->value : -1.0);
        S.check("PAIR-VERTICES     vertex picks (0,0,0)->(3,4,0) -> distance 5.000",
                r.ok && d && std::abs(d->value - 5.0) < 1e-9);
    }
    // (7f) FIFO-2: three items committed -> only the LAST TWO measure (a 3rd line evicts the 1st).
    {
        const Readout r = measureSelections(reg, {
            mkLine({ 0, 0, 0 }, { 1, 0, 0 }, 0.01f, 1),        // stale item (would be evicted by the UI;
            mkLine({ 0, 0, 9 }, { 1, 0, 9 }, 0.01f, 2),        //  the math measures the LAST TWO regardless)
            mkLine({ 0.5f, 0.125f, 9 }, { 1.5f, 0.125f, 9 }, 0.01f, 3) });
        const auto* d = findLine(r, "Parallel distance");
        std::printf("[measure] last-two-of-three: parallelDist=%.9f m (want 0.125, groups 2+3)\n",
                    d ? d->value : -1.0);
        S.check("PAIR-LAST-TWO     three items -> the readout measures the newest two (0.125)",
                r.ok && d && std::abs(d->value - 0.125) < 1e-9);
    }

    const bool pass = (S.pass == S.total);
    std::printf("[measure] %d/%d checks\n", S.pass, S.total);
    std::printf("[measure] %s\n", pass
        ? "ALL PASS (world-space quad area exact; cylinder D/L track the transform; disk area honest to 1%;"
          " sphere distance exact; text() applicable-fields-only; out-of-range/no-B-Rep/zero-triangle neg-ctrls)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::measure
