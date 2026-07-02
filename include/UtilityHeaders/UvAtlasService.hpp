#pragma once
// UvAtlasService -- xatlas-backed per-body UV unwrap + atlas packing for face texturing (Phase C).
// xatlas (MIT) is confined to UvAtlasService.cpp; this header exposes a plain API.
#include <vector>
#include <cstdint>

namespace krs::uv {

struct UvResult {
    bool ok = false;
    std::vector<float>         positions;  // xyz per OUTPUT vertex (scattered from xref)
    std::vector<float>         uvs;        // uv per OUTPUT vertex, NORMALIZED to [0,1]
    std::vector<std::uint32_t> indices;    // OUTPUT indices; triangle count/order == input
    std::vector<std::uint32_t> xref;       // OUTPUT vertex -> INPUT vertex index
    int atlasWidth = 0, atlasHeight = 0;
    int chartCount = 0;
};

// Unwrap a triangle mesh (flat float xyz positions + uint32 triangle indices) into a UV atlas.
// xatlas splits vertices at chart seams (output vertexCount >= input) but PRESERVES triangle count AND
// ORDER, so a parallel per-triangle map (RenderableMeshComponent::triFace -> BRepFace) stays valid 1:1.
// texelsPerUnit > 0 requests a consistent world-scale texel density; <= 0 lets xatlas auto-size.
UvResult unwrapMesh(const std::vector<float>& positions, const std::vector<std::uint32_t>& indices,
                    float texelsPerUnit = 0.0f);

// GATE (KRS_XATLAS_SELFTEST): unwrap preserves triangle topology (indexCount, so triFace stays valid), UVs land
// in [0,1], xref is valid, charts are produced, and it is deterministic. Non-vacuous neg-ctrl. In the .cpp.
bool runUvAtlasGate();

} // namespace krs::uv
