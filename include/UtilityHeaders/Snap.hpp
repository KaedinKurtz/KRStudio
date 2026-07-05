#pragma once
// ===========================================================================
// SNAP ENGINE (mate-selector sprint P3, krs::snap) -- the CANDIDATE + FRAME
// engine behind the Onshape-style mate-connector inference points, with the
// Fusion glyph legend. Pure CPU, no rendering: a hovered face/edge/vertex
// resolves to the discrete set of snappable points (centroids, corners,
// midpoints, arc/circle centers, axis points, apexes), each carrying a full
// oriented WORLD frame a MateConnector can be authored from directly.
//
// CANDIDATE SETS (the documented Onshape inference spec + Fusion additions),
// as shipped:
//   candidatesForFace(faceId):
//     - FaceCentroid: AREA-WEIGHTED centroid of the face's tessellation
//       triangles (triFace == faceId), the same accumulation Measure's
//       faceWorldArea uses. Honest: a face with no triangles / no mesh emits
//       NO centroid (never fabricated).
//     - BoundaryVertex: every TRUE B-Rep vertex adjacent to the face
//       (BRepVertexComponent adjacency).
//     - EdgeMidpoint + EdgeEndpoint for each boundary edge (BRepEdge
//       faceA/faceB == faceId). Line edges: chord midpoint; Other edges: the
//       middle polyline sample (documented approximation). Endpoints dedupe
//       BY POSITION within the kind (a quad's 8 raw endpoints -> 4); they are
//       kept as their OWN kind even where a BoundaryVertex shares the
//       position (distinct durability channel: edgeKey+index vs vertexKey) --
//       a UI stacking them should prefer the BoundaryVertex.
//     - Circular boundary edges: on a PLANAR face a CLOSED circle emits
//       InnerWireCentroid at its centre (approximation of the true
//       inner-wire walk: any closed circular boundary of a plane is treated
//       as a hole rim / disk boundary -- the outer boundary of a full disk
//       over-emits one, documented, harmless); an OPEN arc emits ArcCenter.
//       On curved faces closed circles emit ArcCenter, deduped against the
//       cylinder AxisPoints they coincide with (a rim circle's centre IS the
//       rim axis point).
//     - CYLINDER faces: three AxisPoints -- rim end 0 (index 0), middle
//       (index 1), rim end 1 (index 2) from axisEnd0/1; if the rims are
//       unknown (untrimmed/synthetic: axisEnd0 == axisEnd1) a single
//       AxisPoint at axisPos (index 1).
//     - CONE faces: ConeApex. BRepFace stores neither the half-angle nor cone
//       rims, so the DOCUMENTED FALLBACK apex = axisPos (the analytic apex
//       axisPos + axisDir * radius/tan(halfAngle) needs a half-angle channel;
//       flagged for a future BRepFace extension).
//     - SPHERE faces: the centre as AxisPoint index 1.
//   candidatesForEdge(edgeId):
//     - Line: EdgeMidpoint + both EdgeEndpoints (index 0/1).
//     - Circle/arc: CircleCenter (+Z along the circle axis); an OPEN arc adds
//       its endpoints. (Arc curve-midpoint: future.)
//     - Other: middle polyline sample as EdgeMidpoint + endpoints.
//   candidateForVertex(vertexId, throughFaceId):
//     - ONE candidate; the frame comes from the face the cursor hovered
//       THROUGH (throughFaceId; -1 = the vertex's first adjacent face; no
//       adjacent face at all -> Z = +global Z, the Onshape curve-point rule).
//
// FRAME RULES (deterministic + session-stable):
//   +Z  = OUT of the material for face-derived candidates:
//         planar face   -> the face normal (BRepFace.normal, which CadImporter
//                          now guarantees is orientation-corrected OUTWARD);
//         cylinder/cone -> Z ALONG the axis for axis points/apex AND for the
//                          face's boundary candidates (documented
//                          approximation of the radial surface normal);
//         sphere        -> radial (position - centre) where defined, +global Z
//                          at the centre itself.
//         Hovered-EDGE candidates: Z along the line direction / the circle
//         axis. Vertex candidates: the through-face's rule, else +global Z.
//   X   = deterministicX(Z) = normalize(globalX - dot(globalX, Z) * Z),
//         falling back to the same projection of globalY when
//         |dot(Z, globalX)| > 0.9 -- no time/session dependence.
//   Corrections (pure math, used by the UI): flipZ (Z -> -Z, X kept and
//   re-orthonormalized) and rotateX90 (X rotates +90 deg about Z per click --
//   the Onshape 'reorient secondary axis'; four clicks = identity).
//
// All outputs are WORLD-space (through the entity's TransformComponent).
// Invalid ids / missing components return an empty vector (or, for
// candidateForVertex, a candidate with entity == entt::null). Never crashes.
// ===========================================================================
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>

namespace krs::snap {

enum class SnapKind { FaceCentroid, InnerWireCentroid, BoundaryVertex, EdgeMidpoint, EdgeEndpoint,
                      ArcCenter, CircleCenter, AxisPoint, ConeApex, SurfacePoint };

struct SnapCandidate {
    SnapKind kind = SnapKind::SurfacePoint;
    glm::vec3 pos{ 0.0f };           // WORLD
    glm::vec3 z{ 0.0f, 0.0f, 1.0f }; // WORLD frame: +Z out of material / along axis
    glm::vec3 x{ 1.0f, 0.0f, 0.0f }; // deterministic secondary (see deterministicX)
    entt::entity entity = entt::null;
    std::uint64_t key = 0;           // owning feature's faceKey/edgeKey/vertexKey (durable re-anchor)
    int faceId = -1, edgeId = -1, vertexId = -1;   // hints (indices into the owning components)
    int index = 0;                   // disambiguator within (key, kind): AxisPoint 0/1/2, endpoint 0/1
    int glyph = 0;                   // Fusion legend: 0=square(centroid) 1=plus(center)
                                     //   2=triangle(midpoint) 3=circle(vertex) 4=diamond(axis/apex)
};

// Candidates for a HOVERED face / edge (world transform applied through the entity's
// TransformComponent). Empty on bad ids / missing components; never crashes.
std::vector<SnapCandidate> candidatesForFace(entt::registry& reg, entt::entity e, int faceId);
std::vector<SnapCandidate> candidatesForEdge(entt::registry& reg, entt::entity e, int edgeId);

// Candidate for a hovered TRUE vertex. throughFaceId = the face the cursor was over
// (-1 = first adjacent face; no adjacent face -> Z = +global Z per the Onshape
// curve-point rule). Invalid input -> returned candidate has entity == entt::null.
SnapCandidate candidateForVertex(entt::registry& reg, entt::entity e, int vertexId, int throughFaceId);

// The session-stable secondary axis (see FRAME RULES above). Always unit, always
// perpendicular to z (|dot| < 1e-6), identical across calls for the same z.
glm::vec3 deterministicX(const glm::vec3& z);

// Frame corrections (pure math): flip the primary (Z -> -Z, X kept, re-orthonormalized)
// and rotate the secondary +90 deg about Z (four applications return the original X).
SnapCandidate flipZ(const SnapCandidate& c);
SnapCandidate rotateX90(const SnapCandidate& c);

// KRS_SNAP_SELFTEST gate (SnapGate.cpp): headless synthetic entities, analytic
// assertions with measured numbers, NEG-CTRLs. True iff every check passes.
bool runSnapGate();

} // namespace krs::snap
