#pragma once
// ===========================================================================
// OMPL sprint, Phase 3 — sub-feature SELECTION BACKEND (krs::sel).
//
// A ray resolves to a SPECIFIC B-Rep face and its EXACT analytic parameters,
// read from the OCCT-derived BRepFaceComponent (NOT a mesh/RANSAC fit), plus a
// selection-indicator GEOMETRY computed as DATA (a ring on a cylinder cross-
// section, a square outline on a plane) — rendering is DEFERRED to a supervised
// UI session, this is the gateable backend only.
//
// The backend is OCCT-free: it reads the ECS components that CadImporter (OCCT)
// populated (RenderableMeshComponent.triFace -> BRepFaceComponent.faces). The
// production pick (krs::pick::pickMesh) is reused, so the gate exercises the
// real selection path. Analytic params are transformed to WORLD via the entity
// TransformComponent (reduces to identity for an origin-placed part).
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

#include "components.hpp"     // TransformComponent, RenderableMeshComponent, BRepFace(Component)
#include "RayPick.hpp"        // krs::pick::Ray / pickMesh

namespace krs::sel {

enum class FeatureType { None = -1, Plane = 0, Cylinder = 1, Cone = 2, Sphere = 3, Other = 4,
                         Vertex = 5,     // a measure-mode point pick (nearest tessellation vertex)
                         EdgeCircle = 6, // a TRUE B-Rep circle/arc edge (BRepEdgeComponent)
                         EdgeLine = 7 }; // a TRUE B-Rep line edge

struct Selection {
    bool valid = false;                    // a B-Rep face was resolved
    entt::entity entity = entt::null;
    int faceId = -1;
    int edgeId = -1;                       // BRepEdgeComponent.edges index for EdgeCircle/EdgeLine picks
    int vertexId = -1;                     // BRepVertexComponent.verts index for TRUE corner picks
                                           //   (FeatureType::Vertex with vertexId=-1 = tessellation-vertex
                                           //   fallback from the measure tool's Ctrl pick)
    int groupId = 0;                       // measure-mode ITEM id: shift-extended faces share one group
    FeatureType type = FeatureType::None;
    glm::vec3 hitPoint{ 0.0f };            // ray-surface intersection (world)
    // analytic feature params in WORLD frame (from the B-Rep, never a mesh fit):
    glm::vec3 axisPos{ 0.0f };             // a point on the axis (cyl/cone) or centre (sphere)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f }; // unit axis direction (cyl/cone)
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };  // surface normal (plane)
    float radius = 0.0f;                   // metres
    std::uint64_t faceKey = 0;             // stable topological id (BRepFace.faceKey) for mate-connector anchor
                                           //   EDGE picks carry BRepEdge.edgeKey here (same anchor contract)
    glm::vec3 axisEnd0{ 0.0f };            // cylinder rim centres (world); both zero => no trimmed B-Rep
    glm::vec3 axisEnd1{ 0.0f };
    // EDGE pick conventions (type EdgeCircle/EdgeLine, resolved by krs::sel::resolveEdge in
    // EdgeSelect.hpp): EdgeCircle -> axisPos=world centre, axisDir=plane normal, radius=world r,
    // axisEnd0/1=arc endpoints (equal when closed). EdgeLine -> axisEnd0/1=endpoints,
    // axisDir=direction, axisPos=midpoint. hitPoint = nearest point on the edge to the pick ray.
};

