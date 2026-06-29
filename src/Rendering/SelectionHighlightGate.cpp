// SelectionHighlightGate.cpp -- the gateable, inspectable-at-rest half of the
// SELECTION-HIGHLIGHTS sprint. The on-screen rendering is OPERATOR-VISUAL-CONFIRM
// (a headless runner cannot see a highlight); what IS gated here is the IDENTITY
// and GEOMETRY the renderer is driven from:
//
//   HIGHLIGHT-MATCHES        the stored hover/selected feature == the EXACT
//                            feature the ray resolved (krs::sel::pick). NEG-CTRL:
//                            an off-by-one neighbour / dominant-face highlight
//                            (a REAL failing model) tracks the WRONG feature.
//   INDICATOR-GEOMETRY       buildIndicatorLines() (the SAME function the pass
//                            draws) matches the analytic feature <tol. NEG-CTRL:
//                            an indicator built from the WRONG feature, or with
//                            axis/radius mismatched, FAILS the analytic check.
//   MULTI-SELECT             selecting feature N resolves to N specifically incl.
//                            the SMALL-BORE-ON-LARGE-PART hard case (bore, not the
//                            dominating box plane); the set ACCUMULATES. NEG-CTRL:
//                            a dominant-face resolver FAILS the bore case; a non-
//                            accumulating commit FAILS the accumulation check.
//
// Parts are built directly as ECS components (RenderableMeshComponent + triFace +
// BRepFaceComponent) -- OCCT-free, exactly as SelectionService.hpp consumes them --
// and driven through the REAL krs::sel::pick / krs::sel::indicator backend.

#include "SelectionService.hpp"
#include "RayPick.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <vector>
#include <random>
#include <cmath>

namespace krs::sel {
namespace {

constexpr float kPI2 = 6.28318530718f;

// ---- tiny part builder: triangles + per-triangle faceId + per-face B-Rep -----
struct Part {
    std::vector<Vertex>        verts;
    std::vector<unsigned int>  idx;
    std::vector<int>           triFace;
    std::vector<BRepFace>      faces;

    void tri(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, int f) {
        const unsigned base = unsigned(verts.size());
        Vertex va; va.position = a; Vertex vb; vb.position = b; Vertex vc; vc.position = c;
        verts.push_back(va); verts.push_back(vb); verts.push_back(vc);
        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
        triFace.push_back(f);
    }
    void quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, int f) {
        tri(a, b, c, f); tri(a, c, d, f);
    }

    // a cylindrical WALL (tube) along unit axis A, centre C, radius r, half-length h.
    int addCylWall(const glm::vec3& C, const glm::vec3& Ain, float r, float h, int N) {
        const glm::vec3 A = glm::normalize(Ain);
        glm::vec3 u = glm::cross(A, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(A, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(A, u));
        const int f = int(faces.size());
        BRepFace bf; bf.type = 1; bf.axisPos = C; bf.axisDir = A; bf.radius = r;
        faces.push_back(bf);
        const glm::vec3 top = C + A * h, bot = C - A * h;
        for (int i = 0; i < N; ++i) {
            const float a0 = kPI2 * float(i) / N, a1 = kPI2 * float(i + 1) / N;
            const glm::vec3 d0 = std::cos(a0) * u + std::sin(a0) * v;
            const glm::vec3 d1 = std::cos(a1) * u + std::sin(a1) * v;
            quad(bot + r * d0, bot + r * d1, top + r * d1, top + r * d0, f);
        }
        return f;
    }

    // an axis-aligned planar quad face with outward normal.
    int addPlaneQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                     const glm::vec3& d, const glm::vec3& normal) {
        const int f = int(faces.size());
        BRepFace bf; bf.type = 0; bf.normal = glm::normalize(normal);
        bf.axisPos = 0.25f * (a + b + c + d);
        faces.push_back(bf);
        quad(a, b, c, d, f);
        return f;
    }

