// GraspQuery.cpp -- see GraspQuery.hpp. krs::graspq: the open grasp-solving
// contract's solver REGISTRY plus the reference parallel-jaw solver
// "builtin_antipodal" (pure CPU geometry over the target's B-Rep faces), and
// the headless GRASPQ gate.
//
// Geometry lineage (reused ideas, cited):
//   - world-space face triangles / area-weighted centroid: Measure.cpp
//     faceWorldArea + gatherFaceTris (src/Physics/Measure.cpp:33-54, 228-249);
//   - perpendicular basis + mean-perpendicular-scale world radius, and the
//     both-zero-rims "untrimmed" sentinel: Measure.cpp:22-27, 105-115;
//   - point-to-triangle distance (Ericson 5.1.5): Measure.cpp:182-204;
//   - the antipodal opposed-normal cosine test and the CoM-line stability
//     term: krs::grasp planAntipodal (src/Grasp/GraspPlanner.cpp:89,113-114
//     and 135-137) -- adapted from sampled mesh triangles to analytic B-Rep
//     face pairs.
#include "GraspQuery.hpp"

#include "components.hpp"   // TransformComponent, RenderableMeshComponent, BRepFace(Component), computeFaceKey
#include "Scene.hpp"        // gate registry host (same pattern as the Measure gate)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