// Resolve a world-space ray to the specific B-Rep face it hits + its exact
// analytic parameters. Returns an invalid Selection on a miss or a non-B-Rep
// body (e.g. a raw mesh with no BRepFaceComponent).
// Resolve a single ray-hit (entity + triangle) into a Selection with analytic B-Rep params.
inline Selection resolveHit(entt::registry& reg, const krs::pick::PickHit& hit) {
    Selection s;
    s.entity = hit.entity;
    s.hitPoint = hit.worldPos;
    if (!reg.all_of<RenderableMeshComponent, BRepFaceComponent>(hit.entity)) return s;
    const auto& mesh = reg.get<RenderableMeshComponent>(hit.entity);
    const auto& brep = reg.get<BRepFaceComponent>(hit.entity);
    if (hit.tri < 0 || hit.tri >= int(mesh.triFace.size())) return s;
    const int fid = mesh.triFace[hit.tri];
    if (fid < 0 || fid >= int(brep.faces.size())) return s;
    const BRepFace& bf = brep.faces[fid];

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(hit.entity)) M = xf->getTransform();
    const glm::mat3 R = glm::mat3(glm::inverseTranspose(M));   // normals/directions

    s.valid = true;
    s.faceId = fid;
    s.type = static_cast<FeatureType>(bf.type);
    s.radius = bf.radius;                                 // scale-free B-Rep radius (part placed unit-scale)
    s.faceKey = bf.faceKey;                               // carry the stable topological id to the mate author
    s.axisPos = glm::vec3(M * glm::vec4(bf.axisPos, 1.0f));
    s.axisDir = glm::normalize(R * bf.axisDir);
    s.normal = glm::normalize(R * bf.normal);
    s.axisEnd0 = glm::vec3(M * glm::vec4(bf.axisEnd0, 1.0f));   // bore rim centres -> world (for rim-snap)
    s.axisEnd1 = glm::vec3(M * glm::vec4(bf.axisEnd1, 1.0f));
    return s;
}

inline Selection pick(entt::registry& reg, const krs::pick::Ray& ray) {
    const auto hit = krs::pick::pickMesh(reg, ray);
    if (!hit) return Selection{};                        // miss -> no selection
    return resolveHit(reg, *hit);
}

// Resolve a KNOWN (entity, faceId) into a Selection with world analytic params from the entity's
// CURRENT transform -- no ray needed. This is the re-derivation primitive: anything holding a stable
// (entity, faceId) reference (a mirrored-viewport pick, a stored selection after the body moved) can
// refresh its world frame instead of trusting a frozen click-time snapshot.
inline Selection resolveFace(entt::registry& reg, entt::entity e, int faceId) {
    Selection s;
    s.entity = e;
    if (!reg.valid(e) || !reg.all_of<BRepFaceComponent>(e)) return s;
    const auto& brep = reg.get<BRepFaceComponent>(e);
    if (faceId < 0 || faceId >= int(brep.faces.size())) return s;
    const BRepFace& bf = brep.faces[faceId];
    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
    const glm::mat3 R = glm::mat3(glm::inverseTranspose(M));
    s.valid = true;
    s.faceId = faceId;
    s.type = static_cast<FeatureType>(bf.type);
    s.radius = bf.radius;
    s.faceKey = bf.faceKey;
    s.axisPos = glm::vec3(M * glm::vec4(bf.axisPos, 1.0f));
    s.axisDir = glm::normalize(R * bf.axisDir);
    s.normal = glm::normalize(R * bf.normal);
    s.axisEnd0 = glm::vec3(M * glm::vec4(bf.axisEnd0, 1.0f));
    s.axisEnd1 = glm::vec3(M * glm::vec4(bf.axisEnd1, 1.0f));
    s.hitPoint = s.axisPos;
    return s;
}

// CYLINDER-PREFERRED feature pick (the robot-builder mate workflow picks BORES). Clicking "on a bore"
// usually lands the ray on the flat face AROUND/in front of the hole -- nearest-triangle picking then
// returns that PLANE, never the bore, and one-hit-per-entity x-ray can't reach the cylinder on the
// same part. So: return the NEAREST CYLINDER face the ray hits (the bore the operator is aiming at),
// and only fall back to the plain nearest face when the ray hits no cylinder at all.
inline Selection pickPreferCylinder(entt::registry& reg, const krs::pick::Ray& ray) {
    krs::pick::PickHit bestCyl, bestAny;                  // .t defaults to +inf
    for (auto e : reg.view<TransformComponent, RenderableMeshComponent>()) {
        const auto& xf = reg.get<TransformComponent>(e);
        const auto& mesh = reg.get<RenderableMeshComponent>(e);
        if (mesh.indices.size() < 3 || mesh.vertices.empty()) continue;
        const auto* brep = reg.try_get<BRepFaceComponent>(e);
        const glm::mat4 M = xf.getTransform();
        const glm::mat4 invM = glm::inverse(M);
        const glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));
        // AABB pre-cull: this runs on EVERY mouse move (updateHover) over every renderable; skip
        // whole meshes the local ray cannot hit. Unset AABBs (min==max) are never culled.
        if (mesh.aabbMin != mesh.aabbMax) {
            float tE, tX;
            if (!krs::pick::rayAABB(roL, rdL, mesh.aabbMin - 1e-4f, mesh.aabbMax + 1e-4f, tE, tX)) continue;
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
            const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
            const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
            float tL;
            if (!krs::pick::rayTriangle(roL, rdL, a, b, c, tL)) continue;
            if (tL <= 1e-6f) continue;
            const glm::vec3 worldHit = glm::vec3(M * glm::vec4(roL + rdL * tL, 1.0f));
            const float worldT = glm::dot(worldHit - ray.origin, ray.dir);
            if (worldT <= 1e-5f) continue;
            const int tri = int(i / 3);
            if (worldT < bestAny.t) { bestAny.entity = e; bestAny.worldPos = worldHit; bestAny.t = worldT; bestAny.tri = tri; }
            if (brep && tri < int(mesh.triFace.size())) {
                const int fid = mesh.triFace[tri];
                if (fid >= 0 && fid < int(brep->faces.size()) && brep->faces[fid].type == 1 && worldT < bestCyl.t) {
                    bestCyl.entity = e; bestCyl.worldPos = worldHit; bestCyl.t = worldT; bestCyl.tri = tri;
                }
            }
        }
    }
    if (bestCyl.entity != entt::null) return resolveHit(reg, bestCyl);   // the bore the operator aimed at
    if (bestAny.entity != entt::null) return resolveHit(reg, bestAny);   // fallback: nearest face
    return Selection{};
}

