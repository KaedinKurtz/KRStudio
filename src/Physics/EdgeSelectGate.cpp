// EdgeSelectGate.cpp -- the EDGE-SELECT gate (krs::sel edges, EdgeSelect.hpp):
// resolveEdge world params under scale+translation, ray-proximity pickEdge over
// the polyline segments, depth tie-break, edgeKey stability, real NEG-CTRLs.
#include "EdgeSelect.hpp"

#include "components.hpp"   // TransformComponent, RenderableMeshComponent
#include "Scene.hpp"        // gate registry host (same pattern as the other gates)

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdio>

namespace krs::sel {

namespace {

struct ESuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[edge]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

// Synthetic circle edge: r, centre, +Z normal, closed, 32-segment polyline (the
// import contract: >=2 pts, circles ~32 segments, last point closes the loop).
BRepEdge makeCircle(const glm::vec3& c, float r) {
    BRepEdge e;
    e.kind = BRepEdge::Circle;
    e.center = c; e.axisDir = { 0, 0, 1 }; e.radius = r; e.closed = true;
    e.p0 = e.p1 = c + glm::vec3(r, 0.0f, 0.0f);
    for (int i = 0; i <= 32; ++i) {
        const float a = 6.28318530717958647692f * float(i) / 32.0f;
        e.polyline.push_back(c + r * glm::vec3(std::cos(a), std::sin(a), 0.0f));
    }
    e.polyline.back() = e.polyline.front();                  // close exactly
    e.edgeKey = computeEdgeKey(e);
    return e;
}

BRepEdge makeLine(const glm::vec3& p0, const glm::vec3& p1) {
    BRepEdge e;
    e.kind = BRepEdge::Line;
    e.p0 = p0; e.p1 = p1;
    e.axisDir = glm::normalize(p1 - p0);
    e.polyline = { p0, p1 };
    e.edgeKey = computeEdgeKey(e);
    return e;
}

} // namespace

bool runEdgeSelectGate() {
    std::printf("[edge] ================ EDGE-SELECT GATE (krs::sel edges) ================\n");
    ESuite S;
    Scene scene;
    auto& reg = scene.getRegistry();

    // ---- (1) UNIT-CIRCLE edge r=0.25 at local (1,0,0), normal +Z, on an entity scaled
    //          (2,2,2) + translated (0.3,-0.2,0.5): resolveEdge -> world centre EXACT at
    //          T + 2*(1,0,0), radius EXACT 0.5, axisDir +Z. ---------------------------
    const glm::vec3 T(0.3f, -0.2f, 0.5f);
    auto eCirc = reg.create();
    reg.emplace<TransformComponent>(eCirc, T, glm::quat(1, 0, 0, 0), glm::vec3(2.0f));
    reg.emplace<BRepEdgeComponent>(eCirc).edges.push_back(makeCircle({ 1, 0, 0 }, 0.25f));

    const Selection rc = resolveEdge(reg, eCirc, 0);
    const glm::vec3 wantC = T + glm::vec3(2.0f, 0.0f, 0.0f);
    std::printf("[edge] circle resolve: valid=%d type=%d centre=(%.6f, %.6f, %.6f) (want %.6f, %.6f, %.6f)"
                " r=%.9f (want 0.500000000) axisDir=(%.4f, %.4f, %.4f)\n",
                int(rc.valid), int(rc.type),
                double(rc.axisPos.x), double(rc.axisPos.y), double(rc.axisPos.z),
                double(wantC.x), double(wantC.y), double(wantC.z),
                double(rc.radius), double(rc.axisDir.x), double(rc.axisDir.y), double(rc.axisDir.z));
    S.check("RESOLVE-CENTRE    scaled(2)+translated circle -> world centre exact",
            rc.valid && rc.type == FeatureType::EdgeCircle && glm::length(rc.axisPos - wantC) < 1e-6f);
    S.check("RESOLVE-RADIUS    r=0.25 under uniform scale 2 -> world radius == 0.500 exact",
            rc.valid && std::abs(rc.radius - 0.5f) < 1e-6f);
    S.check("RESOLVE-AXISDIR   circle plane normal stays +Z through the inverse-transpose",
            rc.valid && glm::dot(rc.axisDir, glm::vec3(0, 0, 1)) > 1.0f - 1e-6f);
    S.check("RESOLVE-CLOSED    closed circle -> arc endpoints axisEnd0 == axisEnd1",
            rc.valid && glm::length(rc.axisEnd0 - rc.axisEnd1) < 1e-6f
                     && std::abs(glm::length(rc.axisEnd0 - rc.axisPos) - 0.5f) < 1e-5f);
    S.check("RESOLVE-EDGEKEY   Selection.faceKey carries the edgeKey (non-zero)",
            rc.valid && rc.faceKey != 0 && rc.faceKey == reg.get<BRepEdgeComponent>(eCirc).edges[0].edgeKey);

    // ---- (2) pickEdge 1 mm off the rim (through a polyline VERTEX azimuth) -> HIT with the
    //          right edgeId + hitPoint ON the rim (<1e-3); 10 cm away w/ tol 1 cm -> MISS. --
    const glm::vec3 rim = T + glm::vec3(2.5f, 0.0f, 0.0f);    // world rim vertex at angle 0
    {
        krs::pick::Ray ray;
        ray.origin = { rim.x + 0.001f, rim.y, 5.0f };
        ray.dir = { 0, 0, -1 };
        const Selection s = pickEdge(reg, ray, 0.005f);
        const float rimErr = s.valid ? std::abs(glm::length(s.hitPoint - rc.axisPos) - 0.5f) : -1.0f;
        std::printf("[edge] rim pick (1 mm off): valid=%d type=%d edgeId=%d hit=(%.6f, %.6f, %.6f)"
                    " |hit-centre|-r=%.3e (tolerance 1e-3)\n",
                    int(s.valid), int(s.type), s.edgeId,
                    double(s.hitPoint.x), double(s.hitPoint.y), double(s.hitPoint.z), double(rimErr));
        S.check("PICK-RIM-1MM      ray 1 mm from the rim (tol 5 mm) -> EdgeCircle, edgeId 0, hitPoint on the rim",
                s.valid && s.type == FeatureType::EdgeCircle && s.entity == eCirc && s.edgeId == 0
                        && rimErr >= 0.0f && rimErr < 1e-3f && glm::length(s.hitPoint - rim) < 1e-3f);
    }
    {
        krs::pick::Ray ray;
        ray.origin = { rim.x + 0.10f, rim.y, 5.0f };
        ray.dir = { 0, 0, -1 };
        const Selection s = pickEdge(reg, ray, 0.01f);
        std::printf("[edge] far pick (10 cm off, tol 1 cm): valid=%d (want 0)\n", int(s.valid));
        S.check("PICK-MISS-10CM    ray 10 cm from the rim with tol 1 cm -> miss", !s.valid);
    }

    // ---- (3) LINE edge (0,0,0)-(0,0,0.1) scaled(2)+translated: resolve endpoints EXACT,
    //          and a crossing ray picks it with hitPoint on the segment. -----------------
    const glm::vec3 TL(-1.0f, 0.4f, 0.0f);
    auto eLine = reg.create();
    reg.emplace<TransformComponent>(eLine, TL, glm::quat(1, 0, 0, 0), glm::vec3(2.0f));
    reg.emplace<BRepEdgeComponent>(eLine).edges.push_back(makeLine({ 0, 0, 0 }, { 0, 0, 0.1f }));

    const Selection rl = resolveEdge(reg, eLine, 0);
    const glm::vec3 wantE0 = TL, wantE1 = TL + glm::vec3(0, 0, 0.2f);
    std::printf("[edge] line resolve: valid=%d type=%d e0=(%.6f, %.6f, %.6f) e1=(%.6f, %.6f, %.6f)"
                " (want %.1f,%.1f,%.1f .. %.1f,%.1f,%.2f) mid=(%.6f, %.6f, %.6f)\n",
                int(rl.valid), int(rl.type),
                double(rl.axisEnd0.x), double(rl.axisEnd0.y), double(rl.axisEnd0.z),
                double(rl.axisEnd1.x), double(rl.axisEnd1.y), double(rl.axisEnd1.z),
                double(wantE0.x), double(wantE0.y), double(wantE0.z),
                double(wantE1.x), double(wantE1.y), double(wantE1.z),
                double(rl.axisPos.x), double(rl.axisPos.y), double(rl.axisPos.z));
    S.check("RESOLVE-LINE      endpoints exact under scale(2)+translate; axisPos = midpoint; axisDir +Z",
            rl.valid && rl.type == FeatureType::EdgeLine
                     && glm::length(rl.axisEnd0 - wantE0) < 1e-6f
                     && glm::length(rl.axisEnd1 - wantE1) < 1e-6f
                     && glm::length(rl.axisPos - 0.5f * (wantE0 + wantE1)) < 1e-6f
                     && glm::dot(rl.axisDir, glm::vec3(0, 0, 1)) > 1.0f - 1e-6f);
    {
        krs::pick::Ray ray;
        ray.origin = { 5.0f, 0.4f, 0.1f };                  // crosses the world segment at its midpoint
        ray.dir = { -1, 0, 0 };
        const Selection s = pickEdge(reg, ray, 0.005f);
        const glm::vec3 wantHit(-1.0f, 0.4f, 0.1f);
        std::printf("[edge] line pick: valid=%d type=%d edgeId=%d hit=(%.6f, %.6f, %.6f) (want -1, 0.4, 0.1)\n",
                    int(s.valid), int(s.type), s.edgeId,
                    double(s.hitPoint.x), double(s.hitPoint.y), double(s.hitPoint.z));
        S.check("PICK-LINE         crossing ray -> EdgeLine, edgeId 0, hitPoint on the segment",
                s.valid && s.type == FeatureType::EdgeLine && s.entity == eLine && s.edgeId == 0
                        && glm::length(s.hitPoint - wantHit) < 1e-4f);
    }

    // ---- (4) DEPTH: two entities' edges overlapping in screen space (both dist ~0 to the
    //          same ray) -> the NEARER one along the ray wins (the tie-break). -----------
    auto eFar = reg.create();
    reg.emplace<TransformComponent>(eFar, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    reg.emplace<BRepEdgeComponent>(eFar).edges.push_back(
        makeLine({ 100.0f, -0.5f, 1.0f }, { 100.0f, 0.5f, 1.0f }));
    auto eNear = reg.create();
    reg.emplace<TransformComponent>(eNear, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    reg.emplace<BRepEdgeComponent>(eNear).edges.push_back(
        makeLine({ 100.0f, -0.5f, 3.0f }, { 100.0f, 0.5f, 3.0f }));
    {
        krs::pick::Ray ray;
        ray.origin = { 100.0f, 0.0f, 10.0f };               // pierces BOTH segments (dist 0 each)
        ray.dir = { 0, 0, -1 };
        const Selection s = pickEdge(reg, ray, 0.005f);
        std::printf("[edge] depth pick: valid=%d hit z=%.6f (want 3.0 -- the NEARER edge along the ray)\n",
                    int(s.valid), double(s.hitPoint.z));
        S.check("PICK-NEAREST      two edges on the same pixel -> the nearer ray-t (z=3) wins over z=1",
                s.valid && s.entity == eNear && std::abs(s.hitPoint.z - 3.0f) < 1e-4f);
    }

    // ---- (5) pickPreferEdge: the rim ray resolves the EDGE; a ray near nothing falls back
    //          to pickPreferCylinder (no meshes here -> honest invalid, no crash). --------
    {
        krs::pick::Ray ray;
        ray.origin = { rim.x + 0.001f, rim.y, 5.0f };
        ray.dir = { 0, 0, -1 };
        const Selection s = pickPreferEdge(reg, ray, 0.005f);
        S.check("PREFER-EDGE       pickPreferEdge on the rim ray -> the EdgeCircle (no fallback)",
                s.valid && s.type == FeatureType::EdgeCircle && s.entity == eCirc);
        krs::pick::Ray far; far.origin = { 500.0f, 500.0f, 5.0f }; far.dir = { 0, 0, -1 };
        const Selection f = pickPreferEdge(reg, far, 0.005f);
        S.check("PREFER-FALLBACK   no edge in reach -> falls through to the face pick (no faces -> invalid)",
                !f.valid);
    }

    // ---- (6) edgeKey: stable across two extractions of identical geometry; DIFFERENT for
    //          a translated twin (the position channel); never 0. -------------------------
    {
        const BRepEdge c1 = makeCircle({ 1, 0, 0 }, 0.25f);
        const BRepEdge c2 = makeCircle({ 1, 0, 0 }, 0.25f);              // re-extraction, same geometry
        const BRepEdge cT = makeCircle({ 1.01f, 0, 0 }, 0.25f);          // translated twin
        const BRepEdge l1 = makeLine({ 0, 0, 0 }, { 0, 0, 0.1f });
        const BRepEdge l2 = makeLine({ 0, 0, 0 }, { 0, 0, 0.1f });
        const BRepEdge lT = makeLine({ 0.01f, 0, 0 }, { 0.01f, 0, 0.1f }); // translated twin
        std::printf("[edge] edgeKey: circle=%llx re-extract=%llx translated=%llx | line=%llx re=%llx trans=%llx\n",
                    (unsigned long long)c1.edgeKey, (unsigned long long)c2.edgeKey, (unsigned long long)cT.edgeKey,
                    (unsigned long long)l1.edgeKey, (unsigned long long)l2.edgeKey, (unsigned long long)lT.edgeKey);
        S.check("KEY-STABLE        identical geometry re-extracted -> identical edgeKey (circle + line)",
                c1.edgeKey == c2.edgeKey && l1.edgeKey == l2.edgeKey);
        S.check("KEY-POSITION      translated twin -> DIFFERENT edgeKey (position channel separates it)",
                cT.edgeKey != c1.edgeKey && lT.edgeKey != l1.edgeKey);
        S.check("KEY-NONZERO       edgeKey is never 0 (0 = unset sentinel)",
                c1.edgeKey != 0 && cT.edgeKey != 0 && l1.edgeKey != 0 && lT.edgeKey != 0);
    }

    // ---- NEG-CTRLs: bad inputs return invalid / miss and never crash. -------------------
    const Selection n1 = resolveEdge(reg, eCirc, 5);
    S.check("NEG-CTRL          edgeId out of range (5 of 1) -> invalid, no crash", !n1.valid);
    const Selection n2 = resolveEdge(reg, eCirc, -1);
    S.check("NEG-CTRL          negative edgeId -> invalid", !n2.valid);
    auto eBare = reg.create();
    reg.emplace<TransformComponent>(eBare, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    const Selection n3 = resolveEdge(reg, eBare, 0);
    S.check("NEG-CTRL          entity WITHOUT BRepEdgeComponent -> invalid", !n3.valid);
    {
        krs::pick::Ray ray;
        ray.origin = { rim.x + 0.001f, rim.y, 5.0f };
        ray.dir = { 0, 0, 1 };                              // points AWAY: every edge is behind the origin
        const Selection s = pickEdge(reg, ray, 0.005f);
        S.check("NEG-CTRL          ray pointing away (all hits behind origin) -> miss", !s.valid);
    }

    const bool pass = (S.pass == S.total);
    std::printf("[edge] %d/%d checks\n", S.pass, S.total);
    std::printf("[edge] %s\n", pass
        ? "ALL PASS (circle centre/radius/axis exact under scale+translate; 1 mm rim pick + 10 cm miss;"
          " line endpoints exact; nearer edge wins the pixel; edgeKey stable/position-separated/non-zero;"
          " bad-id/no-component/behind-origin neg-ctrls)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::sel
