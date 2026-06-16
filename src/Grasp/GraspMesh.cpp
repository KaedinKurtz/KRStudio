#include "GraspMesh.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cstdint>

namespace krs::grasp {

MeshMetrics computeMetrics(const RenderableMeshComponent& mesh) {
    MeshMetrics r;
    r.nVerts = int(mesh.vertices.size());
    r.nTris = int(mesh.indices.size() / 3);
    if (mesh.vertices.empty()) return r;

    glm::dvec3 lo(1e30), hi(-1e30);
    bool finite = true;
    for (const Vertex& v : mesh.vertices) {
        const glm::dvec3 p(v.position);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) finite = false;
        if (!std::isfinite(double(v.normal.x)) || !std::isfinite(double(v.normal.y)) || !std::isfinite(double(v.normal.z))) finite = false;
        lo = glm::min(lo, p); hi = glm::max(hi, p);
    }
    r.aabbMin = lo; r.aabbMax = hi; r.extent = hi - lo;
    r.longest = std::max({ r.extent.x, r.extent.y, r.extent.z });
    r.shortest = std::min({ r.extent.x, r.extent.y, r.extent.z });

    // Enclosed volume + centroid via signed tetrahedra (apex at origin, base = each triangle).
    double sv = 0.0;
    glm::dvec3 cacc(0.0);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::dvec3 a(mesh.vertices[mesh.indices[i]].position);
        const glm::dvec3 b(mesh.vertices[mesh.indices[i + 1]].position);
        const glm::dvec3 c(mesh.vertices[mesh.indices[i + 2]].position);
        const double dv = glm::dot(a, glm::cross(b, c)) / 6.0;   // signed tet volume
        sv += dv;
        cacc += (a + b + c) * (0.25 * dv);                       // tet centroid (0+a+b+c)/4, vol-weighted
    }
    r.signedVolume = sv;
    r.volume = std::fabs(sv);
    r.centroid = (std::fabs(sv) > 1e-12) ? cacc / sv : (lo + hi) * 0.5;
    r.finite = finite && std::isfinite(r.volume) && r.volume > 0.0;

    // Watertight / manifold check: every undirected edge of a closed consistent-winding mesh is shared by
    // exactly 2 triangles. count==1 => open boundary; count>2 => non-manifold. A grossly non-watertight mesh
    // (open box) has a large boundary fraction AND an unreliable/inflated volume; an inverted mesh has
    // signedVolume < 0. Both make the mesh unfit to cook an honest collider, so the gate rejects them.
    std::unordered_map<std::uint64_t, int> edge;
    edge.reserve(mesh.indices.size());
    auto key = [](std::uint32_t a, std::uint32_t b) {
        const std::uint32_t lo2 = std::min(a, b), hi2 = std::max(a, b);
        return (std::uint64_t(lo2) << 32) | hi2;
    };
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        ++edge[key(a, b)]; ++edge[key(b, c)]; ++edge[key(c, a)];
    }
    long bound = 0, nonman = 0;
    for (const auto& kv : edge) { if (kv.second == 1) ++bound; else if (kv.second > 2) ++nonman; }
    const double ne = edge.empty() ? 1.0 : double(edge.size());
    r.boundaryFrac = double(bound) / ne;
    r.nonManifoldFrac = double(nonman) / ne;
    // 1% threshold: real watertight scans measure 0.00% boundary; a holed/open mesh measures >>1% -> clean teeth.
    r.watertight = !edge.empty() && r.boundaryFrac < 0.01 && r.nonManifoldFrac < 0.01 && r.signedVolume > 0.0;
    return r;
}

RenderableMeshComponent scaledCopy(const RenderableMeshComponent& mesh, double s) {
    RenderableMeshComponent out = mesh;
    for (Vertex& v : out.vertices) v.position *= float(s);
    out.aabbMin *= float(s); out.aabbMax *= float(s);
    return out;
}

} // namespace krs::grasp
