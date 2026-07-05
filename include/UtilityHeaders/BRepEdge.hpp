#pragma once
// ===========================================================================
// B-REP EDGES -- true edge features from OCCT (constraints + measure sprint).
//
// CadImporter walks each solid's TopoDS edges and stores, per body:
//   - the edge's ANALYTIC identity (circle: centre/axis/radius; line: the two
//     endpoints; other curves fall back to a polyline-only record), body-LOCAL
//     like BRepFace (world params derive through the entity transform), and
//   - the tessellated POLYLINE the picker tests rays against (screen-space
//     nearest-point threshold -- edges are infinitely thin, so picking works
//     on proximity, not intersection).
//
// edgeKey mirrors computeFaceKey: a geometry-invariant FNV-1a hash so a
// constraint anchored to an edge re-anchors across re-import/re-tessellation
// (the topological-naming mitigation, same contract as faceKey).
// ===========================================================================
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

struct BRepEdge {
    enum Kind { Line = 0, Circle = 1, Other = 2 };
    int  kind = Other;
    // analytic params, BODY-LOCAL (transform to world through the entity M):
    glm::vec3 center{ 0.0f };              // circle centre (arc centre for partial arcs)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f }; // circle plane normal / line direction (unit)
    float     radius = 0.0f;               // circle radius (metres); 0 for lines
    glm::vec3 p0{ 0.0f }, p1{ 0.0f };      // line endpoints / arc endpoints (equal on a full circle)
    bool      closed = false;              // full circle (p0==p1) vs arc
    // the two B-Rep FACES this edge bounds (indices into BRepFaceComponent.faces; -1 = none/unknown)
    int faceA = -1, faceB = -1;
    // tessellated polyline for PICKING (body-local; >= 2 points; circles ~32 segments)
    std::vector<glm::vec3> polyline;
    // STABLE geometry-invariant id (see computeEdgeKey). 0 = unset/synthetic.
    std::uint64_t edgeKey = 0;
};
struct BRepEdgeComponent { std::vector<BRepEdge> edges; };

// Geometry-invariant edge key (FNV-1a-64), mirroring computeFaceKey's contract: the SAME physical
// edge hashes identically across re-import; distinct same-shape edges are separated by their
// body-local POSITION channel. Input MUST be body-local.
inline std::uint64_t computeEdgeKey(const BRepEdge& e) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::int64_t v) {
        for (int b = 0; b < 8; ++b) { h ^= std::uint64_t((v >> (b * 8)) & 0xff); h *= 1099511628211ull; }
    };
    mix(e.kind);
    glm::vec3 d = e.axisDir;
    const float L = glm::length(d); d = (L > 1e-9f) ? d / L : glm::vec3(0, 0, 1);
    const float pick = (std::abs(d.z) > 1e-6f) ? d.z : (std::abs(d.y) > 1e-6f ? d.y : d.x);
    if (pick < 0.0f) d = -d;                                     // hemisphere fold (reversed twin keys equal)
    mix(std::llround(d.x / 1e-3)); mix(std::llround(d.y / 1e-3)); mix(std::llround(d.z / 1e-3));
    mix(std::llround(double(e.radius) / 1e-4));
    // position channel: circle centre, or the fold-safe line midpoint (0.1 mm quantized)
    const glm::vec3 anchor = (e.kind == BRepEdge::Circle) ? e.center : 0.5f * (e.p0 + e.p1);
    mix(std::llround(double(anchor.x) / 1e-4));
    mix(std::llround(double(anchor.y) / 1e-4));
    mix(std::llround(double(anchor.z) / 1e-4));
    if (e.kind == BRepEdge::Line)                                 // length separates collinear segments
        mix(std::llround(double(glm::length(e.p1 - e.p0)) / 1e-4));
    return h ? h : 1;                                             // never 0 (0 = unset sentinel)
}