// X-RAY feature pick: resolve the cycleIndex-th body the ray pierces (near->far), so repeated clicks
// at the same pixel walk DEEPER and can select a bore OCCLUDED behind the component in front of it.
// cycleIndex is taken modulo the hit count; an empty result yields an invalid Selection. hitCount (if
// non-null) returns how many bodies the ray pierced, so the UI can show "feature 2/5" and know when to wrap.
inline Selection pickCycled(entt::registry& reg, const krs::pick::Ray& ray, int cycleIndex, int* hitCount = nullptr) {
    const auto hits = krs::pick::pickMeshAll(reg, ray);
    if (hitCount) *hitCount = int(hits.size());
    if (hits.empty()) return Selection{};
    const int n = int(hits.size());
    const int idx = ((cycleIndex % n) + n) % n;
    return resolveHit(reg, hits[idx]);
}

// Selection-indicator GEOMETRY as DATA (no rendering). For a cylinder/cone the
// indicator is a ring on the cross-section through the hit point (centre =
// projection of the hit onto the axis line); for a plane it is a square outline
// on the surface around the hit; otherwise a single point at the hit.
struct IndicatorGeometry {
    FeatureType type = FeatureType::None;
    glm::vec3 center{ 0.0f };
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };  // ring plane normal (cyl axis) or surface normal (plane)
    float radius = 0.0f;
    std::vector<glm::vec3> points;         // world-space ring / outline vertices
};