namespace krs::graspq {

namespace {

// ---------------------------------------------------------------------------
// Scoring weights (the reference solver's honest knobs). Each term is in
// [0,1]; every term is written into GraspCandidate::why with its weight.
// ---------------------------------------------------------------------------
constexpr double kWPadOverlap     = 0.40;   // w1: pad footprint backed by both faces
constexpr double kWCentroidProx   = 0.25;   // w2: grasp line passes near the object centroid
constexpr double kWApertureMargin = 0.20;   // w3: aperture sits comfortably inside [min,max]
constexpr double kWKeepOut        = 0.30;   // w4: PENALTY for contacts near KeepOut faces

constexpr double kAntipodalTolDeg = 2.0;    // planar normals must be anti-parallel within this
constexpr double kRankQuantum     = 1e-6;   // scores closer than this tie-break by top-downness

// Two unit vectors spanning the plane perpendicular to (unit) d -- same basis
// construction as Measure.cpp:22-27 / SelectionService.hpp.
void perpBasis(const glm::dvec3& d, glm::dvec3& u, glm::dvec3& v) {
    u = glm::cross(d, glm::dvec3(0.0, 0.0, 1.0));
    if (glm::dot(u, u) < 1e-12) u = glm::cross(d, glm::dvec3(0.0, 1.0, 0.0));
    u = glm::normalize(u);
    v = glm::normalize(glm::cross(d, u));
}

// Region-classified closest distance from p to triangle abc (Ericson, Real-Time
// Collision Detection 5.1.5) -- same routine as Measure.cpp:182-204 (that copy
// lives in an anonymous namespace, so it is restated here, not linked).
double pointTriDist(const glm::dvec3& p, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
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

// One target face resolved to WORLD space.
struct WFace {
    int faceId = -1;
    std::uint64_t key = 0;
    int role = kRoleNone;
    int type = 4;                                   // BRepFace.type
    bool contactable = true;
    glm::dvec3 normal{0,0,1};                       // outward unit (planes; from mesh winding when triangles exist)
    glm::dvec3 centroid{0};                         // area-weighted world centroid (fallback: transformed axisPos)
    double area = 0.0;                              // world triangle area (0 = no triangles, never fabricated)
    glm::dvec3 axisPos{0}, axisDir{0,0,1};          // world (cylinders)
    double radiusW = 0.0;                           // world radius (mean perpendicular scale, Measure.cpp:105-111)
    glm::dvec3 end0{0}, end1{0};                    // world rim centres
    bool trimmed = false;                           // both-zero-rims sentinel (components.hpp / Measure.cpp:113-115)
    std::vector<std::array<glm::dvec3,3>> tris;     // world triangles (pad overlap + keepout distance)
};

// Resolve the target entity's B-Rep faces into world space. Returns false
// (empty out) when the entity/components are missing -- solver yields an
// honest empty candidate set, never a crash.
bool gatherFaces(entt::registry& reg, const GraspTarget& target,
                 std::vector<WFace>& out, glm::dvec3& objCenter, double& objRadius) {
    const entt::entity e = target.entity;
    if (!reg.valid(e) || !reg.all_of<BRepFaceComponent>(e)) return false;
    const auto& faces = reg.get<BRepFaceComponent>(e).faces;
    if (faces.empty()) return false;

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
    const glm::dmat3 L = glm::dmat3(glm::mat3(M));  // linear part (rotation*scale) for directions

    // grip-only mode: ANY GripSurface role in the map restricts contact to
    // GripSurface faces (see header). KeepOut-only maps do NOT trigger it.
    bool gripOnly = false;
    for (const auto& kv : target.faceRoles)
        if (kv.second == kRoleGripSurface) { gripOnly = true; break; }

    const auto* mesh = reg.try_get<RenderableMeshComponent>(e);

    out.clear();
    out.reserve(faces.size());
    for (int fid = 0; fid < int(faces.size()); ++fid) {
        const BRepFace& bf = faces[std::size_t(fid)];
        WFace w;
        w.faceId = fid;
        w.key    = bf.faceKey;
        w.type   = bf.type;
        if (bf.faceKey != 0) {
            const auto it = target.faceRoles.find(bf.faceKey);
            if (it != target.faceRoles.end()) w.role = it->second;
        }
        w.contactable = (w.role != kRoleKeepOut) && (w.role != kRoleInteraction)
                        && (!gripOnly || w.role == kRoleGripSurface);

        w.axisPos = glm::dvec3(glm::vec3(M * glm::vec4(bf.axisPos, 1.0f)));
        const glm::dvec3 adL = glm::dvec3(bf.axisDir);
        w.axisDir = (glm::dot(adL, adL) > 1e-12) ? glm::normalize(L * adL) : glm::dvec3(0, 0, 1);
        w.trimmed = glm::dot(glm::dvec3(bf.axisEnd0), glm::dvec3(bf.axisEnd0)) > 1e-12
                 || glm::dot(glm::dvec3(bf.axisEnd1), glm::dvec3(bf.axisEnd1)) > 1e-12;
        w.end0 = glm::dvec3(glm::vec3(M * glm::vec4(bf.axisEnd0, 1.0f)));
        w.end1 = glm::dvec3(glm::vec3(M * glm::vec4(bf.axisEnd1, 1.0f)));

        // world radius: mean of the two linear-map scale factors PERPENDICULAR
        // to the axis (Measure.cpp:105-111 -- the honest single number under
        // non-uniform scale).
        {
            glm::dvec3 u, v;
            perpBasis(w.axisDir, u, v);
            const double su = glm::length(L * u), sv = glm::length(L * v);
            w.radiusW = double(bf.radius) * 0.5 * (su + sv);
        }

        // world triangles + area-weighted centroid + WINDING outward normal
        // (Measure.cpp faceWorldArea / gatherFaceTris technique). The mesh
        // winding is the renderable ground truth for "outward" -- BRepFace
        // .normal may be flipped by the source CAD face orientation.
        glm::dvec3 nAcc(0.0), cAcc(0.0);
        double aAcc = 0.0;
        if (mesh) {
            const std::size_t nTri = std::min(mesh->indices.size() / 3, mesh->triFace.size());
            const std::size_t nVerts = mesh->vertices.size();
            for (std::size_t t = 0; t < nTri; ++t) {
                if (mesh->triFace[t] != fid) continue;
                const unsigned i0 = mesh->indices[3 * t + 0];
                const unsigned i1 = mesh->indices[3 * t + 1];
                const unsigned i2 = mesh->indices[3 * t + 2];
                if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) continue;   // malformed tri: skip, no crash
                const glm::dvec3 a(glm::vec3(M * glm::vec4(mesh->vertices[i0].position, 1.0f)));
                const glm::dvec3 b(glm::vec3(M * glm::vec4(mesh->vertices[i1].position, 1.0f)));
                const glm::dvec3 c(glm::vec3(M * glm::vec4(mesh->vertices[i2].position, 1.0f)));
                const glm::dvec3 cr = glm::cross(b - a, c - a);              // = 2*area*outwardN
                const double triA = 0.5 * glm::length(cr);
                if (triA < 1e-16) continue;
                nAcc += cr;
                aAcc += triA;
                cAcc += triA * ((a + b + c) / 3.0);
                w.tris.push_back({a, b, c});
            }
        }
        w.area = aAcc;
        w.centroid = (aAcc > 0.0) ? cAcc / aAcc : w.axisPos;
        if (glm::dot(nAcc, nAcc) > 1e-24) {
            w.normal = glm::normalize(nAcc);
        } else {
            const glm::dvec3 nL = glm::dvec3(bf.normal);
            w.normal = (glm::dot(nL, nL) > 1e-12) ? glm::normalize(L * nL) : glm::dvec3(0, 0, 1);
        }
        out.push_back(std::move(w));
    }

    // object centre + radius for the centroid-proximity normalisation: world
    // AABB of the mesh vertices (fallback: face-centroid bounds).
    glm::dvec3 lo(1e300), hi(-1e300);
    bool any = false;
    if (mesh) {
        for (const auto& vx : mesh->vertices) {
            const glm::dvec3 p(glm::vec3(M * glm::vec4(vx.position, 1.0f)));
            lo = glm::min(lo, p); hi = glm::max(hi, p); any = true;
        }
    }
    if (!any) {
        for (const auto& w : out) { lo = glm::min(lo, w.centroid); hi = glm::max(hi, w.centroid); any = true; }
    }
    objCenter = any ? 0.5 * (lo + hi) : glm::dvec3(0.0);
    objRadius = any ? std::max(0.5 * glm::length(hi - lo), 1e-6) : 1e-6;
    return true;
}

// 2D rectangle helper (coordinates in the plane perpendicular to the grip axis).
struct Rect2 {
    double lo0 = 1e300, lo1 = 1e300, hi0 = -1e300, hi1 = -1e300;
    void add(double a, double b) { lo0 = std::min(lo0, a); hi0 = std::max(hi0, a);
                                   lo1 = std::min(lo1, b); hi1 = std::max(hi1, b); }
    bool valid() const { return hi0 >= lo0 && hi1 >= lo1; }
    static Rect2 intersect(const Rect2& a, const Rect2& b) {
        Rect2 r; r.lo0 = std::max(a.lo0, b.lo0); r.hi0 = std::min(a.hi0, b.hi0);
        r.lo1 = std::max(a.lo1, b.lo1); r.hi1 = std::min(a.hi1, b.hi1); return r;
    }
    double area() const { return valid() ? (hi0 - lo0) * (hi1 - lo1) : 0.0; }
};

// TCP approach axis: perpendicular to the grip axis x, preferring world -Z
// ("top-down"); degenerate (x vertical) falls back to +X then +Y projections.
glm::dvec3 chooseApproach(const glm::dvec3& x) {
    static const glm::dvec3 prefs[3] = { {0,0,-1}, {1,0,0}, {0,1,0} };
    for (const auto& p : prefs) {
        const glm::dvec3 z = p - glm::dot(p, x) * x;
        if (glm::dot(z, z) > 1e-12) return glm::normalize(z);
    }
    return glm::dvec3(0, 0, -1);   // unreachable for unit x
}

// Deterministic sign fold for an axis (same hemisphere discipline as
// computeFaceKey, components.hpp:647-651): flip so the dominant non-zero
// component is positive.
glm::dvec3 foldAxis(const glm::dvec3& d) {
    const double pick = (std::abs(d.z) > 1e-6) ? d.z : (std::abs(d.y) > 1e-6 ? d.y : d.x);
    return (pick < 0.0) ? -d : d;
}

// Nearest distance from either contact to any KeepOut face (triangles when the
// face has them, centroid otherwise). Returns +inf when there are none.
double keepOutDistance(const std::vector<WFace>& faces,
                       const glm::dvec3& cA, const glm::dvec3& cB, bool& anyKeepOut) {
    double best = 1e300;
    anyKeepOut = false;
    for (const auto& f : faces) {
        if (f.role != kRoleKeepOut) continue;
        anyKeepOut = true;
        if (!f.tris.empty()) {
            for (const auto& t : f.tris) {
                best = std::min(best, pointTriDist(cA, t[0], t[1], t[2]));
                best = std::min(best, pointTriDist(cB, t[0], t[1], t[2]));
            }
        } else {
            best = std::min(best, std::min(glm::length(cA - f.centroid), glm::length(cB - f.centroid)));
        }
    }
    return best;
}

struct Ranked { GraspCandidate cand; double topdown = 0.0; };

// Shared scoring tail: centroid-proximity + aperture-margin + keepout terms,
// the four-term score, and the honest `why` strings. `pad` and its note come
// from the pair/cylinder specific code.
void finishCandidate(Ranked& r, const GraspQuery& q, const std::vector<WFace>& faces,
                     const glm::dvec3& objCenter, double objRadius,
                     double pad, const QString& padNote, const QString& geomNote) {
    GraspCandidate& c = r.cand;
    const JawSpec& jaw = q.jaw;

    // centroid-proximity: perpendicular distance of the object centre from the
    // grip line (through tcpPos along tcpX), normalised by the object radius --
    // the krs::grasp comPerp stability idea (GraspPlanner.cpp:135-137).
    const glm::dvec3 x(c.tcpX);
    const glm::dvec3 w = objCenter - glm::dvec3(c.tcpPos);
    const double dCentroid = glm::length(w - glm::dot(w, x) * x);
    const double centroidProx = 1.0 - std::min(1.0, dCentroid / objRadius);

    // aperture-margin: how deep inside [min,max] the required gap sits
    // (1 = centred in the range, 0 = at a limit).
    const double range = jaw.apertureMaxM - jaw.apertureMinM;
    double apMargin = 0.0;
    if (range > 1e-12) {
        apMargin = 2.0 * std::min(c.apertureM - jaw.apertureMinM, jaw.apertureMaxM - c.apertureM) / range;
        apMargin = std::clamp(apMargin, 0.0, 1.0);
    }

    // keepout-proximity penalty: contacts near a KeepOut face are punished
    // within a safety radius of 2*max(pad dims). No KeepOut faces -> 0.
    bool anyKeepOut = false;
    const double dKeep = keepOutDistance(faces, glm::dvec3(c.contactA), glm::dvec3(c.contactB), anyKeepOut);
    const double dSafe = std::max(2.0 * std::max(jaw.jawDepthM, jaw.jawWidthM), 1e-9);
    const double keepTerm = anyKeepOut ? std::max(0.0, 1.0 - dKeep / dSafe) : 0.0;

    c.score = kWPadOverlap * pad + kWCentroidProx * centroidProx
            + kWApertureMargin * apMargin - kWKeepOut * keepTerm;
    r.topdown = glm::dot(glm::dvec3(c.tcpZ), glm::dvec3(0, 0, -1));

    c.why << geomNote;
    c.why << QString::asprintf("pad-overlap %.3f (w %.2f -> %+.4f)%s", pad, kWPadOverlap,
                               kWPadOverlap * pad, padNote.isEmpty() ? "" : qUtf8Printable(" -- " + padNote));
    c.why << QString::asprintf("centroid-proximity %.3f (w %.2f -> %+.4f; grip line %.4f m off centre, obj radius %.4f m)",
                               centroidProx, kWCentroidProx, kWCentroidProx * centroidProx, dCentroid, objRadius);
    c.why << QString::asprintf("aperture-margin %.3f (w %.2f -> %+.4f; aperture %.4f m in [%.4f, %.4f])",
                               apMargin, kWApertureMargin, kWApertureMargin * apMargin,
                               c.apertureM, jaw.apertureMinM, jaw.apertureMaxM);
    if (anyKeepOut)
        c.why << QString::asprintf("keepout-proximity %.3f (w %.2f -> %+.4f; nearest KeepOut face %.4f m, safety radius %.4f m)",
                                   keepTerm, kWKeepOut, -kWKeepOut * keepTerm, dKeep, dSafe);
    else
        c.why << QStringLiteral("keepout-proximity 0.000 (no KeepOut faces on target)");
    c.why << QString::asprintf("rank: score %.6f, top-down %.3f (scores within %.0e tie-break by top-down approach, then faceKey)",
                               c.score, r.topdown, kRankQuantum);
    const glm::vec3 al = q.jaw.approachLocal;
    if (glm::length(al - glm::vec3(0, 0, 1)) > 1e-4f)
        c.why << QStringLiteral("note: jaw.approachLocal != +Z; builtin uses TCP +Z as the approach axis (not compensated)");
}

// ---------------------------------------------------------------------------
// builtin_antipodal -- the reference solver. Planar antipodal face pairs +
// cylinder diameter grips over the target's B-Rep faces, respecting roles.
// Deterministic: no sampling; stable ranking (see header).
// ---------------------------------------------------------------------------
std::vector<GraspCandidate> builtinAntipodal(entt::registry& reg, const GraspQuery& q) {
    std::vector<GraspCandidate> out;
    if (q.maxCandidates <= 0) return out;

    std::vector<WFace> faces;
    glm::dvec3 objCenter(0.0);
    double objRadius = 1e-6;
    if (!gatherFaces(reg, q.target, faces, objCenter, objRadius)) return out;   // honest empty

    const JawSpec& jaw = q.jaw;
    const double cosOppose = -std::cos(glm::radians(kAntipodalTolDeg));   // dot(nA,nB) must be <= this
    std::vector<Ranked> ranked;

    // ---- PLANAR ANTIPODAL PAIRS -------------------------------------------
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const WFace& A = faces[i];
        if (!A.contactable || A.type != 0 || A.area <= 0.0) continue;
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
            const WFace& B = faces[j];
            if (!B.contactable || B.type != 0 || B.area <= 0.0) continue;

            // anti-parallel outward normals within tolerance (the krs::grasp
            // antipodal criterion, GraspPlanner.cpp:113-114, on analytic faces).
            if (glm::dot(A.normal, B.normal) > cosOppose) continue;
            // material BETWEEN the faces: each centroid behind the other's plane.
            if (glm::dot(A.normal, B.centroid - A.centroid) > -1e-9) continue;
            if (glm::dot(B.normal, A.centroid - B.centroid) > -1e-9) continue;

            // plane gap along the (near-common) normal axis.
            const double gapA = glm::dot(A.normal, A.centroid - B.centroid);
            const double gapB = glm::dot(B.normal, B.centroid - A.centroid);
            const double gap = 0.5 * (gapA + gapB);
            if (gap < jaw.apertureMinM || gap > jaw.apertureMaxM) continue;

            // grip axis (jaw-open) and TCP frame.
            glm::dvec3 x = glm::normalize(A.normal - B.normal);
            x = foldAxis(x);
            const glm::dvec3 z = chooseApproach(x);
            const glm::dvec3 y = glm::cross(z, x);                     // X x Y = Z (right-handed TCP)

            // overlap of the two faces projected onto the (z,y) plane, then
            // the pad footprint clamped into that overlap.
            const glm::dvec3 O = 0.5 * (A.centroid + B.centroid);
            auto rectOf = [&](const WFace& f) {
                Rect2 r;
                for (const auto& t : f.tris)
                    for (const auto& p : t)
                        r.add(glm::dot(p - O, z), glm::dot(p - O, y));
                return r;
            };
            const Rect2 overlap = Rect2::intersect(rectOf(A), rectOf(B));
            if (!overlap.valid()) continue;                            // jaws would not oppose through material
            const double c0 = 0.5 * (overlap.lo0 + overlap.hi0);
            const double c1 = 0.5 * (overlap.lo1 + overlap.hi1);
            Rect2 padRect;
            padRect.add(c0 - 0.5 * jaw.jawDepthM, c1 - 0.5 * jaw.jawWidthM);
            padRect.add(c0 + 0.5 * jaw.jawDepthM, c1 + 0.5 * jaw.jawWidthM);
            const double padArea = jaw.jawDepthM * jaw.jawWidthM;
            const double pad = (padArea > 1e-12)
                ? std::clamp(Rect2::intersect(overlap, padRect).area() / padArea, 0.0, 1.0) : 0.0;

            // contact points: the overlap centre pushed along the grip axis
            // onto each face's plane (centroid-overlap region, pad-clamped).
            const glm::dvec3 base = O + c0 * z + c1 * y;
            const double denA = glm::dot(x, A.normal), denB = glm::dot(x, B.normal);
            if (std::abs(denA) < 1e-9 || std::abs(denB) < 1e-9) continue;   // grip axis parallel to a face plane: degenerate
            const double tA = glm::dot(A.centroid - base, A.normal) / denA;
            const double tB = glm::dot(B.centroid - base, B.normal) / denB;
            const glm::dvec3 pA = base + tA * x;
            const glm::dvec3 pB = base + tB * x;
            const double aperture = std::abs(tA - tB);
            if (aperture < jaw.apertureMinM || aperture > jaw.apertureMaxM) continue;

            Ranked r;
            r.cand.tcpPos   = glm::vec3(0.5 * (pA + pB));
            r.cand.tcpZ     = glm::vec3(z);
            r.cand.tcpX     = glm::vec3(x);
            r.cand.apertureM = aperture;
            r.cand.faceKeyA = A.key;
            r.cand.faceKeyB = B.key;
            r.cand.contactA = glm::vec3(pA);
            r.cand.contactB = glm::vec3(pB);
            const double opposedDeg = glm::degrees(std::acos(std::clamp(-glm::dot(A.normal, B.normal), -1.0, 1.0)));
            finishCandidate(r, q, faces, objCenter, objRadius, pad,
                QString(),
                QString::asprintf("planar antipodal pair faceKeyA=%llu faceKeyB=%llu (normals %.3f deg off anti-parallel, plane gap %.4f m)",
                                  (unsigned long long)A.key, (unsigned long long)B.key, opposedDeg, gap));
            ranked.push_back(std::move(r));
        }
    }

