// UvAtlasService.cpp -- xatlas (MIT) per-body UV unwrap / atlas packing, confined to this TU.
#include "UvAtlasService.hpp"

#include <xatlas.h>

#include <cstdio>
#include <cmath>

namespace krs::uv {

UvResult unwrapMesh(const std::vector<float>& positions, const std::vector<std::uint32_t>& indices,
                    float texelsPerUnit)
{
    UvResult out;
    if (positions.size() < 9 || indices.size() < 3 || (indices.size() % 3) != 0) return out;

    xatlas::Atlas* atlas = xatlas::Create();
    xatlas::MeshDecl decl;
    decl.vertexCount          = std::uint32_t(positions.size() / 3);
    decl.vertexPositionData   = positions.data();
    decl.vertexPositionStride = sizeof(float) * 3;
    decl.indexCount           = std::uint32_t(indices.size());
    decl.indexData            = indices.data();
    decl.indexFormat          = xatlas::IndexFormat::UInt32;

    if (xatlas::AddMesh(atlas, decl) != xatlas::AddMeshError::Success) { xatlas::Destroy(atlas); return out; }

    xatlas::ChartOptions chartOpts;
    xatlas::PackOptions  packOpts;
    if (texelsPerUnit > 0.0f) packOpts.texelsPerUnit = texelsPerUnit;
    xatlas::Generate(atlas, chartOpts, packOpts);

    if (atlas->meshCount < 1 || atlas->width == 0 || atlas->height == 0) { xatlas::Destroy(atlas); return out; }
    const xatlas::Mesh& m = atlas->meshes[0];
    const float w = float(atlas->width), h = float(atlas->height);

    out.positions.resize(std::size_t(m.vertexCount) * 3);
    out.uvs.resize(std::size_t(m.vertexCount) * 2);
    out.xref.resize(m.vertexCount);
    for (std::uint32_t i = 0; i < m.vertexCount; ++i) {
        const xatlas::Vertex& v = m.vertexArray[i];
        out.xref[i] = v.xref;
        out.positions[3 * i + 0] = positions[3 * v.xref + 0];
        out.positions[3 * i + 1] = positions[3 * v.xref + 1];
        out.positions[3 * i + 2] = positions[3 * v.xref + 2];
        out.uvs[2 * i + 0] = v.uv[0] / w;                    // normalize texel -> [0,1]
        out.uvs[2 * i + 1] = v.uv[1] / h;
    }
    out.indices.assign(m.indexArray, m.indexArray + m.indexCount);
    out.atlasWidth  = int(atlas->width);
    out.atlasHeight = int(atlas->height);
    out.chartCount  = int(atlas->chartCount);
    out.ok = true;
    xatlas::Destroy(atlas);
    return out;
}

// ---------------------------------------------------------------------------
namespace {
// A unit cube centred at origin: 8 corners, 12 triangles (36 indices).
void makeCube(std::vector<float>& v, std::vector<std::uint32_t>& idx) {
    v = { -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,0.5f,-0.5f,  -0.5f,0.5f,-0.5f,
          -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,0.5f, 0.5f,  -0.5f,0.5f, 0.5f };
    idx = { 0,1,2, 0,2,3,   4,6,5, 4,7,6,   0,4,5, 0,5,1,
            1,5,6, 1,6,2,   2,6,7, 2,7,3,   3,7,4, 3,4,0 };
}
} // namespace

bool runUvAtlasGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[xatlas] GATE UV-ATLAS -- xatlas unwrap preserves triFace topology, UVs in [0,1], charts, deterministic\n");
    bool pass = true;

    std::vector<float> v; std::vector<std::uint32_t> idx; makeCube(v, idx);
    const std::uint32_t inVerts = std::uint32_t(v.size() / 3);
    const std::size_t   inTris  = idx.size() / 3;

    UvResult r = unwrapMesh(v, idx);

    // A: triangle topology preserved (indexCount unchanged) -> the triFace->BRepFace map stays valid 1:1
    const bool topoKept = r.ok && r.indices.size() == idx.size() && (r.indices.size() % 3) == 0
                       && r.positions.size() / 3 >= inVerts;   // xatlas may split verts at seams
    printf("[xatlas]   A topology: ok=%d out-tris=%zu (in %zu) out-verts=%zu (in %u, >=)  %s\n",
           r.ok, r.indices.size() / 3, inTris, r.positions.size() / 3, inVerts, topoKept ? "PASS" : "FAIL");
    pass &= topoKept;

    // B: every UV in [0,1] (normalized), and xref references a valid input vertex
    bool uvIn01 = !r.uvs.empty(), xrefOk = !r.xref.empty();
    for (float uv : r.uvs) if (uv < -1e-4f || uv > 1.0f + 1e-4f) uvIn01 = false;
    for (std::uint32_t x : r.xref) if (x >= inVerts) xrefOk = false;
    const bool B = uvIn01 && xrefOk;
    printf("[xatlas]   B uvs in [0,1]=%d ; xref valid (<%u)=%d  %s\n", uvIn01, inVerts, xrefOk, B ? "PASS" : "FAIL");
    pass &= B;

    // C: charts produced + atlas sized
    const bool C = r.chartCount > 0 && r.atlasWidth > 0 && r.atlasHeight > 0;
    printf("[xatlas]   C charts=%d atlas=%dx%d  %s\n", r.chartCount, r.atlasWidth, r.atlasHeight, C ? "PASS" : "FAIL");
    pass &= C;

    // D: deterministic -- a second unwrap yields the same vertex count and identical UVs
    UvResult r2 = unwrapMesh(v, idx);
    bool deterministic = r2.ok && r2.uvs.size() == r.uvs.size();
    if (deterministic) for (std::size_t i = 0; i < r.uvs.size(); ++i) if (std::abs(r.uvs[i] - r2.uvs[i]) > 1e-6f) deterministic = false;
    printf("[xatlas]   D deterministic: 2nd unwrap identical UVs=%d  %s\n", deterministic, deterministic ? "PASS" : "FAIL");
    pass &= deterministic;

    // E: NEG-CTRL -- an empty / degenerate mesh returns ok=false (no fabricated atlas)
    UvResult empty = unwrapMesh({}, {});
    std::vector<float> deg = { 0,0,0, 0,0,0, 0,0,0 }; std::vector<std::uint32_t> degIdx = { 0,1,2 };
    UvResult degen = unwrapMesh(deg, degIdx);          // zero-area triangle
    const bool E = !empty.ok && !degen.ok;
    printf("[xatlas]   E NEG-CTRL empty ok=%d degenerate ok=%d (both must be 0)  %s\n", empty.ok, degen.ok, E ? "PASS" : "FAIL");
    pass &= E;

    printf("[xatlas] %s\n", pass ? "ALL PASS (xatlas unwrap preserves triangle topology + triFace map; UVs normalized in [0,1]; charts packed; deterministic; empty/degenerate rejected)"
                             : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::uv