inline IndicatorGeometry indicator(const Selection& sel, int segments = 32, float planeHalf = 0.01f) {
    IndicatorGeometry g;
    if (!sel.valid) return g;
    g.type = sel.type;
    if (sel.type == FeatureType::EdgeLine) {               // TRUE line edge: highlight the segment itself
        g.center = sel.axisPos;
        g.normal = sel.axisDir;
        g.points.push_back(sel.axisEnd0);
        g.points.push_back(sel.axisEnd1);
        return g;
    }
    if (sel.type == FeatureType::EdgeCircle) {             // TRUE circle edge: ring at the circle itself
        g.center = sel.axisPos;
        g.normal = sel.axisDir;
        g.radius = sel.radius;
        glm::vec3 u = glm::cross(sel.axisDir, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(sel.axisDir, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(sel.axisDir, u));
        for (int i = 0; i < segments; ++i) {
            const float a = 6.2831853f * float(i) / float(segments);
            g.points.push_back(g.center + sel.radius * (std::cos(a) * u + std::sin(a) * v));
        }
        return g;
    }
    if (sel.type == FeatureType::Cylinder || sel.type == FeatureType::Cone) {
        const glm::vec3 d = sel.axisDir;
        // RIM SNAP: if the trimmed B-Rep gave the two end-cap centres, ring the bore EDGE NEAREST the
        // click (internal or external) so the ring lands on a real rim, not mid-wall. Else (synthetic/
        // demo faces with no caps) fall back to the cross-section through the hit point.
        const bool haveCaps = glm::distance(sel.axisEnd0, sel.axisEnd1) > 1e-5f;
        if (haveCaps) {
            g.center = (glm::distance(sel.hitPoint, sel.axisEnd0) <= glm::distance(sel.hitPoint, sel.axisEnd1))
                       ? sel.axisEnd0 : sel.axisEnd1;
        } else {
            g.center = sel.axisPos + glm::dot(sel.hitPoint - sel.axisPos, d) * d;  // projection onto axis line
        }
        g.normal = d;
        g.radius = sel.radius;
        // an orthonormal basis (u,v) spanning the cross-section plane.
        glm::vec3 u = glm::cross(d, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(d, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(d, u));
        for (int i = 0; i < segments; ++i) {
            const float a = 6.2831853f * float(i) / float(segments);
            g.points.push_back(g.center + sel.radius * (std::cos(a) * u + std::sin(a) * v));
        }
    } else if (sel.type == FeatureType::Plane) {
        g.center = sel.hitPoint;
        g.normal = sel.normal;
        glm::vec3 u = glm::cross(sel.normal, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(sel.normal, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(sel.normal, u));
        const glm::vec3 corners[4] = { u + v, -u + v, -u - v, u - v };
        for (const auto& c : corners) g.points.push_back(g.center + planeHalf * c);
    } else {
        g.center = sel.hitPoint;
        g.points.push_back(sel.hitPoint);
    }
    return g;
}

// ===========================================================================
// SELECTION-HIGHLIGHTS sprint -- the VISUAL half (state + render geometry).
//
// Two feature Selections refer to the SAME sub-feature iff they share the
// resolving identity (entity, faceId). The highlight/selection is REQUIRED to
// track the TRUE feature the ray resolved -- never a neighbour or the dominant
// face -- so identity equality IS the gateable contract (HIGHLIGHT-MATCHES).
// ===========================================================================
inline bool sameFeature(const Selection& a, const Selection& b) {
    return a.valid && b.valid && a.entity == b.entity
        && a.faceId == b.faceId && a.edgeId == b.edgeId    // edges identify by (entity, edgeId)
        && a.vertexId == b.vertexId;                       // true corners by (entity, vertexId)
}

// Per-scene selection state held in registry.ctx() (the SceneProperties pattern).
// `hover` is what a HOVER ray currently resolves to (highlight preview); `selected`
// is the accumulating SET a CLICK commits (the multi-feature selection the robot-
// builder needs). Both store the EXACT krs::sel::pick result -- no re-derivation.
struct SelectionState {
    // Feature picking is a TRIGGERED MODE, never the ambient default: with it always-on, every
    // stray click committed a persistent glowing feature (and armed the mate workflow), which made
    // selecting ANYTHING hazardous. Armed by: the Builder's "Choose joint bores" button (with a
    // boreQuota that auto-disarms), the ribbon Measure mode, or the View-menu manual override.
    bool enabled = false;
    bool fifoTwoBores = false;           // robot-builder bore picking: keep AT MOST 2 CYLINDER (bore-edge)
                                         // selections, FIFO -- clicking a 3rd bore evicts the OLDEST, so
                                         // there are only ever 2 bore edges selected at a time.
    int  boreQuota = 0;                  // choose-bores mode: >0 auto-DISARMS (enabled=false) once this many
                                         // bores are committed; the picks stay selected for the Define/Mate.
    bool measureMode = false;            // Onshape-style measure: FIFO-2 ITEMS (groups), any feature kind
    int  nextGroupId = 1;                // measure item id allocator
    Selection hover;                     // current hovered feature (valid==false => none)
    std::vector<Selection> selected;     // committed set (accumulates across clicks)
};

// GLOBAL FIFO CAP: committed features accumulated without bound (every plane ever clicked stayed as
// an always-on-top overlay until individually re-clicked or cleared -- the scene filled with orphan
// glowing squares). Cap the whole set FIFO; no workflow holds more than a handful of features.
inline void enforceSelectionCap(SelectionState& st, std::size_t cap = 8) {
    while (st.selected.size() > cap)
        st.selected.erase(st.selected.begin());          // evict the oldest committed feature
}

// Enforce the FIFO-two-bores rule: while more than two CYLINDER (bore) features are selected, drop the
// OLDEST cylinder. Non-cylinder selections are left alone. No-op unless st.fifoTwoBores is set.
inline void enforceFifoTwoBores(SelectionState& st) {
    if (!st.fifoTwoBores) return;
    auto cylCount = [&] { int n = 0; for (const auto& s : st.selected) if (s.valid && s.type == FeatureType::Cylinder) ++n; return n; };
    while (cylCount() > 2) {
        for (std::size_t i = 0; i < st.selected.size(); ++i)
            if (st.selected[i].valid && st.selected[i].type == FeatureType::Cylinder) {
                st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));   // evict the oldest bore
                break;
            }
    }
}

// HOVER: resolve the ray and store it as the hovered feature (replaces prior hover).
// A miss stores an invalid hover (highlight disappears). The stored hover IS pick().
inline void updateHover(SelectionState& st, entt::registry& reg, const krs::pick::Ray& ray) {
    st.hover = pickPreferCylinder(reg, ray);   // bores win over the flat face around them
}

// CHOOSE-BORES completion: once the quota of committed bores is reached the mode DISARMS ITSELF
// (enabled=false) with the picks kept selected -- the operator clicks exactly the bores they want
// and the viewport goes back to normal clicking, no lingering pick-anything hazard.
inline void autoDisarmOnQuota(SelectionState& st) {
    if (!st.fifoTwoBores || st.boreQuota <= 0) return;
    int n = 0;
    for (const auto& q : st.selected) if (q.valid && q.type == FeatureType::Cylinder) ++n;
    if (n >= st.boreQuota) { st.enabled = false; st.boreQuota = 0; }
}

// CLICK / COMMIT: resolve the ray; on a hit ADD the feature to the selected SET
// (it ACCUMULATES -- a second pick does NOT clear the first). Re-picking an already-
// selected feature TOGGLES it off. A miss leaves the set untouched (does not clear).
// `additive=false` replaces the set with just this feature (single-select mode).
// Returns the resolved Selection (the exact pick result -- the highlight tracks THIS).
inline Selection commitSelection(SelectionState& st, entt::registry& reg,
                                 const krs::pick::Ray& ray, bool additive = true) {
    const Selection s = pickPreferCylinder(reg, ray);   // bores win over the flat face around them
    // BORE-COLLECT mode (the Builder's fifoTwoBores): ONLY cylinders join the set, and clicking
    // anything that is NOT a bore -- a flat face, empty space -- CLEARS it. Accidental plane picks
    // used to persist as glowing overlays that later orphaned in space when their link moved.
    if (st.fifoTwoBores && (!s.valid || s.type != FeatureType::Cylinder)) {
        st.selected.clear();
        return s;
    }
    if (!s.valid) return s;                              // miss -> no change to the set
    for (std::size_t i = 0; i < st.selected.size(); ++i) {
        if (sameFeature(st.selected[i], s)) {            // toggle off if re-picked
            st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
            return s;
        }
    }
    if (!additive) st.selected.clear();
    st.selected.push_back(s);
    enforceFifoTwoBores(st);                             // keep only the 2 most-recent bore edges (FIFO)
    enforceSelectionCap(st);                             // global FIFO cap (no unbounded overlay pile-up)
    autoDisarmOnQuota(st);                               // choose-bores mode ends itself once the pair is in
    return s;
}

// X-RAY commit: like commitSelection but resolves the cycleIndex-th body the ray pierces
// (near->far via pickCycled), so an occluded bore behind a front component can be committed by
// clicking the same pixel repeatedly. Behaviour on the resolved feature is identical (accumulate /
// toggle / replace).
inline Selection commitSelectionCycled(SelectionState& st, entt::registry& reg,
                                       const krs::pick::Ray& ray, int cycleIndex, bool additive = true) {
    const Selection s = pickCycled(reg, ray, cycleIndex);
    // Same bore-collect rule as commitSelection: in fifoTwoBores mode a non-bore commit CLEARS.
    if (st.fifoTwoBores && (!s.valid || s.type != FeatureType::Cylinder)) {
        st.selected.clear();
        return s;
    }
    if (!s.valid) return s;                              // miss / non-B-Rep at this depth -> no change
    for (std::size_t i = 0; i < st.selected.size(); ++i) {
        if (sameFeature(st.selected[i], s)) {            // toggle off if re-picked
            st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
            return s;
        }
    }
    if (!additive) st.selected.clear();
    st.selected.push_back(s);
    enforceFifoTwoBores(st);                             // keep only the 2 most-recent bore edges (FIFO)
    enforceSelectionCap(st);                             // global FIFO cap (no unbounded overlay pile-up)
    autoDisarmOnQuota(st);                               // choose-bores mode ends itself once the pair is in
    return s;
}

// ---------------------------------------------------------------------------
// MEASURE MODE (Onshape-style) -- the FIFO-2 ITEM buffer.
// An ITEM (groupId) is one logical measurand: usually a single feature, or a
// shift-extended set of faces treated as one plane (areas sum). A plain click
// STARTS a new item; shift-click JOINS the current one; at most two items live
// (the OLDEST is evicted whole). Re-picking a face toggles it off.
// ---------------------------------------------------------------------------

// Vertex pick (measure mode, Ctrl+click): the nearest vertex of the hit triangle. On a
// tessellated B-Rep the tessellation vertices lie on model edges/corners, so a corner click
// lands on a TRUE model vertex; on a raw mesh it is honestly the nearest mesh vertex.
inline Selection pickVertex(entt::registry& reg, const krs::pick::Ray& ray) {
    const auto hit = krs::pick::pickMesh(reg, ray);
    if (!hit) return Selection{};
    const auto* mesh = reg.try_get<RenderableMeshComponent>(hit->entity);
    if (!mesh || hit->tri < 0 || std::size_t(hit->tri) * 3 + 2 >= mesh->indices.size()) return Selection{};
    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(hit->entity)) M = xf->getTransform();
    float bestD = 3.4e38f; glm::vec3 bestP(0.0f);
    for (int k = 0; k < 3; ++k) {
        const glm::vec3 p = glm::vec3(M * glm::vec4(
            mesh->vertices[mesh->indices[std::size_t(hit->tri) * 3 + std::size_t(k)]].position, 1.0f));
        const float d = glm::distance(p, hit->worldPos);
        if (d < bestD) { bestD = d; bestP = p; }
    }
    Selection s;
    s.valid = true; s.entity = hit->entity; s.faceId = -1;
    s.type = FeatureType::Vertex;
    s.hitPoint = bestP;
    return s;
}

