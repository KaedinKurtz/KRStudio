// OpenVDB particle -> level set -> triangle mesh ("hero stills"). Compiled
// with /permissive- like SdfBaker.cpp: OpenVDB 12 headers require it.
#include "FluidMesher.hpp"

#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <limits>

#if defined(KR_WITH_OPENVDB)
// Qt's keyword macros collide with OpenVDB (TypeList::foreach) and TBB
// (profiling.h's emit functions). Undefine them for these includes.
#ifdef foreach
#undef foreach
#endif
#ifdef emit
#undef emit
#endif
#ifdef signals
#undef signals
#endif
#ifdef slots
#undef slots
#endif
#include <openvdb/openvdb.h>
#include <openvdb/tools/ParticlesToLevelSet.h>
#include <openvdb/tools/LevelSetFilter.h>
#include <openvdb/tools/VolumeToMesh.h>
#endif

namespace krs {

#if defined(KR_WITH_OPENVDB)
namespace {
// Adapter satisfying openvdb::tools::ParticlesToLevelSet's ParticleList.
struct ParticleList {
    using PosType = openvdb::Vec3R;

    const std::vector<glm::vec3>& pts;
    openvdb::Real radius;

    size_t size() const { return pts.size(); }
    void getPos(size_t n, PosType& xyz) const
    {
        xyz = PosType(pts[n].x, pts[n].y, pts[n].z);
    }
    void getPosRad(size_t n, PosType& xyz, openvdb::Real& r) const
    {
        getPos(n, xyz);
        r = radius;
    }
};
} // namespace
#endif

bool meshFluidParticles(const std::vector<glm::vec3>& positions, float particleRadius,
                        RenderableMeshComponent& out)
{
#if defined(KR_WITH_OPENVDB)
    if (positions.empty() || particleRadius <= 0.0f) return false;

    static bool s_vdbInit = false;
    if (!s_vdbInit) {
        openvdb::initialize();
        s_vdbInit = true;
    }

    QElapsedTimer timer;
    timer.start();

    // Splat radius > rest spacing closes the gaps of a relaxed packing;
    // voxel size trades fidelity for time/memory.
    const float splatRadius = particleRadius * 1.6f;
    const float voxel = std::max(0.004f, particleRadius * 0.6f);
    const float halfWidth = 3.0f;

    auto grid = openvdb::createLevelSet<openvdb::FloatGrid>(voxel, halfWidth);
    openvdb::tools::ParticlesToLevelSet<openvdb::FloatGrid> raster(*grid);
    raster.setRmin(0.0); // keep every particle, even below voxel size
    ParticleList plist{ positions, splatRadius };
    raster.rasterizeSpheres(plist);
    raster.finalize();

    // Light smoothing: melts the sphere union into a sheet without losing
    // splash features.
    openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filter(*grid);
    filter.gaussian();

    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> tris;
    std::vector<openvdb::Vec4I> quads;
    openvdb::tools::volumeToMesh(*grid, points, tris, quads, 0.0, 0.35);
    if (points.empty() || (tris.empty() && quads.empty())) return false;

    out = RenderableMeshComponent{};
    out.vertices.resize(points.size());
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (size_t i = 0; i < points.size(); ++i) {
        const glm::vec3 p(points[i].x(), points[i].y(), points[i].z());
        out.vertices[i].position = p;
        out.vertices[i].normal = glm::vec3(0.0f);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    out.aabbMin = mn;
    out.aabbMax = mx;

    // OpenVDB winds level-set faces clockwise for OpenGL's conventions —
    // emit reversed so normals point out of the water.
    out.indices.reserve(tris.size() * 3 + quads.size() * 6);
    auto pushTri = [&](unsigned a, unsigned b, unsigned c) {
        out.indices.push_back(a);
        out.indices.push_back(c);
        out.indices.push_back(b);
    };
    for (const auto& t : tris) pushTri(t.x(), t.y(), t.z());
    for (const auto& q : quads) {
        pushTri(q.x(), q.y(), q.z());
        pushTri(q.x(), q.z(), q.w());
    }

    // Smooth vertex normals from face accumulation.
    for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
        auto& v0 = out.vertices[out.indices[i]];
        auto& v1 = out.vertices[out.indices[i + 1]];
        auto& v2 = out.vertices[out.indices[i + 2]];
        const glm::vec3 n = glm::cross(v1.position - v0.position, v2.position - v0.position);
        v0.normal += n;
        v1.normal += n;
        v2.normal += n;
    }
    for (auto& v : out.vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0, 1, 0);
    }

    out.hasUVs = false;
    out.hasTangents = false;
    out.sourcePath = "fluid-mesh";

    qInfo() << "[FluidMesher]" << positions.size() << "particles ->" << out.vertices.size()
            << "verts," << out.indices.size() / 3 << "tris in" << timer.elapsed() << "ms (voxel"
            << voxel << ")";
    return true;
#else
    Q_UNUSED(positions);
    Q_UNUSED(particleRadius);
    Q_UNUSED(out);
    qWarning() << "[FluidMesher] built without OpenVDB";
    return false;
#endif
}

} // namespace krs