    // ---- CYLINDER DIAMETER GRIPS ------------------------------------------
    for (const WFace& F : faces) {
        if (!F.contactable || F.type != 1 || F.radiusW <= 1e-9) continue;
        const double diameter = 2.0 * F.radiusW;
        if (diameter < jaw.apertureMinM || diameter > jaw.apertureMaxM) continue;

        const glm::dvec3 axis = F.axisDir;
        const glm::dvec3 center = F.trimmed ? 0.5 * (F.end0 + F.end1) : F.axisPos;

        // grip axis perpendicular to the cylinder axis; approach prefers
        // world -Z. A vertical cylinder gets an exact top-down approach along
        // its own axis; otherwise x = axis x (-Z) keeps -Z reachable exactly.
        glm::dvec3 x, z;
        const glm::dvec3 zPref(0, 0, -1);
        if (std::abs(glm::dot(axis, zPref)) > 0.999) {
            glm::dvec3 u, v;
            perpBasis(axis, u, v);
            x = foldAxis(u);
            z = zPref;
        } else {
            x = foldAxis(glm::normalize(glm::cross(axis, zPref)));
            z = zPref;                                              // perpendicular to x by construction
        }
        const glm::dvec3 y = glm::cross(z, x);

        const glm::dvec3 pA = center + F.radiusW * x;
        const glm::dvec3 pB = center - F.radiusW * x;

        // pad coverage: line contact along the axis; compare the trimmed
        // axial extent with the pad's extent projected onto the axis.
        double pad = 0.5;
        QString padNote;
        if (F.trimmed) {
            const double axialLen = glm::length(F.end1 - F.end0);
            const double padAxial = std::abs(glm::dot(axis, z)) * jaw.jawDepthM
                                  + std::abs(glm::dot(axis, y)) * jaw.jawWidthM;
            pad = (padAxial > 1e-9) ? std::min(1.0, axialLen / padAxial) : 0.0;
            padNote = QString::asprintf("line contact: cylinder axial extent %.4f m vs pad axial extent %.4f m", axialLen, padAxial);
        } else {
            padNote = QStringLiteral("untrimmed cylinder: unknown axial extent, coverage assumed 0.5");
        }

        Ranked r;
        r.cand.tcpPos   = glm::vec3(center);
        r.cand.tcpZ     = glm::vec3(z);
        r.cand.tcpX     = glm::vec3(x);
        r.cand.apertureM = diameter;
        r.cand.faceKeyA = F.key;
        r.cand.faceKeyB = F.key;                                    // one face, two opposing contacts
        r.cand.contactA = glm::vec3(pA);
        r.cand.contactB = glm::vec3(pB);
        finishCandidate(r, q, faces, objCenter, objRadius, pad, padNote,
            QString::asprintf("cylinder diameter grip faceKey=%llu (world diameter %.4f m, axis (%.2f, %.2f, %.2f))",
                              (unsigned long long)F.key, diameter, axis.x, axis.y, axis.z));
        ranked.push_back(std::move(r));
    }