// Measure commit over a PRE-RESOLVED Selection (the viewport resolves edge-vs-face preference
// before calling): extend=true (shift) joins the current item. Toggle-off applies to identifiable
// features (faces and edges); vertices never toggle.
inline Selection commitMeasureResolved(SelectionState& st, Selection s, bool extend) {
    if (!s.valid) return s;                              // miss -> buffer untouched
    if (s.faceId >= 0 || s.edgeId >= 0) {
        for (std::size_t i = 0; i < st.selected.size(); ++i)
            if (sameFeature(st.selected[i], s)) {
                st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
                return s;
            }
    }
    if (extend && !st.selected.empty()) s.groupId = st.selected.back().groupId;   // join the current item
    else                                s.groupId = st.nextGroupId++;             // start a new item
    st.selected.push_back(s);
    // FIFO-2 items: while more than two DISTINCT groups live, evict the oldest group whole.
    auto distinctGroups = [&st] {
        std::vector<int> g;
        for (const auto& q : st.selected)
            if (std::find(g.begin(), g.end(), q.groupId) == g.end()) g.push_back(q.groupId);
        return g;
    };
    std::vector<int> ids = distinctGroups();
    while (ids.size() > 2) {
        const int oldest = ids.front();
        st.selected.erase(std::remove_if(st.selected.begin(), st.selected.end(),
                              [oldest](const Selection& q) { return q.groupId == oldest; }),
                          st.selected.end());
        ids = distinctGroups();
    }
    return s;
}