    // a box centred at c with half-extents he: 6 planar faces (large, "dominating").
    void addBox(const glm::vec3& c, const glm::vec3& he) {
        const glm::vec3 p000 = c + glm::vec3(-he.x, -he.y, -he.z);
        const glm::vec3 p100 = c + glm::vec3(he.x, -he.y, -he.z);
        const glm::vec3 p110 = c + glm::vec3(he.x, he.y, -he.z);
        const glm::vec3 p010 = c + glm::vec3(-he.x, he.y, -he.z);
        const glm::vec3 p001 = c + glm::vec3(-he.x, -he.y, he.z);
        const glm::vec3 p101 = c + glm::vec3(he.x, -he.y, he.z);
        const glm::vec3 p111 = c + glm::vec3(he.x, he.y, he.z);
        const glm::vec3 p011 = c + glm::vec3(-he.x, he.y, he.z);
        addPlaneQuad(p100, p110, p111, p101, { 1, 0, 0 });   // +X
        addPlaneQuad(p000, p001, p011, p010, { -1, 0, 0 });  // -X
        addPlaneQuad(p010, p011, p111, p110, { 0, 1, 0 });   // +Y
        addPlaneQuad(p000, p100, p101, p001, { 0, -1, 0 });  // -Y
        addPlaneQuad(p001, p101, p111, p011, { 0, 0, 1 });   // +Z
        addPlaneQuad(p000, p010, p110, p100, { 0, 0, -1 });  // -Z
    }

