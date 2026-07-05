// SnapEngine.cpp -- krs::snap candidate + frame engine (mate-selector sprint P3).
// Pure CPU, no rendering, no OCCT: reads the ECS components CadImporter populated
// (BRepFaceComponent / BRepEdgeComponent / BRepVertexComponent + the tessellation)
// and emits the Onshape/Fusion inference points with full oriented WORLD frames.
// The candidate sets and frame rules are DOCUMENTED in Snap.hpp -- keep both in sync.
#include "Snap.hpp"
#include "components.hpp"   // TransformComponent, RenderableMeshComponent, BRepFace(Component)
#include "BRepEdge.hpp"
#include "BRepVertex.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

namespace krs::snap {

namespace {

constexpr float kMergeTol = 1e-5f;   // position dedupe tolerance (metres)

// World transform + normal/direction transformer for an entity (identity when absent).
struct Frames {
    glm::mat4 M{ 1.0f };
    glm::mat3 R{ 1.0f };   // inverse-transpose linear part: correct for normals under scale
};
Frames framesOf(entt::registry& reg, entt::entity e) {
    Frames f;
    if (const auto* xf = reg.try_get<TransformComponent>(e)) {
        f.M = xf->getTransform();
        f.R = glm::mat3(glm::inverseTranspose(f.M));
    }
    return f;
}
glm::vec3 xp(const Frames& f, const glm::vec3& p) { return glm::vec3(f.M * glm::vec4(p, 1.0f)); }
glm::vec3 xd(const Frames& f, const glm::vec3& d) {
    const glm::vec3 r = f.R * d;
    const float L = glm::length(r);
    return (L > 1e-12f) ? r / L : glm::vec3(0, 0, 1);
}

// +Z-out-of-material for a face-derived candidate at world position atW (FRAME RULES, Snap.hpp):
// plane -> outward face normal (CadImporter guarantees orientation-corrected OUTWARD storage);
// cylinder/cone -> ALONG the axis; sphere -> radial (position - centre), +global Z at the centre;
// other -> the stored normal channel (default +Z), honestly approximate.
glm::vec3 faceZAt(const BRepFace& bf, const Frames& f, const glm::vec3& atW) {
    switch (bf.type) {
        case 0:  return xd(f, bf.normal);
        case 1:
        case 2:  return xd(f, bf.axisDir);
        case 3: {
            const glm::vec3 r = atW - xp(f, bf.axisPos);
            const float L = glm::length(r);
            return (L > kMergeTol) ? r / L : glm::vec3(0, 0, 1);
        }
        default: return xd(f, bf.normal);
    }
}

int glyphOf(SnapKind k) {
    switch (k) {
        case SnapKind::FaceCentroid:      return 0;   // square (centroid)
        case SnapKind::InnerWireCentroid: return 1;   // plus (center)
        case SnapKind::ArcCenter:         return 1;
        case SnapKind::CircleCenter:      return 1;
        case SnapKind::EdgeMidpoint:      return 2;   // triangle (midpoint)
        case SnapKind::BoundaryVertex:    return 3;   // circle (vertex)
        case SnapKind::EdgeEndpoint:      return 3;
        case SnapKind::AxisPoint:         return 4;   // diamond (axis/apex)
        case SnapKind::ConeApex:          return 4;
        case SnapKind::SurfacePoint:      return 3;   // reserved (free surface point)
    }
    return 0;
}

SnapCandidate make(SnapKind kind, const glm::vec3& pos, const glm::vec3& z,
                   entt::entity e, std::uint64_t key, int index = 0) {
    SnapCandidate c;
    c.kind = kind;
    c.pos = pos;
    const float L = glm::length(z);
    c.z = (L > 1e-12f) ? z / L : glm::vec3(0, 0, 1);
    c.x = deterministicX(c.z);
    c.entity = e;
    c.key = key;
    c.index = index;
    c.glyph = glyphOf(kind);
    return c;
}

bool nearPos(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < kMergeTol; }

// AREA-WEIGHTED world centroid of the triangles whose triFace == faceId (the same
// accumulation Measure's faceWorldArea uses; double precision). Returns area (0 = none).
double faceCentroidWorld(const RenderableMeshComponent& mesh, const glm::mat4& M,
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
        if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) continue;   // malformed tri: skip
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

glm::vec3 deterministicX(const glm::vec3& z) {
    const float L = glm::length(z);
    const glm::vec3 zn = (L > 1e-12f) ? z / L : glm::vec3(0, 0, 1);
    const glm::vec3 gX(1, 0, 0), gY(0, 1, 0);
    // globalX projected off Z; the same projection of globalY when Z is near globalX.
    const glm::vec3 ref = (std::abs(glm::dot(zn, gX)) > 0.9f) ? gY : gX;
    glm::vec3 x = ref - glm::dot(ref, zn) * zn;
    const float xl = glm::length(x);
    if (xl < 1e-9f) {                       // z parallel to the fallback too (cannot happen for
        glm::vec3 alt = gY - glm::dot(gY, zn) * zn;    // unit z, kept as an honest guard)
        const float al = glm::length(alt);
        return (al > 1e-9f) ? alt / al : glm::vec3(0, 1, 0);
    }
    return x / xl;
}

SnapCandidate flipZ(const SnapCandidate& c) {
    SnapCandidate r = c;
    const float zl = glm::length(c.z);
    r.z = (zl > 1e-12f) ? -c.z / zl : glm::vec3(0, 0, -1);
    glm::vec3 x = c.x - glm::dot(c.x, r.z) * r.z;      // X kept, re-orthonormalized
    const float xl = glm::length(x);
    r.x = (xl > 1e-9f) ? x / xl : deterministicX(r.z);
    return r;
}

SnapCandidate rotateX90(const SnapCandidate& c) {
    SnapCandidate r = c;
    const float zl = glm::length(c.z);
    const glm::vec3 zn = (zl > 1e-12f) ? c.z / zl : glm::vec3(0, 0, 1);
    glm::vec3 x = glm::cross(zn, c.x);                 // +90 deg about Z (Rodrigues, theta = 90)
    x -= glm::dot(x, zn) * zn;                         // re-orthonormalize against Z
    const float xl = glm::length(x);
    r.x = (xl > 1e-9f) ? x / xl : deterministicX(zn);
    return r;
}

std::vector<SnapCandidate> candidatesForFace(entt::registry& reg, entt::entity e, int faceId) {
    std::vector<SnapCandidate> out;
    if (!reg.valid(e) || !reg.all_of<BRepFaceComponent>(e)) return out;
    const auto& faces = reg.get<BRepFaceComponent>(e).faces;
    if (faceId < 0 || faceId >= int(faces.size())) return out;
    const BRepFace& bf = faces[std::size_t(faceId)];
    const Frames f = framesOf(reg, e);

    // 1) FaceCentroid -- area-weighted, from the tessellation (never fabricated).
    if (const auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
        glm::dvec3 centroid(0.0);
        if (faceCentroidWorld(*mesh, f.M, faceId, centroid) > 0.0) {
            const glm::vec3 cW(centroid);
            out.push_back(make(SnapKind::FaceCentroid, cW, faceZAt(bf, f, cW), e, bf.faceKey));
            out.back().faceId = faceId;
        }
    }

    // 2) Axis-derived candidates by surface type.
    if (bf.type == 1) {                                        // CYLINDER: 3 axis points
        const glm::vec3 aW = xd(f, bf.axisDir);
        const glm::vec3 e0 = xp(f, bf.axisEnd0), e1 = xp(f, bf.axisEnd1);
        if (glm::length(bf.axisEnd1 - bf.axisEnd0) > kMergeTol) {
            const glm::vec3 pts[3] = { e0, 0.5f * (e0 + e1), e1 };
            for (int i = 0; i < 3; ++i) {
                out.push_back(make(SnapKind::AxisPoint, pts[i], aW, e, bf.faceKey, i));
                out.back().faceId = faceId;
            }
        } else {                                               // rims unknown: middle only
            out.push_back(make(SnapKind::AxisPoint, xp(f, bf.axisPos), aW, e, bf.faceKey, 1));
            out.back().faceId = faceId;
        }
    } else if (bf.type == 2) {                                 // CONE: apex (documented fallback)
        // BRepFace stores neither half-angle nor cone rims -> apex = axisPos (Snap.hpp).
        out.push_back(make(SnapKind::ConeApex, xp(f, bf.axisPos), xd(f, bf.axisDir), e, bf.faceKey));
        out.back().faceId = faceId;
    } else if (bf.type == 3) {                                 // SPHERE: centre as AxisPoint index 1
        out.push_back(make(SnapKind::AxisPoint, xp(f, bf.axisPos), glm::vec3(0, 0, 1), e, bf.faceKey, 1));
        out.back().faceId = faceId;
    }

    // 3) TRUE boundary vertices (BRepVertexComponent adjacency).
    if (const auto* vc = reg.try_get<BRepVertexComponent>(e)) {
        for (int vi = 0; vi < int(vc->verts.size()); ++vi) {
            const BRepVertex& bv = vc->verts[std::size_t(vi)];
            if (std::find(bv.faces.begin(), bv.faces.end(), faceId) == bv.faces.end()) continue;
            const glm::vec3 pW = xp(f, bv.pos);
            out.push_back(make(SnapKind::BoundaryVertex, pW, faceZAt(bf, f, pW), e, bv.vertexKey));
            out.back().faceId = faceId;
            out.back().vertexId = vi;
        }
    }

    // 4) Boundary edges (faceA/faceB adjacency): midpoints, endpoints, arc/wire centres.
    if (const auto* ec = reg.try_get<BRepEdgeComponent>(e)) {
        std::vector<glm::vec3> endpointSeen;                   // in-kind position dedupe
        auto pushEndpoint = [&](const glm::vec3& pW, int edgeId, std::uint64_t key, int endIdx) {
            for (const auto& q : endpointSeen) if (nearPos(q, pW)) return;
            endpointSeen.push_back(pW);
            out.push_back(make(SnapKind::EdgeEndpoint, pW, faceZAt(bf, f, pW), e, key, endIdx));
            out.back().faceId = faceId;
            out.back().edgeId = edgeId;
        };
        for (int ei = 0; ei < int(ec->edges.size()); ++ei) {
            const BRepEdge& be = ec->edges[std::size_t(ei)];
            if (be.faceA != faceId && be.faceB != faceId) continue;
            if (be.kind == BRepEdge::Circle) {
                const glm::vec3 cW = xp(f, be.center);
                const glm::vec3 axW = xd(f, be.axisDir);
                if (be.closed && bf.type == 0) {
                    // PLANAR face + closed circular boundary: InnerWireCentroid approximation
                    // (hole rim / disk boundary -- see Snap.hpp for the documented approximation).
                    out.push_back(make(SnapKind::InnerWireCentroid, cW, faceZAt(bf, f, cW), e, be.edgeKey));
                    out.back().faceId = faceId;
                    out.back().edgeId = ei;
                } else if (be.closed) {
                    // Curved face rim circle: ArcCenter, deduped against coincident AxisPoints
                    // (a cylinder rim's centre IS the rim axis point emitted above).
                    bool dup = false;
                    for (const auto& q : out)
                        if (q.kind == SnapKind::AxisPoint && nearPos(q.pos, cW)) { dup = true; break; }
                    if (!dup) {
                        out.push_back(make(SnapKind::ArcCenter, cW, axW, e, be.edgeKey));
                        out.back().faceId = faceId;
                        out.back().edgeId = ei;
                    }
                } else {
                    // OPEN arc: ArcCenter + the arc endpoints.
                    out.push_back(make(SnapKind::ArcCenter, cW, axW, e, be.edgeKey));
                    out.back().faceId = faceId;
                    out.back().edgeId = ei;
                    pushEndpoint(xp(f, be.p0), ei, be.edgeKey, 0);
                    pushEndpoint(xp(f, be.p1), ei, be.edgeKey, 1);
                }
            } else {
                // Line / Other: midpoint (chord for lines, middle polyline sample otherwise) + ends.
                glm::vec3 midL = 0.5f * (be.p0 + be.p1);
                if (be.kind == BRepEdge::Other && !be.polyline.empty())
                    midL = be.polyline[be.polyline.size() / 2];
                const glm::vec3 mW = xp(f, midL);
                out.push_back(make(SnapKind::EdgeMidpoint, mW, faceZAt(bf, f, mW), e, be.edgeKey));
                out.back().faceId = faceId;
                out.back().edgeId = ei;
                pushEndpoint(xp(f, be.p0), ei, be.edgeKey, 0);
                pushEndpoint(xp(f, be.p1), ei, be.edgeKey, 1);
            }
        }
    }
    return out;
}

std::vector<SnapCandidate> candidatesForEdge(entt::registry& reg, entt::entity e, int edgeId) {
    std::vector<SnapCandidate> out;
    if (!reg.valid(e) || !reg.all_of<BRepEdgeComponent>(e)) return out;
    const auto& edges = reg.get<BRepEdgeComponent>(e).edges;
    if (edgeId < 0 || edgeId >= int(edges.size())) return out;
    const BRepEdge& be = edges[std::size_t(edgeId)];
    const Frames f = framesOf(reg, e);

    auto tag = [&](SnapCandidate& c) { c.edgeId = edgeId; };
    if (be.kind == BRepEdge::Circle) {
        // Hovered circle/arc: CircleCenter with +Z along the circle axis; open arcs add ends.
        out.push_back(make(SnapKind::CircleCenter, xp(f, be.center), xd(f, be.axisDir), e, be.edgeKey));
        tag(out.back());
        if (!be.closed) {
            const glm::vec3 axW = xd(f, be.axisDir);
            out.push_back(make(SnapKind::EdgeEndpoint, xp(f, be.p0), axW, e, be.edgeKey, 0)); tag(out.back());
            out.push_back(make(SnapKind::EdgeEndpoint, xp(f, be.p1), axW, e, be.edgeKey, 1)); tag(out.back());
        }
    } else {
        // Line (and Other, honestly approximated): Z along the edge direction.
        const glm::vec3 chord = be.p1 - be.p0;
        const float cl = glm::length(chord);
        glm::vec3 dW = (cl > 1e-12f) ? xd(f, chord / cl) : glm::vec3(0, 0, 1);
        if (be.kind == BRepEdge::Line && glm::length(be.axisDir) > 1e-12f)
            dW = xd(f, be.axisDir);                            // exact stored line direction
        glm::vec3 midL = 0.5f * (be.p0 + be.p1);
        if (be.kind == BRepEdge::Other && !be.polyline.empty())
            midL = be.polyline[be.polyline.size() / 2];        // middle sample (documented approx)
        out.push_back(make(SnapKind::EdgeMidpoint, xp(f, midL), dW, e, be.edgeKey));    tag(out.back());
        out.push_back(make(SnapKind::EdgeEndpoint, xp(f, be.p0), dW, e, be.edgeKey, 0)); tag(out.back());
        out.push_back(make(SnapKind::EdgeEndpoint, xp(f, be.p1), dW, e, be.edgeKey, 1)); tag(out.back());
    }
    return out;
}

SnapCandidate candidateForVertex(entt::registry& reg, entt::entity e, int vertexId, int throughFaceId) {
    SnapCandidate c;                                           // entity == entt::null => invalid
    if (!reg.valid(e) || !reg.all_of<BRepVertexComponent>(e)) return c;
    const auto& verts = reg.get<BRepVertexComponent>(e).verts;
    if (vertexId < 0 || vertexId >= int(verts.size())) return c;
    const BRepVertex& bv = verts[std::size_t(vertexId)];
    const Frames f = framesOf(reg, e);
    const glm::vec3 pW = xp(f, bv.pos);

    // Frame from the face the cursor was over; -1 = first adjacent face; none -> +global Z
    // (the Onshape curve-point rule).
    glm::vec3 z(0, 0, 1);
    int usedFace = -1;
    const auto* fc = reg.try_get<BRepFaceComponent>(e);
    int fid = throughFaceId;
    if (fid < 0 && !bv.faces.empty()) fid = bv.faces.front();
    if (fc && fid >= 0 && fid < int(fc->faces.size())) {
        z = faceZAt(fc->faces[std::size_t(fid)], f, pW);
        usedFace = fid;
    }
    c = make(SnapKind::BoundaryVertex, pW, z, e, bv.vertexKey);
    c.faceId = usedFace;
    c.vertexId = vertexId;
    return c;
}

} // namespace krs::snap
