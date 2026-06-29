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

enum class FeatureType { None = -1, Plane = 0, Cylinder = 1, Cone = 2, Sphere = 3, Other = 4 };

struct Selection {
    bool valid = false;                    // a B-Rep face was resolved
    entt::entity entity = entt::null;
    int faceId = -1;
    FeatureType type = FeatureType::None;
    glm::vec3 hitPoint{ 0.0f };            // ray-surface intersection (world)
    // analytic feature params in WORLD frame (from the B-Rep, never a mesh fit):
    glm::vec3 axisPos{ 0.0f };             // a point on the axis (cyl/cone) or centre (sphere)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f }; // unit axis direction (cyl/cone)
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };  // surface normal (plane)
    float radius = 0.0f;                   // metres
    glm::vec3 axisEnd0{ 0.0f };            // cylinder rim centres (world); both zero => no trimmed B-Rep
    glm::vec3 axisEnd1{ 0.0f };
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
    return a.valid && b.valid && a.entity == b.entity && a.faceId == b.faceId;
}

// Per-scene selection state held in registry.ctx() (the SceneProperties pattern).
// `hover` is what a HOVER ray currently resolves to (highlight preview); `selected`
// is the accumulating SET a CLICK commits (the multi-feature selection the robot-
// builder needs). Both store the EXACT krs::sel::pick result -- no re-derivation.
struct SelectionState {
    bool enabled = true;                 // feature-selection mode (View toggle)
    Selection hover;                     // current hovered feature (valid==false => none)
    std::vector<Selection> selected;     // committed set (accumulates across clicks)
};

// HOVER: resolve the ray and store it as the hovered feature (replaces prior hover).
// A miss stores an invalid hover (highlight disappears). The stored hover IS pick().
inline void updateHover(SelectionState& st, entt::registry& reg, const krs::pick::Ray& ray) {
    st.hover = pickPreferCylinder(reg, ray);   // bores win over the flat face around them
}

// CLICK / COMMIT: resolve the ray; on a hit ADD the feature to the selected SET
// (it ACCUMULATES -- a second pick does NOT clear the first). Re-picking an already-
// selected feature TOGGLES it off. A miss leaves the set untouched (does not clear).
// `additive=false` replaces the set with just this feature (single-select mode).
// Returns the resolved Selection (the exact pick result -- the highlight tracks THIS).
inline Selection commitSelection(SelectionState& st, entt::registry& reg,
                                 const krs::pick::Ray& ray, bool additive = true) {
    const Selection s = pickPreferCylinder(reg, ray);   // bores win over the flat face around them
    if (!s.valid) return s;                              // miss -> no change to the set
    for (std::size_t i = 0; i < st.selected.size(); ++i) {
        if (sameFeature(st.selected[i], s)) {            // toggle off if re-picked
            st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
            return s;
        }
    }
    if (!additive) st.selected.clear();
    st.selected.push_back(s);
    return s;
}

// X-RAY commit: like commitSelection but resolves the cycleIndex-th body the ray pierces
// (near->far via pickCycled), so an occluded bore behind a front component can be committed by
// clicking the same pixel repeatedly. Behaviour on the resolved feature is identical (accumulate /
// toggle / replace).
inline Selection commitSelectionCycled(SelectionState& st, entt::registry& reg,
                                       const krs::pick::Ray& ray, int cycleIndex, bool additive = true) {
    const Selection s = pickCycled(reg, ray, cycleIndex);
    if (!s.valid) return s;                              // miss / non-B-Rep at this depth -> no change
    for (std::size_t i = 0; i < st.selected.size(); ++i) {
        if (sameFeature(st.selected[i], s)) {            // toggle off if re-picked
            st.selected.erase(st.selected.begin() + std::ptrdiff_t(i));
            return s;
        }
    }
    if (!additive) st.selected.clear();
    st.selected.push_back(s);
    return s;
}

inline void clearSelection(SelectionState& st) { st.selected.clear(); }

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
    if ((g.type == FeatureType::Cylinder || g.type == FeatureType::Cone
         || g.type == FeatureType::Plane) && n >= 2) {
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