// Measure commit from a ray (face/vertex paths; the viewport calls commitMeasureResolved directly
// when a TRUE EDGE pick wins the tolerance race -- see EdgeSelect.hpp).
inline Selection commitMeasure(SelectionState& st, entt::registry& reg,
                               const krs::pick::Ray& ray, bool extend, bool vertexPick = false) {
    return commitMeasureResolved(st, vertexPick ? pickVertex(reg, ray) : pickPreferCylinder(reg, ray),
                                 extend);
}

// Commit a PRE-RESOLVED Selection into the plain accumulating set (same toggle/FIFO semantics as
// commitSelection) -- the edge-pick path lands here.
inline Selection commitResolved(SelectionState& st, Selection s, bool additive = true) {
    if (st.fifoTwoBores && (!s.valid || s.type != FeatureType::Cylinder)) {
        st.selected.clear();
        return s;
    }
    if (!s.valid) return s;
    for (std::size_t i = 0; i < st.selected.size(); ++i) {
        if (sameFeature(st.selected[i], s)) {
            st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
            return s;
        }
    }
    if (!additive) st.selected.clear();
    st.selected.push_back(s);
    enforceFifoTwoBores(st);
    enforceSelectionCap(st);
    autoDisarmOnQuota(st);
    return s;
}

inline void clearSelection(SelectionState& st) { st.selected.clear(); }