    // ---- RANK -------------------------------------------------------------
    // score descending; near-ties (within kRankQuantum) prefer the top-down
    // approach, then ascending faceKeys. Quantised score keeps the ordering a
    // strict weak order and immunises the rank against last-ulp float noise.
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        const long long qa = std::llround(a.cand.score / kRankQuantum);
        const long long qb = std::llround(b.cand.score / kRankQuantum);
        if (qa != qb) return qa > qb;
        if (a.topdown != b.topdown) return a.topdown > b.topdown;
        if (a.cand.faceKeyA != b.cand.faceKeyA) return a.cand.faceKeyA < b.cand.faceKeyA;
        return a.cand.faceKeyB < b.cand.faceKeyB;
    });
    const std::size_t cap = std::min<std::size_t>(ranked.size(), std::size_t(q.maxCandidates));
    out.reserve(cap);
    for (std::size_t k = 0; k < cap; ++k) out.push_back(std::move(ranked[k].cand));
    return out;
}

// ---------------------------------------------------------------------------
// Solver registry -- the open seam. Function-local statics (no SIOF); a mutex
// so node/Python runtimes can register from any thread.
// ---------------------------------------------------------------------------
std::mutex& solverMutex() { static std::mutex m; return m; }
std::map<std::string, SolverFn>& solverMap() {
    static std::map<std::string, SolverFn> m{ { "builtin_antipodal", &builtinAntipodal } };
    return m;
}

} // namespace

