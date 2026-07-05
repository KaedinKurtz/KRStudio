// Measure.cpp -- see Measure.hpp. krs::measure: the measurement math behind the
// Measure tool (world-space diameter/length/area of a picked B-Rep feature, and
// feature-to-feature distance), plus the headless MEASURE gate.
#include "Measure.hpp"

#include "components.hpp"   // TransformComponent, RenderableMeshComponent, BRepFace(Component), Vertex
#include "Scene.hpp"        // gate registry host (same pattern as the other gates)

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
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
