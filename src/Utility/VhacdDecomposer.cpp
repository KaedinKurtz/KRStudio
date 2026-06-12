#include "VhacdDecomposer.hpp"
#include "components.hpp"

#include <QDebug>
#include <QElapsedTimer>

#if defined(KR_WITH_VHACD)
// Exactly ONE translation unit in the project may define the implementation.
#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>
#endif

namespace krs {

std::vector<std::vector<glm::vec3>>
decomposeMesh(const std::vector<Vertex>& vertices,
              const std::vector<unsigned int>& indices,
              int maxHulls, int voxelResolution, int maxVertsPerHull)
{
    std::vector<std::vector<glm::vec3>> hulls;
#if defined(KR_WITH_VHACD)
    if (vertices.size() < 4 || indices.size() < 3) return hulls;

    QElapsedTimer timer;
    timer.start();

    std::vector<float> points;
    points.reserve(vertices.size() * 3);
    for (const Vertex& v : vertices) {
        points.push_back(v.position.x);
        points.push_back(v.position.y);
        points.push_back(v.position.z);
    }

    VHACD::IVHACD::Parameters params;
    params.m_maxConvexHulls = uint32_t(maxHulls);
    params.m_resolution = uint32_t(voxelResolution);
    params.m_minimumVolumePercentErrorAllowed = 1.0;
    params.m_maxNumVerticesPerCH = uint32_t(maxVertsPerHull); // PhysX hull limit
    params.m_shrinkWrap = true;
    // KNOWN LIMIT: on NON-MANIFOLD compounds (overlapping shells) V-HACD's
    // voxel classification can partially fill open cavities regardless of
    // fill mode — single manifold meshes (typical imports) decompose
    // correctly. Exact containers should prefer static trimesh collision.
    params.m_fillMode = VHACD::FillMode::FLOOD_FILL;
    params.m_asyncACD = false; // we are already on a worker thread

    VHACD::IVHACD* vhacd = VHACD::CreateVHACD();
    const bool ok = vhacd->Compute(points.data(), uint32_t(vertices.size()),
                                   indices.data(), uint32_t(indices.size() / 3), params);
    if (ok) {
        hulls.reserve(vhacd->GetNConvexHulls());
        for (uint32_t i = 0; i < vhacd->GetNConvexHulls(); ++i) {
            VHACD::IVHACD::ConvexHull ch;
            vhacd->GetConvexHull(i, ch);
            if (ch.m_volume <= 0.0 || ch.m_points.size() < 4) continue; // flat hulls fail cooking
            std::vector<glm::vec3> pts;
            pts.reserve(ch.m_points.size());
            for (const auto& p : ch.m_points) // V-HACD outputs doubles
                pts.emplace_back(float(p.mX), float(p.mY), float(p.mZ));
            hulls.push_back(std::move(pts));
        }
    }
    vhacd->Clean();
    vhacd->Release();

    qInfo().nospace() << "[Cook] V-HACD: " << indices.size() / 3 << " tris -> "
                      << hulls.size() << " hulls in " << timer.elapsed() << " ms";
#else
    Q_UNUSED(vertices); Q_UNUSED(indices);
    Q_UNUSED(maxHulls); Q_UNUSED(voxelResolution); Q_UNUSED(maxVertsPerHull);
    qWarning() << "[Cook] V-HACD not built in (KR_WITH_VHACD off)";
#endif
    return hulls;
}

} // namespace krs