    entt::entity emplace(entt::registry& reg, const glm::vec3& pos = glm::vec3(0.0f)) {
        const entt::entity e = reg.create();
        reg.emplace<TransformComponent>(e, pos, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& rm = reg.emplace<RenderableMeshComponent>(e);
        rm.vertices = verts; rm.indices = idx; rm.triFace = triFace;
        reg.emplace<BRepFaceComponent>(e).faces = faces;
        return e;
    }
};

// Build a Selection from a SPECIFIC faceId (the same world transform pick() applies).
// Used to construct the REAL failing models (neighbour / dominant face) honestly.
Selection selectionFromFace(entt::registry& reg, entt::entity ent, const glm::vec3& hitPoint, int faceId) {
    Selection s;
    if (!reg.all_of<RenderableMeshComponent, BRepFaceComponent>(ent)) return s;
    const auto& brep = reg.get<BRepFaceComponent>(ent);
    if (faceId < 0 || faceId >= int(brep.faces.size())) return s;
    const BRepFace& bf = brep.faces[faceId];
    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(ent)) M = xf->getTransform();
    const glm::mat3 R = glm::mat3(glm::inverseTranspose(M));
    s.valid = true; s.entity = ent; s.faceId = faceId; s.hitPoint = hitPoint;
    s.type = static_cast<FeatureType>(bf.type);
    s.radius = bf.radius;
    s.axisPos = glm::vec3(M * glm::vec4(bf.axisPos, 1.0f));
    s.axisDir = glm::normalize(R * bf.axisDir);
    s.normal = glm::normalize(R * bf.normal);
    return s;
}

// per-face surface area (groups triangle areas by triFace) -> argmax = dominant face.
int dominantFaceOf(const RenderableMeshComponent& mesh, std::size_t nFaces) {
    std::vector<double> area(nFaces, 0.0);
    for (std::size_t t = 0; t * 3 + 2 < mesh.indices.size() && t < mesh.triFace.size(); ++t) {
        const int f = mesh.triFace[t];
        if (f < 0 || f >= int(nFaces)) continue;
        const glm::vec3& a = mesh.vertices[mesh.indices[t * 3]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[t * 3 + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[t * 3 + 2]].position;
        area[f] += 0.5 * double(glm::length(glm::cross(b - a, c - a)));
    }
    int dom = -1; double best = -1.0;
    for (int f = 0; f < int(nFaces); ++f) if (area[f] > best) { best = area[f]; dom = f; }
    return dom;
}

// REAL failing model A: the highlight tracks the DOMINANT (largest-area) face of the
// hit body instead of the face the ray actually resolved.
Selection pickDominantFace(entt::registry& reg, const krs::pick::Ray& ray) {
    Selection s;
    const auto hit = krs::pick::pickMesh(reg, ray);
    if (!hit) return s;
    if (!reg.all_of<RenderableMeshComponent, BRepFaceComponent>(hit->entity)) return s;
    const auto& mesh = reg.get<RenderableMeshComponent>(hit->entity);
    const auto& brep = reg.get<BRepFaceComponent>(hit->entity);
    const int dom = dominantFaceOf(mesh, brep.faces.size());
    return selectionFromFace(reg, hit->entity, hit->worldPos, dom);
}

// REAL failing model B: an off-by-one neighbour highlight (faceId+1). A single-face
// body has NO neighbour, so the model is undefined there -> return invalid (never a
// false "match"); this keeps the neg-control honest if the scene ever changes.
Selection pickNeighbourFace(entt::registry& reg, const krs::pick::Ray& ray) {
    const Selection truth = pick(reg, ray);
    if (!truth.valid) return truth;
    const auto& brep = reg.get<BRepFaceComponent>(truth.entity);
    const int nFaces = int(brep.faces.size());
    if (nFaces < 2) { Selection s; return s; }              // no distinct neighbour exists
    const int neigh = (truth.faceId + 1) % nFaces;
    return selectionFromFace(reg, truth.entity, truth.hitPoint, neigh);
}

} // namespace

// ===========================================================================
bool runHighlightMatchesGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selhl] GATE HIGHLIGHT-MATCHES -- stored hover/selected feature == the ray-resolved feature\n");

    entt::registry reg;

    // a small "shop scene": two cylinder parts + a box, mixed features.
    { Part p; p.addCylWall({ 0,0,0 }, { 0,0,1 }, 0.20f, 0.5f, 40);
              p.addPlaneQuad({ -0.3f,-0.3f,0.6f }, { 0.3f,-0.3f,0.6f }, { 0.3f,0.3f,0.6f }, { -0.3f,0.3f,0.6f }, { 0,0,1 });
              p.emplace(reg, { -1.2f, 0, 0 }); }
    { Part p; p.addCylWall({ 0,0,0 }, { 0,1,0 }, 0.12f, 0.4f, 40);
              // an end cap (face 1) so the off-by-one NEG-CTRL is a genuinely different
              // feature -- a 1-face part would let (0+1)%1 wrap to itself (vacuous).
              p.addPlaneQuad({ 0.12f,0.4f,-0.12f }, { 0.12f,0.4f,0.12f },
                             { -0.12f,0.4f,0.12f }, { -0.12f,0.4f,-0.12f }, { 0,1,0 });
              p.emplace(reg, { 1.2f, 0, 0 }); }
    { Part p; p.addBox({ 0,0,0 }, { 0.5f,0.5f,0.5f });
              p.addCylWall({ 0,0,0 }, { 0,0,1 }, 0.05f, 0.6f, 32);   // a small bore through the box
              p.emplace(reg, { 0, -1.4f, 0 }); }

    SelectionState st;

    // FUZZ: many rays toward features across the parts; the stored identity must equal pick().
    std::mt19937 rng(20260619u);
    std::uniform_real_distribution<float> jit(-0.04f, 0.04f);
    int valid = 0, realMatch = 0, neighbourMatch = 0, dominantMatch = 0, misses = 0, missClean = 0;

    auto castAt = [&](const glm::vec3& origin, const glm::vec3& dir) {
        krs::pick::Ray ray; ray.origin = origin; ray.dir = glm::normalize(dir);
        const Selection truth = pickPreferCylinder(reg, ray);   // updateHover's resolver (bores win)
        // HOVER stores exactly the resolver result: the highlight tracks the TRUE resolved feature.
        updateHover(st, reg, ray);
        if (!truth.valid) {
            ++misses;
            if (!st.hover.valid) ++missClean;     // a miss => no highlight
            return;
        }
        ++valid;
        if (sameFeature(st.hover, truth) && st.hover.faceId == truth.faceId
            && st.hover.entity == truth.entity) ++realMatch;
        // REAL failing models: do they track the wrong feature?
        const Selection neigh = pickNeighbourFace(reg, ray);
        const Selection dom = pickDominantFace(reg, ray);
        if (neigh.valid && sameFeature(neigh, truth)) ++neighbourMatch;
        if (dom.valid && sameFeature(dom, truth)) ++dominantMatch;
    };

    // cylinder #1 wall (radial), its cap (axial), cylinder #2 wall, box small bore (radial), box faces.
    for (int i = 0; i < 60; ++i) {
        const float a = kPI2 * i / 60.0f;
        const glm::vec3 out(std::cos(a), std::sin(a), 0);
        castAt(glm::vec3(-1.2f, 0, 0) + glm::vec3(0, 0, jit(rng)), out);                    // cyl1 wall
        castAt(glm::vec3(1.2f, 0, 0) + glm::vec3(jit(rng), 0, jit(rng)),
               glm::vec3(std::cos(a), 0, std::sin(a)));                                      // cyl2 wall (axis Y)
        castAt(glm::vec3(0, -1.4f, 0) + glm::vec3(0, 0, jit(rng)), out);                    // box small bore wall
    }
    castAt({ -1.2f, 0, 2.0f }, { 0, 0, -1 });            // cyl1 top plane (axial)
    castAt({ 0, -1.4f, 2.0f }, { 0, 0, -1 });            // box +Z plane
    castAt({ 5, 5, 5 }, { 0, 0, 1 });                    // a guaranteed MISS

    const bool realPerfect = valid > 100 && realMatch == valid;
    const bool neighbourWorse = neighbourMatch == 0;                  // neighbour NEVER matches truth
    const bool dominantWorse = dominantMatch < valid;                // dominant face mismatches on non-dominant hits
    const bool missesClean = misses == 0 || missClean == misses;
    const bool pass = realPerfect && neighbourWorse && dominantWorse && missesClean;

    printf("[selhl]   stored-hover == pick(): %d/%d (100%% req) ; misses clean %d/%d  %s\n",
           realMatch, valid, missClean, misses, realPerfect && missesClean ? "PASS" : "FAIL");
    printf("[selhl]   NEG-CTRL neighbour-face highlight matches truth: %d/%d (must be 0)  %s\n",
           neighbourMatch, valid, neighbourWorse ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl]   NEG-CTRL dominant-face highlight matches truth: %d/%d (must be < %d)  %s\n",
           dominantMatch, valid, valid, dominantWorse ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl] %s\n", pass ? "ALL PASS (highlight tracks the true resolved feature; wrong-feature models rejected)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runIndicatorGeometryGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selhl] GATE INDICATOR-GEOMETRY -- rendered disk/arrow == the analytic feature (derived, not hardcoded)\n");

    entt::registry reg;
    std::mt19937 rng(424242u);
    std::uniform_real_distribution<float> rad(0.05f, 0.40f), pos(-0.5f, 0.5f), ax(-1.0f, 1.0f);

    const float tol = 1e-4f;
    int nCyl = 0, cylOk = 0, cylNegWrongFeat = 0, cylNegMismatch = 0;
    int nPlane = 0, planeOk = 0, planeNegTilt = 0;
    float worstErr = 0.0f;

    // FUZZ: random cylinder features + a plane reference; build the indicator the PASS
    // draws (buildIndicatorLines) and check it against the analytic Selection.
    for (int t = 0; t < 40; ++t) {
        glm::vec3 axis(ax(rng), ax(rng), ax(rng));
        if (glm::dot(axis, axis) < 0.05f) axis = glm::vec3(0, 0, 1);
        axis = glm::normalize(axis);
        const float r = rad(rng);
        const glm::vec3 C(pos(rng), pos(rng), pos(rng));

        Part p;
        const int cylF = p.addCylWall(C, axis, r, 0.5f, 48);
        // a perpendicular-ish plane feature, used as the WRONG-feature neg-ctrl.
        glm::vec3 pn = glm::cross(axis, glm::vec3(0, 1, 0));
        if (glm::dot(pn, pn) < 1e-4f) pn = glm::cross(axis, glm::vec3(1, 0, 0));
        pn = glm::normalize(pn);
        glm::vec3 u = glm::normalize(glm::cross(pn, axis));
        const int planeF = p.addPlaneQuad(C + u * 0.6f - axis * 0.3f, C + u * 0.6f + axis * 0.3f,
                                          C + u * 0.9f + axis * 0.3f, C + u * 0.9f - axis * 0.3f, pn);
        const entt::entity e = p.emplace(reg);
        (void)cylF; (void)planeF;

        // resolve the cylinder wall: ray from the axis outward along u.
        krs::pick::Ray ray; ray.origin = C; ray.dir = u;
        const Selection sel = pick(reg, ray);
        if (!sel.valid || sel.type != FeatureType::Cylinder) { reg.clear(); continue; }
        ++nCyl;

        // REAL indicator (what the pass renders) -> analytic check.
        const IndicatorGeometry g = indicator(sel);
        const IndicatorLines L = buildIndicatorLines(g);
        const IndicatorCheck chk = checkIndicator(L, sel, tol);
        if (chk.ok) ++cylOk;
        worstErr = std::max(worstErr, std::max(chk.radErr, std::max(chk.maxRimRadErr, chk.maxRimPlaneErr)));

        // NEG-CTRL 1: indicator built from the WRONG feature (the plane) -> fails vs the cylinder.
        const Selection wrongSel = selectionFromFace(reg, e, sel.hitPoint, planeF);
        const IndicatorLines Lwrong = buildIndicatorLines(indicator(wrongSel));
        if (!checkIndicator(Lwrong, sel, tol).ok) ++cylNegWrongFeat;

        // NEG-CTRL 2: axis/radius MISMATCH -- corrupt the analytic params, rebuild the disk.
        IndicatorGeometry gBad = g;
        gBad.radius *= 1.3f;                                  // wrong radius
        const glm::quat tilt = glm::angleAxis(0.26f, u);      // ~15 deg axis tilt
        gBad.normal = glm::normalize(tilt * g.normal);
        // rebuild the rim at the corrupted radius/axis so it is a self-consistent WRONG disk.
        gBad.points.clear();
        glm::vec3 bu = glm::cross(gBad.normal, glm::vec3(0, 0, 1));
        if (glm::dot(bu, bu) < 1e-8f) bu = glm::cross(gBad.normal, glm::vec3(0, 1, 0));
        bu = glm::normalize(bu);
        const glm::vec3 bv = glm::normalize(glm::cross(gBad.normal, bu));
        for (int i = 0; i < 32; ++i) {
            const float aa = kPI2 * i / 32.0f;
            gBad.points.push_back(gBad.center + gBad.radius * (std::cos(aa) * bu + std::sin(aa) * bv));
        }
        const IndicatorLines Lbad = buildIndicatorLines(gBad);
        if (!checkIndicator(Lbad, sel, tol).ok) ++cylNegMismatch;

        // POSITIVE PLANE case: the pass ALSO renders plane indicators (normal arrow +
        // outline) -- gate that render branch too, not just the cylinder one.
        const glm::vec3 planeCenter = C + u * 0.75f;           // a point on the plane quad
        const Selection planeSel = selectionFromFace(reg, e, planeCenter, planeF);
        if (planeSel.valid && planeSel.type == FeatureType::Plane) {
            ++nPlane;
            const IndicatorLines Lp = buildIndicatorLines(indicator(planeSel));
            if (checkIndicator(Lp, planeSel, tol).ok) ++planeOk;
            worstErr = std::max(worstErr, checkIndicator(Lp, planeSel, tol).maxRimPlaneErr);
            // NEG-CTRL: a plane indicator whose normal is tilted off the face normal FAILS.
            IndicatorGeometry gp = indicator(planeSel);
            gp.normal = glm::normalize(glm::angleAxis(0.26f, u) * gp.normal);  // ~15 deg tilt
            if (!checkIndicator(buildIndicatorLines(gp), planeSel, tol).ok) ++planeNegTilt;
        }

        reg.clear();
    }

    const bool realOk = nCyl > 20 && cylOk == nCyl;
    const bool negWrongFeat = cylNegWrongFeat == nCyl;
    const bool negMismatch = cylNegMismatch == nCyl;
    const bool planeRealOk = nPlane > 20 && planeOk == nPlane;
    const bool planeNeg = planeNegTilt == nPlane;
    const bool pass = realOk && negWrongFeat && negMismatch && planeRealOk && planeNeg;

    printf("[selhl]   REAL cylinder indicator matches analytic feature: %d/%d (<%.0e) ; worst residual=%.3e  %s\n",
           cylOk, nCyl, tol, worstErr, realOk ? "PASS" : "FAIL");
    printf("[selhl]   NEG-CTRL wrong-feature indicator fails: %d/%d (must be all)  %s\n",
           cylNegWrongFeat, nCyl, negWrongFeat ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl]   NEG-CTRL axis/radius-mismatch indicator fails: %d/%d (must be all)  %s\n",
           cylNegMismatch, nCyl, negMismatch ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl]   REAL plane indicator (normal arrow + outline) matches face: %d/%d  %s\n",
           planeOk, nPlane, planeRealOk ? "PASS" : "FAIL");
    printf("[selhl]   NEG-CTRL tilted-normal plane indicator fails: %d/%d (must be all)  %s\n",
           planeNegTilt, nPlane, planeNeg ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl] %s\n", pass ? "ALL PASS (rendered cylinder+plane indicators derived from & match the true analytic feature)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runMultiSelectGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[selhl] GATE MULTI-SELECT -- pick feature N (incl. small bore on a big part); the set accumulates\n");

    entt::registry reg;

    // SUBFEAT-HARD: a big box (six large planar faces, the "dominating" geometry)
    // with THREE small bores (r=0.05) -- 1/20 of the 1.0 box -- through it along Z.
    Part p;
    p.addBox({ 0,0,0 }, { 0.5f, 0.5f, 0.5f });               // faces 0..5 (planes, area ~1.0 each)
    struct Bore { glm::vec2 xy; float r; int face; };
    std::vector<Bore> bores = { { {0.0f,0.0f}, 0.05f, -1 },
                                { {0.25f,0.10f}, 0.05f, -1 },
                                { {-0.20f,0.20f}, 0.07f, -1 } };
    for (auto& b : bores)
        b.face = p.addCylWall({ b.xy.x, b.xy.y, 0.0f }, { 0,0,1 }, b.r, 0.6f, 32);
    const entt::entity ent = p.emplace(reg);

    const auto& mesh = reg.get<RenderableMeshComponent>(ent);
    const auto& brep = reg.get<BRepFaceComponent>(ent);
    const int domFace = dominantFaceOf(mesh, brep.faces.size());
    const bool domIsPlane = domFace >= 0 && brep.faces[domFace].type == 0;   // a box plane dominates

    // (1) each small bore radial ray must resolve to THAT bore (small Cylinder), not a plane.
    int boresResolved = 0, dominantWouldFail = 0;
    for (const auto& b : bores) {
        krs::pick::Ray ray; ray.origin = { b.xy.x, b.xy.y, 0.0f }; ray.dir = { 1, 0, 0 };
        const Selection s = pick(reg, ray);
        const bool ok = s.valid && s.type == FeatureType::Cylinder && s.faceId == b.face
                        && std::abs(s.radius - b.r) < 1e-4f;
        if (ok) ++boresResolved;
        // NEG-CTRL: the dominant-face resolver returns the box plane here -> NOT the bore.
        const Selection dom = pickDominantFace(reg, ray);
        if (!(dom.valid && dom.type == FeatureType::Cylinder && dom.faceId == b.face)) ++dominantWouldFail;
    }
    const bool hardOk = boresResolved == int(bores.size());
    const bool dominantNeg = dominantWouldFail == int(bores.size());

    // (2) the selected SET accumulates: pick bore A, then bore B -> both present, A not cleared.
    SelectionState st;
    krs::pick::Ray rA; rA.origin = { bores[0].xy.x, bores[0].xy.y, 0 }; rA.dir = { 1, 0, 0 };
    krs::pick::Ray rB; rB.origin = { bores[1].xy.x, bores[1].xy.y, 0 }; rB.dir = { 1, 0, 0 };
    const Selection sA = commitSelection(st, reg, rA);
    const Selection sB = commitSelection(st, reg, rB);
    const bool accumulates = st.selected.size() == 2
        && sameFeature(st.selected[0], sA) && sameFeature(st.selected[1], sB)
        && sA.faceId != sB.faceId;
    // re-picking A toggles it off -> set holds only B.
    commitSelection(st, reg, rA);
    const bool toggleOk = st.selected.size() == 1 && sameFeature(st.selected[0], sB);
    // a MISS does not clear the set.
    krs::pick::Ray rMiss; rMiss.origin = { 5, 5, 5 }; rMiss.dir = { 0, 0, 1 };
    commitSelection(st, reg, rMiss);
    const bool missKeepsSet = st.selected.size() == 1;

    // NEG-CTRL: a NON-ACCUMULATING commit (clears every click) cannot hold two features.
    SelectionState stNA;
    auto commitNonAccum = [&](const krs::pick::Ray& ray) {
        const Selection s = pick(reg, ray);
        if (!s.valid) return; stNA.selected.clear(); stNA.selected.push_back(s);   // the BUG
    };
    commitNonAccum(rA); commitNonAccum(rB);
    const bool nonAccumFails = stNA.selected.size() == 1;     // only B survived -> FAILS "accumulates"

    const bool pass = domIsPlane && hardOk && dominantNeg && accumulates && toggleOk && missKeepsSet && nonAccumFails;

    printf("[selhl]   small-bore-on-large-part: %d/%d bores resolve to the SMALL bore (dominant face=%d is a %s)  %s\n",
           boresResolved, int(bores.size()), domFace, domIsPlane ? "PLANE" : "non-plane",
           hardOk && domIsPlane ? "PASS" : "FAIL");
    printf("[selhl]   NEG-CTRL dominant-face resolver misses the bore: %d/%d  %s\n",
           dominantWouldFail, int(bores.size()), dominantNeg ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl]   set accumulates (A then B -> 2; re-A toggles -> 1; miss keeps): %s/%s/%s  %s\n",
           accumulates ? "ok" : "BAD", toggleOk ? "ok" : "BAD", missKeepsSet ? "ok" : "BAD",
           accumulates && toggleOk && missKeepsSet ? "PASS" : "FAIL");
    printf("[selhl]   NEG-CTRL non-accumulating commit holds only 1 of 2: %s  %s\n",
           nonAccumFails ? "yes" : "no", nonAccumFails ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[selhl] %s\n", pass ? "ALL PASS (small bore disambiguated; selection set accumulates; neg-ctrls reject)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::sel