void registerSolver(const std::string& name, SolverFn fn) {
    std::lock_guard<std::mutex> lock(solverMutex());
    solverMap()[name] = std::move(fn);              // last write wins (documented)
}

std::vector<std::string> solverNames() {
    std::lock_guard<std::mutex> lock(solverMutex());
    std::vector<std::string> names;
    names.reserve(solverMap().size());
    for (const auto& kv : solverMap()) names.push_back(kv.first);   // std::map = already sorted
    return names;
}

std::vector<GraspCandidate> solve(entt::registry& reg, const GraspQuery& q, const std::string& solver) {
    SolverFn fn;
    {
        std::lock_guard<std::mutex> lock(solverMutex());
        const auto it = solverMap().find(solver);
        if (it == solverMap().end()) return {};     // unknown solver: honest empty, no crash, no fallback
        fn = it->second;                            // copy, then call OUTSIDE the lock (solver may register)
    }
    if (!fn) return {};
    return fn(reg, q);
}

// ===========================================================================
// GRASPQ gate (env KRS_GRASPQ_SELFTEST) -- headless, pure CPU, synthetic
// B-Rep entities in a Scene registry, real NEG-CTRLs. Mirrors the Measure
// gate style (Measure.cpp:571-580): every check prints a measured number;
// the summary line is ALL PASS / FAILURES PRESENT.
// ===========================================================================
namespace {

struct GSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[graspq]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

// Mint a box entity: 6 planar B-Rep faces (outward normals, faceKeys via
// computeFaceKey), 24-vertex/12-triangle mesh with triFace, world position via
// TransformComponent (the mesh is body-local, centred at the origin).
entt::entity makeBox(entt::registry& reg, const glm::vec3& worldCenter, const glm::vec3& halfExt) {
    const auto e = reg.create();
    reg.emplace<TransformComponent>(e, worldCenter, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& bc = reg.emplace<BRepFaceComponent>(e);
    auto& mesh = reg.emplace<RenderableMeshComponent>(e);

    struct FaceDef { glm::vec3 n, u, v; };
    const FaceDef defs[6] = {
        { { 1,0,0}, {0,1,0}, {0,0,1} },   // +X   (u x v = n so the winding is outward)
        { {-1,0,0}, {0,0,1}, {0,1,0} },   // -X
        { { 0,1,0}, {0,0,1}, {1,0,0} },   // +Y
        { { 0,-1,0},{1,0,0}, {0,0,1} },   // -Y
        { { 0,0,1}, {1,0,0}, {0,1,0} },   // +Z
        { { 0,0,-1},{0,1,0}, {1,0,0} },   // -Z
    };
    const float st[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
    for (int f = 0; f < 6; ++f) {
        const FaceDef& d = defs[f];
        const float hn = std::abs(glm::dot(d.n, halfExt));
        const float hu = std::abs(glm::dot(d.u, halfExt));
        const float hv = std::abs(glm::dot(d.v, halfExt));
        const unsigned base = unsigned(mesh.vertices.size());
        for (const auto& s : st) {
            Vertex vx;
            vx.position = d.n * hn + d.u * (s[0] * hu) + d.v * (s[1] * hv);
            vx.normal = d.n;
            mesh.vertices.push_back(vx);
        }
        mesh.indices.insert(mesh.indices.end(), { base + 0, base + 1, base + 2, base + 0, base + 2, base + 3 });
        mesh.triFace.push_back(f);
        mesh.triFace.push_back(f);

        BRepFace bf;
        bf.type = 0;
        bf.normal = d.n;
        bf.axisPos = d.n * hn;               // body-local face centre
        bf.faceKey = computeFaceKey(bf);
        bc.faces.push_back(bf);
    }
    mesh.aabbMin = -halfExt; mesh.aabbMax = halfExt;
    return e;
}

// Mint a vertical (local +Z axis) trimmed cylinder wall: 1 B-Rep cylinder face
// (radius r, rims at z = -h..+h) + a 32-segment tube mesh, all triFace = 0.
entt::entity makeCylinder(entt::registry& reg, const glm::vec3& worldCenter, float r, float halfLen) {
    const auto e = reg.create();
    reg.emplace<TransformComponent>(e, worldCenter, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& bc = reg.emplace<BRepFaceComponent>(e);
    BRepFace bf;
    bf.type = 1;
    bf.radius = r;
    bf.axisPos = { 0, 0, 0 };
    bf.axisDir = { 0, 0, 1 };
    bf.axisEnd0 = { 0, 0, -halfLen };
    bf.axisEnd1 = { 0, 0,  halfLen };
    bf.faceKey = computeFaceKey(bf);
    bc.faces.push_back(bf);

    auto& mesh = reg.emplace<RenderableMeshComponent>(e);
    const int N = 32;
    for (int i = 0; i < N; ++i) {
        const float ang = 6.28318530717958647692f * float(i) / float(N);
        const glm::vec3 radial(r * std::cos(ang), r * std::sin(ang), 0.0f);
        Vertex lo; lo.position = radial + glm::vec3(0, 0, -halfLen);
        Vertex hi; hi.position = radial + glm::vec3(0, 0,  halfLen);
        mesh.vertices.push_back(lo);                 // 2i
        mesh.vertices.push_back(hi);                 // 2i+1
    }
    for (int i = 0; i < N; ++i) {
        const unsigned b0 = unsigned(2 * i), t0 = b0 + 1;
        const unsigned b1 = unsigned(2 * ((i + 1) % N)), t1 = b1 + 1;
        mesh.indices.insert(mesh.indices.end(), { b0, b1, t1, b0, t1, t0 });   // outward winding
        mesh.triFace.push_back(0);
        mesh.triFace.push_back(0);
    }
    mesh.aabbMin = { -r, -r, -halfLen }; mesh.aabbMax = { r, r, halfLen };
    return e;
}

bool sameCandidates(const std::vector<GraspCandidate>& A, const std::vector<GraspCandidate>& B) {
    if (A.size() != B.size()) return false;
    for (std::size_t i = 0; i < A.size(); ++i) {
        const GraspCandidate& a = A[i];
        const GraspCandidate& b = B[i];
        if (a.tcpPos != b.tcpPos || a.tcpZ != b.tcpZ || a.tcpX != b.tcpX) return false;
        if (a.apertureM != b.apertureM || a.score != b.score) return false;
        if (a.faceKeyA != b.faceKeyA || a.faceKeyB != b.faceKeyB) return false;
        if (a.contactA != b.contactA || a.contactB != b.contactB) return false;
        if (a.why != b.why) return false;
    }
    return true;
}

} // namespace

bool runGraspQueryGate() {
    std::printf("[graspq] ================ GRASPQ GATE (krs::graspq) ================\n");
    GSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();

    // ---- the 40 mm box (world-offset centre so the world-space math is exercised) ----
    const glm::vec3 boxCenter(0.3f, -0.2f, 0.5f);
    const auto eBox = makeBox(reg, boxCenter, glm::vec3(0.02f));
    const auto& boxFaces = reg.get<BRepFaceComponent>(eBox).faces;
    const std::uint64_t kPX = boxFaces[0].faceKey, kNX = boxFaces[1].faceKey;
    const std::uint64_t kPY = boxFaces[2].faceKey, kNY = boxFaces[3].faceKey;
    const std::uint64_t kPZ = boxFaces[4].faceKey, kNZ = boxFaces[5].faceKey;
    std::printf("[graspq] box faceKeys: +X=%llu -X=%llu +Y=%llu -Y=%llu +Z=%llu -Z=%llu\n",
                (unsigned long long)kPX, (unsigned long long)kNX, (unsigned long long)kPY,
                (unsigned long long)kNY, (unsigned long long)kPZ, (unsigned long long)kNZ);
    S.check("KEYS-MINTED       all 6 box faceKeys non-zero and distinct",
            kPX && kNX && kPY && kNY && kPZ && kNZ
            && kPX != kNX && kPY != kNY && kPZ != kNZ && kPX != kPY && kPX != kPZ && kPY != kPZ);

    GraspQuery q;                                    // default jaw: aperture 0..0.1, pad 20x15 mm
    q.target.entity = eBox;

    // ---- (1) box: candidates exist; TOP-RANKED grips a SIDE pair, top-down. ----
    const auto boxCands = solve(reg, q);
    std::printf("[graspq] box: %zu candidates\n", boxCands.size());
    S.check("BOX-CANDS         builtin solver returns candidates for the 40 mm box", !boxCands.empty());
    if (!boxCands.empty()) {
        const GraspCandidate& top = boxCands.front();
        for (const auto& line : top.why)
            std::printf("[graspq]   top.why: %s\n", qUtf8Printable(line));
        const bool sidePair =
            (top.faceKeyA == kPX && top.faceKeyB == kNX) || (top.faceKeyA == kNX && top.faceKeyB == kPX) ||
            (top.faceKeyA == kPY && top.faceKeyB == kNY) || (top.faceKeyA == kNY && top.faceKeyB == kPY);
        const bool zPair =
            (top.faceKeyA == kPZ && top.faceKeyB == kNZ) || (top.faceKeyA == kNZ && top.faceKeyB == kPZ);
        const double zDown = glm::dot(glm::dvec3(top.tcpZ), glm::dvec3(0, 0, -1));
        std::printf("[graspq] top: keys %llu/%llu aperture=%.9f m tcpZ=(%.6f, %.6f, %.6f) zDown=%.9f\n",
                    (unsigned long long)top.faceKeyA, (unsigned long long)top.faceKeyB, top.apertureM,
                    double(top.tcpZ.x), double(top.tcpZ.y), double(top.tcpZ.z), zDown);
        S.check("BOX-TOP-SIDES     top-ranked grips two OPPOSITE SIDE faces (top-down reachable)", sidePair);
        S.check("BOX-NOT-TOPBOTTOM top-ranked is NOT the +Z/-Z pair (vertical grip axis blocks -Z approach)", !zPair);
        S.check("BOX-TOPDOWN       top-ranked approach is world -Z within 1e-6", std::abs(zDown - 1.0) < 1e-6);
        S.check("BOX-APERTURE      aperture == 0.040 m within 1e-6", std::abs(top.apertureM - 0.040) < 1e-6);

        // contacts ON the two gripped faces (plane distance + inside the 40x40 mm bounds).
        auto onFace = [&](const glm::vec3& p, std::uint64_t key) {
            for (std::size_t f = 0; f < boxFaces.size(); ++f) {
                if (boxFaces[f].faceKey != key) continue;
                const glm::dvec3 n(boxFaces[f].normal);       // identity rotation: local == world direction
                const glm::dvec3 fc = glm::dvec3(boxCenter) + glm::dvec3(boxFaces[f].axisPos);
                const glm::dvec3 d = glm::dvec3(p) - fc;
                if (std::abs(glm::dot(d, n)) > 1e-6) return false;             // on the plane
                const glm::dvec3 tang = d - glm::dot(d, n) * n;
                return std::abs(tang.x) < 0.02 + 1e-6 && std::abs(tang.y) < 0.02 + 1e-6
                    && std::abs(tang.z) < 0.02 + 1e-6;                         // inside the face
            }
            return false;
        };
        S.check("BOX-CONTACT-A     contactA lies ON face faceKeyA within 1e-6", onFace(top.contactA, top.faceKeyA));
        S.check("BOX-CONTACT-B     contactB lies ON face faceKeyB within 1e-6", onFace(top.contactB, top.faceKeyB));

        const glm::dvec3 mid = 0.5 * (glm::dvec3(top.contactA) + glm::dvec3(top.contactB));
        const double dCenter = glm::length(glm::dvec3(top.tcpPos) - glm::dvec3(boxCenter));
        const double dMid = glm::length(glm::dvec3(top.tcpPos) - mid);
        std::printf("[graspq] tcp: |tcp-boxCenter|=%.3e m |tcp-contactMid|=%.3e m (want both < 1e-6)\n", dCenter, dMid);
        S.check("BOX-TCP-CENTERED  tcpPos == box centre AND contact midpoint within 1e-6", dCenter < 1e-6 && dMid < 1e-6);
        S.check("BOX-FRAME-ORTHO   |tcpX|=|tcpZ|=1 and tcpX . tcpZ == 0 (1e-6)",
                std::abs(glm::length(glm::dvec3(top.tcpX)) - 1.0) < 1e-6
                && std::abs(glm::length(glm::dvec3(top.tcpZ)) - 1.0) < 1e-6
                && std::abs(glm::dot(glm::dvec3(top.tcpX), glm::dvec3(top.tcpZ))) < 1e-6);
        S.check("BOX-WHY-TERMS     why lists all four scored terms",
                top.why.filter("pad-overlap").size() == 1 && top.why.filter("centroid-proximity").size() == 1
                && top.why.filter("aperture-margin").size() == 1 && top.why.filter("keepout-proximity").size() == 1);
    }

    // ---- (2) ROLES: KeepOut face -> its pair vanishes, others remain. ----
    {
        GraspQuery qk = q;
        qk.target.faceRoles[kPY] = kRoleKeepOut;
        const auto cands = solve(reg, qk);
        bool touchesKeepOut = false, hasYPair = false;
        for (const auto& c : cands) {
            if (c.faceKeyA == kPY || c.faceKeyB == kPY) touchesKeepOut = true;
            if ((c.faceKeyA == kPY && c.faceKeyB == kNY) || (c.faceKeyA == kNY && c.faceKeyB == kPY)) hasYPair = true;
        }
        std::printf("[graspq] keepout(+Y): %zu candidates, touchesKeepOut=%d, hasYPair=%d\n",
                    cands.size(), int(touchesKeepOut), int(hasYPair));
        S.check("ROLE-KEEPOUT      +Y marked KeepOut -> the Y pair vanishes, KeepOut face never contacted",
                !touchesKeepOut && !hasYPair);
        S.check("ROLE-KEEPOUT-REST other pairs SURVIVE a KeepOut-only role map (KeepOut is not grip-only)",
                !cands.empty());
    }

    // ---- (3) ROLES: GripSurface-only -> only the marked pair returned. ----
    {
        GraspQuery qg = q;
        qg.target.faceRoles[kPX] = kRoleGripSurface;
        qg.target.faceRoles[kNX] = kRoleGripSurface;
        const auto cands = solve(reg, qg);
        bool onlyXPair = !cands.empty();
        for (const auto& c : cands) {
            const bool isXPair = (c.faceKeyA == kPX && c.faceKeyB == kNX) || (c.faceKeyA == kNX && c.faceKeyB == kPX);
            if (!isXPair) onlyXPair = false;
        }
        std::printf("[graspq] grip-only(+X/-X): %zu candidates, onlyXPair=%d\n", cands.size(), int(onlyXPair));
        S.check("ROLE-GRIPONLY     only the GripSurface-marked X pair is returned", onlyXPair);
    }

    // ---- (4) 20 mm cylinder -> diameter grip, aperture exact, contacts on the wall. ----
    {
        const glm::vec3 cylCenter(0.1f, 0.05f, 0.2f);
        const auto eCyl = makeCylinder(reg, cylCenter, 0.01f, 0.03f);
        GraspQuery qc = q;
        qc.target = GraspTarget{};
        qc.target.entity = eCyl;
        const auto cands = solve(reg, qc);
        std::printf("[graspq] cylinder: %zu candidates\n", cands.size());
        bool ok = !cands.empty();
        if (ok) {
            const GraspCandidate& top = cands.front();
            const double zDown = glm::dot(glm::dvec3(top.tcpZ), glm::dvec3(0, 0, -1));
            const double rA = glm::length(glm::dvec2(top.contactA.x - cylCenter.x, top.contactA.y - cylCenter.y));
            const double rB = glm::length(glm::dvec2(top.contactB.x - cylCenter.x, top.contactB.y - cylCenter.y));
            std::printf("[graspq] cyl top: aperture=%.9f m (want 0.020) zDown=%.6f contact radii %.9f/%.9f (want 0.010)\n",
                        top.apertureM, zDown, rA, rB);
            S.check("CYL-APERTURE      diameter grip aperture == 0.020 m within 1e-6",
                    std::abs(top.apertureM - 0.020) < 1e-6);
            S.check("CYL-SAME-FACE     both contact keys are the one cylinder face",
                    top.faceKeyA == top.faceKeyB && top.faceKeyA != 0);
            S.check("CYL-ON-WALL       both contacts at radius 0.010 m from the axis within 1e-6",
                    std::abs(rA - 0.010) < 1e-6 && std::abs(rB - 0.010) < 1e-6);
            S.check("CYL-TOPDOWN       vertical cylinder -> approach is world -Z within 1e-6",
                    std::abs(zDown - 1.0) < 1e-6);
        } else {
            S.check("CYL-APERTURE      diameter grip aperture == 0.020 m within 1e-6", false);
            S.check("CYL-SAME-FACE     both contact keys are the one cylinder face", false);
            S.check("CYL-ON-WALL       both contacts at radius 0.010 m from the axis within 1e-6", false);
            S.check("CYL-TOPDOWN       vertical cylinder -> approach is world -Z within 1e-6", false);
        }
    }

    // ---- (5) aperture limit: 15 mm jaw cannot grip the 40 mm box -> honest ZERO. ----
    {
        GraspQuery qs = q;
        qs.jaw.apertureMaxM = 0.015;
        const auto cands = solve(reg, qs);
        std::printf("[graspq] jaw max 15 mm on the 40 mm box: %zu candidates (want 0)\n", cands.size());
        S.check("NEG-CTRL          aperture max 15 mm < 40 mm box -> ZERO candidates (honest empty)", cands.empty());
    }

    // ---- (6) registry seam: runtime registration + routing + unknown name. ----
    {
        registerSolver("test_dummy", [](entt::registry&, const GraspQuery&) {
            GraspCandidate c;
            c.score = 42.0;
            c.why << QStringLiteral("test_dummy");
            return std::vector<GraspCandidate>{ c };
        });
        const auto names = solverNames();
        std::string joined;
        for (const auto& n : names) { joined += n; joined += ' '; }
        std::printf("[graspq] solverNames: %s\n", joined.c_str());
        const bool hasBuiltin = std::find(names.begin(), names.end(), "builtin_antipodal") != names.end();
        const bool hasDummy   = std::find(names.begin(), names.end(), "test_dummy") != names.end();
        S.check("REGISTRY-NAMES    solverNames() lists builtin_antipodal AND the runtime-registered test_dummy",
                hasBuiltin && hasDummy);
        const auto viaDummy = solve(reg, q, "test_dummy");
        S.check("REGISTRY-ROUTES   solve(..., \"test_dummy\") routes to the registered solver",
                viaDummy.size() == 1 && viaDummy[0].score == 42.0
                && viaDummy[0].why.size() == 1 && viaDummy[0].why[0] == QStringLiteral("test_dummy"));
        const auto viaUnknown = solve(reg, q, "no_such_solver");
        S.check("NEG-CTRL          unknown solver name -> empty result, no crash, no silent fallback",
                viaUnknown.empty());
    }

    // ---- (7) NEG-CTRL: invalid / bare targets never crash. ----
    {
        GraspQuery qn = q;
        qn.target = GraspTarget{};                                  // entt::null entity
        const auto c1 = solve(reg, qn);
        const auto eBare = reg.create();
        reg.emplace<TransformComponent>(eBare, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        qn.target.entity = eBare;                                   // no BRepFaceComponent
        const auto c2 = solve(reg, qn);
        S.check("NEG-CTRL          null entity / no-B-Rep target -> empty, no crash", c1.empty() && c2.empty());
    }

    // ---- (8) determinism: the same seed twice -> the identical candidate list. ----
    {
        GraspQuery qd = q;
        qd.seed = 1234;
        const auto r1 = solve(reg, qd);
        const auto r2 = solve(reg, qd);
        std::printf("[graspq] determinism: run1=%zu run2=%zu candidates, identical=%d\n",
                    r1.size(), r2.size(), int(sameCandidates(r1, r2)));
        S.check("DETERMINISM       same seed twice -> bit-identical candidate list (frames, scores, why)",
                !r1.empty() && sameCandidates(r1, r2));
    }

    const bool pass = (S.pass == S.total);
    std::printf("[graspq] %d/%d checks\n", S.pass, S.total);
    std::printf("[graspq] %s\n", pass
        ? "ALL PASS (side-pair antipodal grip exact + top-down; KeepOut/GripSurface roles filter; cylinder"
          " diameter grip exact; too-small jaw honestly empty; registry routes + unknown-name safe; deterministic)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::graspq
