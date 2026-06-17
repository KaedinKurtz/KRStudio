#pragma once
// CoacdCollider.hpp -- loader for the OFFLINE-generated CoACD (Approximate Convex Decomposition) colliders.
// scripts/gen_coacd.py runs CoACD (github.com/SarahWeiii/CoACD, MIT) via its prebuilt Python wheel and writes
// one binary per object: assets/ycb/<id>/coacd.bin. We ship only the OUTPUT convex parts (derivatives of the
// YCB meshes) -- no CoACD source is vendored, so the C++ build is untouched. Each part is an already-convex
// hull; the cooking service cooks each into a PxConvexMesh exactly as the V-HACD decomposition path does, so
// the ONLY thing that changes vs the prior pipeline is the collider geometry (the locked physics/criterion are
// unchanged). Format:  "COAC" | u32 numParts | per part { u32 numVerts ; float32[3]*numVerts }  (little-endian).
#include <glm/glm.hpp>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace krs::grasp {

// Read a cooked CoACD decomposition into one vertex cloud per convex part (mesh-local meters, the SAME frame as
// the .ply Vertex.position). Returns false (leaving `out` empty/partial) on any missing/short/corrupt file, so
// the caller can fall back to the runtime V-HACD path.
inline bool loadCoacdParts(const std::string& path, std::vector<std::vector<glm::vec3>>& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4] = {};
    in.read(magic, 4);
    if (in.gcount() != 4 || magic[0] != 'C' || magic[1] != 'O' || magic[2] != 'A' || magic[3] != 'C') return false;
    std::uint32_t numParts = 0;
    in.read(reinterpret_cast<char*>(&numParts), sizeof(numParts));
    if (!in || numParts == 0 || numParts > 100000u) return false;
    out.reserve(numParts);
    for (std::uint32_t p = 0; p < numParts; ++p) {
        std::uint32_t numVerts = 0;
        in.read(reinterpret_cast<char*>(&numVerts), sizeof(numVerts));
        if (!in || numVerts == 0 || numVerts > 10000000u) { out.clear(); return false; }
        std::vector<glm::vec3> verts(numVerts);   // glm::vec3 == 3 tightly-packed floats -> bulk read
        const std::streamsize bytes = std::streamsize(numVerts) * std::streamsize(sizeof(glm::vec3));
        in.read(reinterpret_cast<char*>(verts.data()), bytes);
        if (in.gcount() != bytes) { out.clear(); return false; }
        out.push_back(std::move(verts));
    }
    return !out.empty();
}

} // namespace krs::grasp