// PER-FRAME REFRESH: re-derive every committed selection's world params from its (entity, faceId)
// at the CURRENT transforms, so the highlight rings TRAVEL WITH a moving robot instead of orphaning
// in space where the bore was clicked. The rim intent (nearest end at click time) is preserved and
// re-expressed on the fresh frame. Selections whose entity died are dropped; synthetic selections
// (no faceId) are left as-is. Cheap: the set is FIFO-capped at 8.
inline void refreshSelections(SelectionState& st, entt::registry& reg) {
    for (std::size_t i = 0; i < st.selected.size(); ) {
        Selection& s = st.selected[i];
        if (!s.valid || !reg.valid(s.entity)) { st.selected.erase(st.selected.begin() + std::ptrdiff_t(i)); continue; }
        if (s.faceId >= 0) {
            Selection f = resolveFace(reg, s.entity, s.faceId);
            if (f.valid) {
                // rim-anchored features re-snap to the rim NEAREST the original click; features
                // without rims (planes: axisEnd0/1 both zero) KEEP the click point -- the old
                // unconditional rim snap parked plane anchors at the world origin.
                const bool hasRims = glm::dot(f.axisEnd0, f.axisEnd0)
                                   + glm::dot(f.axisEnd1, f.axisEnd1) > 1e-12f;
                const bool nearEnd0 = glm::distance(s.hitPoint, s.axisEnd0) <= glm::distance(s.hitPoint, s.axisEnd1);
                f.hitPoint = hasRims ? (nearEnd0 ? f.axisEnd0 : f.axisEnd1) : s.hitPoint;
                f.groupId = s.groupId;                   // the measure ITEM id survives the re-derivation
                s = f;
            }
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// RENDER GEOMETRY -- the SINGLE builder the SelectionHighlightPass draws AND the
// INDICATOR-GEOMETRY gate checks. The ring vertices ARE the backend indicator()
// points (no parallel hardcoded ring); the axis/normal arrow is derived from the
// analytic centre+normal. Because the gate asserts THIS function's output against
// the analytic Selection, "the rendered indicator == the true feature" is gated.
// ---------------------------------------------------------------------------
struct IndicatorLines {
    FeatureType type = FeatureType::None;
    std::vector<glm::vec3> ring;         // GL_LINES pairs: closed disk rim / plane outline
    std::vector<glm::vec3> arrow;        // GL_LINES pairs: shaft + 2 head barbs along the normal
    glm::vec3 diskCenter{ 0.0f };        // == IndicatorGeometry.center
    glm::vec3 diskNormal{ 0,0,1 };       // == cyl axis (cyl/cone) or surface normal (plane)
    float     diskRadius = 0.0f;         // == feature radius (cyl/cone)
};

inline IndicatorLines buildIndicatorLines(const IndicatorGeometry& g, float arrowLen = 0.0f) {
    IndicatorLines out;
    out.type = g.type;
    out.diskCenter = g.center;
    out.diskNormal = g.normal;
    out.diskRadius = g.radius;

    const std::size_t n = g.points.size();
    if (g.type == FeatureType::EdgeLine && n >= 2) {       // open segment, no loop close / no arrow
        out.ring.push_back(g.points[0]);
        out.ring.push_back(g.points[1]);
        return out;
    }
    if ((g.type == FeatureType::Cylinder || g.type == FeatureType::Cone
         || g.type == FeatureType::Plane || g.type == FeatureType::EdgeCircle) && n >= 2) {
        for (std::size_t i = 0; i < n; ++i) {            // close the loop: rim / outline
            out.ring.push_back(g.points[i]);
            out.ring.push_back(g.points[(i + 1) % n]);
        }
        // basis in the plane perpendicular to the normal, for the arrow head barbs.
        glm::vec3 u = glm::cross(g.normal, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(g.normal, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const float L = arrowLen > 0.0f ? arrowLen
                        : (g.radius > 1e-5f ? g.radius * 1.6f : 0.03f);
        const glm::vec3 nrm = glm::normalize(g.normal);
        const glm::vec3 tip = g.center + nrm * L;
        out.arrow.push_back(g.center); out.arrow.push_back(tip);     // shaft
        const float hb = L * 0.18f;
        out.arrow.push_back(tip); out.arrow.push_back(tip - nrm * hb * 1.6f + u * hb);
        out.arrow.push_back(tip); out.arrow.push_back(tip - nrm * hb * 1.6f - u * hb);
    } else if (n >= 1) {                                  // point feature: crosshair
        glm::vec3 u = glm::cross(g.normal, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(g.normal, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(g.normal, u));
        const float s = 0.01f;
        out.ring.push_back(g.center - u * s); out.ring.push_back(g.center + u * s);
        out.ring.push_back(g.center - v * s); out.ring.push_back(g.center + v * s);
    }
    return out;
}

// Analytic verdict used by the INDICATOR-GEOMETRY gate: does the BUILT render
// geometry match the TRUE feature's analytic params? (disk normal==axis/normal,
// radius==feature radius, centre on the axis line, every rim vertex at the radius
// in the cross-section plane). Returns ok + the worst residuals so the gate can
// PRINT a measured number and reject a corrupted indicator (real failing model).
struct IndicatorCheck {
    bool  ok = true;
    float axisAlign = 1.0f;   // |dot(diskNormal, axis/normal)|  (want ~1)
    float radErr = 0.0f;      // |diskRadius - feature radius|
    float centerOffAxis = 0.0f;
    float maxRimRadErr = 0.0f;
    float maxRimPlaneErr = 0.0f;
};

inline IndicatorCheck checkIndicator(const IndicatorLines& L, const Selection& sel, float tol) {
    IndicatorCheck c;
    if (sel.type == FeatureType::Cylinder || sel.type == FeatureType::Cone) {
        const glm::vec3 axis = glm::normalize(sel.axisDir);
        c.axisAlign = std::abs(glm::dot(glm::normalize(L.diskNormal), axis));
        c.radErr = std::abs(L.diskRadius - sel.radius);
        const glm::vec3 w = L.diskCenter - sel.axisPos;
        c.centerOffAxis = glm::length(w - glm::dot(w, axis) * axis);
        for (const auto& p : L.ring) {
            c.maxRimRadErr = std::max(c.maxRimRadErr, std::abs(glm::length(p - L.diskCenter) - sel.radius));
            c.maxRimPlaneErr = std::max(c.maxRimPlaneErr, std::abs(glm::dot(p - L.diskCenter, axis)));
        }
        c.ok = (c.axisAlign > 1.0f - tol) && (c.radErr < tol) && (c.centerOffAxis < tol)
               && (c.maxRimRadErr < tol) && (c.maxRimPlaneErr < tol);
    } else if (sel.type == FeatureType::Plane) {
        const glm::vec3 nrm = glm::normalize(sel.normal);
        c.axisAlign = std::abs(glm::dot(glm::normalize(L.diskNormal), nrm));
        for (const auto& p : L.ring)
            c.maxRimPlaneErr = std::max(c.maxRimPlaneErr, std::abs(glm::dot(p - L.diskCenter, nrm)));
        c.ok = (c.axisAlign > 1.0f - tol) && (c.maxRimPlaneErr < tol);
    } else {
        c.ok = !L.ring.empty();
    }
    return c;
}

// ---- SELECTION-HIGHLIGHTS gates (defined in SelectionHighlightGate.cpp) -----
// HIGHLIGHT-MATCHES : hover/selected identity == the ray-resolved feature; a
//                     neighbour/dominant-face highlight (real failing model) FAILS.
// INDICATOR-GEOMETRY: buildIndicatorLines() matches the analytic feature <tol;
//                     a wrong-feature / axis-radius-mismatched indicator FAILS.
// MULTI-SELECT      : selecting feature N resolves to N (incl. small-bore-on-large-
//                     part); the set accumulates; dominant-resolver & non-
//                     accumulating-commit neg-ctrls FAIL.
bool runHighlightMatchesGate();
bool runIndicatorGeometryGate();
bool runMultiSelectGate();

} // namespace krs::sel
