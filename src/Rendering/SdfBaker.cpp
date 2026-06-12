// OpenVDB mesh -> SDF baking. This file is compiled with /permissive-
// (see CMakeLists): OpenVDB 12 headers require a conformant compiler.
#include "SdfBaker.hpp"
#include "components.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>

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
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/Interpolation.h>
#endif

bool bakeMeshToSdf(const std::vector<Vertex>& vertices,
                   const std::vector<unsigned int>& indices,
                   const glm::mat4& worldTransform,
                   float voxelSize,
                   SdfBakeResult& out)
{
#if defined(KR_WITH_OPENVDB)
    if (vertices.empty() || indices.size() < 3) return false;

    static bool s_vdbInit = false;
    if (!s_vdbInit) { openvdb::initialize(); s_vdbInit = true; }

    QElapsedTimer bakeTimer;
    bakeTimer.start();

    std::vector<openvdb::Vec3s> points;
    points.reserve(vertices.size());
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const auto& v : vertices) {
        const glm::vec3 p = glm::vec3(worldTransform * glm::vec4(v.position, 1.0f));
        points.emplace_back(p.x, p.y, p.z);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    std::vector<openvdb::Vec3I> tris;
    tris.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
        tris.emplace_back(indices[i], indices[i + 1], indices[i + 2]);

    const float voxel = std::max(0.005f, voxelSize);
    const float halfWidthVoxels = 4.0f;
    auto vdbXform = openvdb::math::Transform::createLinearTransform(voxel);
    openvdb::FloatGrid::Ptr grid;
    try {
        grid = openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(
            *vdbXform, points, tris, halfWidthVoxels);
    }
    catch (const std::exception& ex) {
        qWarning() << "[SdfBaker] meshToLevelSet failed:" << ex.what();
        return false;
    }

    const float margin = halfWidthVoxels * voxel;
    mn -= glm::vec3(margin);
    mx += glm::vec3(margin);

    out.aabbMin = mn;
    out.aabbMax = mx;
    const glm::vec3 ext = mx - mn;
    out.dims = glm::clamp(glm::ivec3(glm::ceil(ext / voxel)), glm::ivec3(8), glm::ivec3(160));
    out.field.resize(size_t(out.dims.x) * out.dims.y * out.dims.z);

    openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(*grid);
    const glm::vec3 step = ext / glm::vec3(out.dims - glm::ivec3(1));
    size_t idx = 0;
    for (int z = 0; z < out.dims.z; ++z)
        for (int y = 0; y < out.dims.y; ++y)
            for (int x = 0; x < out.dims.x; ++x, ++idx) {
                const glm::vec3 wp = mn + step * glm::vec3(x, y, z);
                out.field[idx] = float(sampler.wsSample(openvdb::Vec3R(wp.x, wp.y, wp.z)));
            }

    qInfo() << "[SdfBaker] baked" << out.dims.x << "x" << out.dims.y << "x" << out.dims.z
            << "voxels @" << voxel << "m from" << tris.size() << "triangles in"
            << bakeTimer.elapsed() << "ms";
    return true;
#else
    Q_UNUSED(vertices); Q_UNUSED(indices); Q_UNUSED(worldTransform);
    Q_UNUSED(voxelSize); Q_UNUSED(out);
    qWarning() << "[SdfBaker] built without OpenVDB - SDF colliders disabled";
    return false;
#endif
}
