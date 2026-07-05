#pragma once
// ===========================================================================
// B-REP VERTICES -- true model vertices from OCCT (mate-selector sprint P3).
//
// CadImporter walks each solid's TopoDS vertices and stores, per body, the
// BODY-LOCAL corner points (same storage frame as BRepFace/BRepEdge) plus the
// adjacency needed by the SnapEngine (which faces/edges meet here -- a
// vertex's snap FRAME derives from the face it was hovered through, per the
// Onshape spec). vertexKey mirrors computeFaceKey/computeEdgeKey: a geometry-
// invariant hash so a connector anchored to a corner re-anchors across
// re-import (position channel only -- a corner IS its position).
// ===========================================================================
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

struct BRepVertex {
    glm::vec3 pos{ 0.0f };                 // BODY-LOCAL corner position
    // adjacency (indices into BRepFaceComponent.faces / BRepEdgeComponent.edges; may be empty
    // when the owner face/edge was skipped at import -- consumers must tolerate that honestly)
    std::vector<int> faces;
    std::vector<int> edges;
    std::uint64_t vertexKey = 0;           // see computeVertexKey. 0 = unset/synthetic.
};
struct BRepVertexComponent { std::vector<BRepVertex> verts; };

// Geometry-invariant vertex key (FNV-1a-64 over the 0.1 mm-quantized body-local position).
inline std::uint64_t computeVertexKey(const BRepVertex& v) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::int64_t x) {
        for (int b = 0; b < 8; ++b) { h ^= std::uint64_t((x >> (b * 8)) & 0xff); h *= 1099511628211ull; }
    };
    mix(std::llround(double(v.pos.x) / 1e-4));
    mix(std::llround(double(v.pos.y) / 1e-4));
    mix(std::llround(double(v.pos.z) / 1e-4));
    return h ? h : 1;                       // never 0 (0 = unset sentinel)
}
