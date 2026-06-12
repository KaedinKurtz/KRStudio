#include "CollisionCookingService.hpp"
#include "components.hpp"

#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if defined(KR_WITH_PHYSX)
#include <PxPhysicsAPI.h>
using namespace physx;
#endif

namespace {

// FNV-1a 64-bit over an arbitrary byte range.
inline uint64_t fnv1a(const void* data, size_t bytes, uint64_t seed = 14695981039346656037ull)
{
    const auto* p = static_cast<const unsigned char*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

struct CollisionCookingService::Impl
{
#if defined(KR_WITH_PHYSX)
    PxPhysics* physics = nullptr;

    std::mutex cacheMutex;     // guards the future + edge maps
    std::mutex creationMutex;  // serializes PxPhysics::create*Mesh calls
    std::unordered_map<uint64_t, std::shared_future<PxTriangleMesh*>> triCache;
    std::unordered_map<uint64_t, std::shared_future<PxConvexMesh*>> hullCache;
    // Debug-wireframe edge lists, keyed by cooked mesh pointer (stable for
    // the mesh's lifetime; shared across all instances of the geometry).
    std::unordered_map<const void*, std::shared_ptr<const std::vector<glm::vec3>>> edgeCache;

    PxTriangleMesh* cookTriangleMesh(std::vector<PxVec3> points,
                                     std::vector<uint32_t> indices,
                                     std::string name)
    {
        if (points.empty() || indices.size() < 3) return nullptr;
        QElapsedTimer timer;
        timer.start();

        PxTriangleMeshDesc desc;
        desc.points.count = static_cast<PxU32>(points.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = points.data();
        desc.triangles.count = static_cast<PxU32>(indices.size() / 3);
        desc.triangles.stride = 3 * sizeof(uint32_t);
        desc.triangles.data = indices.data();

        PxCookingParams params{ PxTolerancesScale{} };
        // Imported meshes often arrive as disjoint triangle soups; welding
        // removes the spurious internal edges that cause contact noise.
        if (!qEnvironmentVariableIsSet("KRS_NO_WELD")) {
            params.meshWeldTolerance = 1e-4f;
            params.meshPreprocessParams = PxMeshPreprocessingFlag::eWELD_VERTICES;
        }

        // Cook + insert directly (same path the convex cooking has always
        // used); creation is serialized because PxPhysics insertion is the
        // shared resource.
        PxTriangleMesh* mesh = nullptr;
        PxTriangleMeshCookingResult::Enum result = PxTriangleMeshCookingResult::eSUCCESS;
        {
            std::lock_guard<std::mutex> lock(creationMutex);
            if (physics)
                mesh = PxCreateTriangleMesh(params, desc,
                                            physics->getPhysicsInsertionCallback(), &result);
        }
        if (!mesh) {
            qWarning() << "[Cook] trimesh cooking FAILED for" << name.c_str()
                       << "(" << points.size() << "verts," << indices.size() / 3 << "tris, result"
                       << int(result) << ")";
            return nullptr;
        }
        if (mesh)
            qInfo().nospace() << "[Cook] trimesh '" << name.c_str() << "': "
                              << points.size() << " verts -> " << mesh->getNbTriangles()
                              << " tris in " << timer.elapsed() << " ms";
        return mesh;
    }

    PxConvexMesh* cookConvexHull(std::vector<PxVec3> points, std::string name)
    {
        if (points.size() < 4) return nullptr;
        QElapsedTimer timer;
        timer.start();

        PxConvexMeshDesc desc;
        desc.points.count = static_cast<PxU32>(points.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = points.data();
        desc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eQUANTIZE_INPUT;
        desc.vertexLimit = 64;

        PxCookingParams params{ PxTolerancesScale{} };
        PxDefaultMemoryOutputStream out;
        if (!PxCookConvexMesh(params, desc, out)) {
            qWarning() << "[Cook] convex cooking FAILED for" << name.c_str()
                       << "(" << points.size() << "points )";
            return nullptr;
        }

        PxConvexMesh* mesh = nullptr;
        {
            std::lock_guard<std::mutex> lock(creationMutex);
            if (physics) {
                PxDefaultMemoryInputData in(out.getData(), out.getSize());
                mesh = physics->createConvexMesh(in);
            }
        }
        if (mesh)
            qInfo().nospace() << "[Cook] hull '" << name.c_str() << "': "
                              << points.size() << " points -> " << mesh->getNbVertices()
                              << " hull verts in " << timer.elapsed() << " ms";
        return mesh;
    }
#endif
};

namespace {

#if defined(KR_WITH_PHYSX)
/// Collect the unique edges of an index list as a GL_LINES vertex list.
std::shared_ptr<const std::vector<glm::vec3>>
buildEdgeList(const PxVec3* verts, const std::vector<std::pair<uint32_t, uint32_t>>& edges)
{
    auto out = std::make_shared<std::vector<glm::vec3>>();
    out->reserve(edges.size() * 2);
    for (const auto& [a, b] : edges) {
        out->emplace_back(verts[a].x, verts[a].y, verts[a].z);
        out->emplace_back(verts[b].x, verts[b].y, verts[b].z);
    }
    return out;
}

void addEdge(std::vector<std::pair<uint32_t, uint32_t>>& edges,
             std::unordered_set<uint64_t>& seen, uint32_t a, uint32_t b)
{
    const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
    if (seen.insert(key).second) edges.emplace_back(a, b);
}
#endif

} // namespace

CollisionCookingService& CollisionCookingService::instance()
{
    static CollisionCookingService s;
    return s;
}

CollisionCookingService::CollisionCookingService() : m_impl(new Impl) {}
CollisionCookingService::~CollisionCookingService() { delete m_impl; }

void CollisionCookingService::initialize(physx::PxPhysics* physics)
{
#if defined(KR_WITH_PHYSX)
    m_impl->physics = physics;
#else
    Q_UNUSED(physics);
#endif
}

bool CollisionCookingService::isInitialized() const
{
#if defined(KR_WITH_PHYSX)
    return m_impl->physics != nullptr;
#else
    return false;
#endif
}

void CollisionCookingService::shutdown()
{
#if defined(KR_WITH_PHYSX)
    // Wait for in-flight cooks, then drop our cache references. Shapes that
    // still use a mesh keep it alive through PhysX refcounting.
    std::unordered_map<uint64_t, std::shared_future<PxTriangleMesh*>> tris;
    std::unordered_map<uint64_t, std::shared_future<PxConvexMesh*>> hulls;
    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        tris.swap(m_impl->triCache);
        hulls.swap(m_impl->hullCache);
    }
    for (auto& [key, fut] : tris)
        if (PxTriangleMesh* m = fut.valid() ? fut.get() : nullptr) m->release();
    for (auto& [key, fut] : hulls)
        if (PxConvexMesh* m = fut.valid() ? fut.get() : nullptr) m->release();
    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        m_impl->edgeCache.clear(); // keys dangle once the meshes are released
    }
    m_impl->physics = nullptr;
#endif
}

uint64_t CollisionCookingService::hashGeometry(const std::vector<Vertex>& vertices,
                                               const std::vector<unsigned int>& indices)
{
    // Hash positions only: normals/UVs don't affect collision, and the same
    // geometry re-exported with different shading data should share a cook.
    uint64_t h = 14695981039346656037ull;
    for (const Vertex& v : vertices)
        h = fnv1a(&v.position, sizeof(v.position), h);
    if (!indices.empty())
        h = fnv1a(indices.data(), indices.size() * sizeof(unsigned int), h);
    return h;
}

std::shared_future<physx::PxTriangleMesh*>
CollisionCookingService::requestTriangleMesh(const std::vector<Vertex>& vertices,
                                             const std::vector<unsigned int>& indices,
                                             const std::string& debugName)
{
#if defined(KR_WITH_PHYSX)
    if (!isInitialized() || vertices.empty() || indices.size() < 3) {
        std::promise<PxTriangleMesh*> p;
        p.set_value(nullptr);
        return p.get_future().share();
    }

    const uint64_t key = hashGeometry(vertices, indices);
    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        auto it = m_impl->triCache.find(key);
        if (it != m_impl->triCache.end()) {
            // Evict resolved failures so a later request can retry.
            const bool failed = it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready
                                && it->second.get() == nullptr;
            if (!failed) return it->second;
            m_impl->triCache.erase(it);
        }

        // Copy geometry for the worker thread: registry storage can move.
        std::vector<PxVec3> points;
        points.reserve(vertices.size());
        for (const Vertex& v : vertices)
            points.emplace_back(v.position.x, v.position.y, v.position.z);
        std::vector<uint32_t> idx(indices.begin(), indices.end());

        Impl* impl = m_impl;
        auto fut = std::async(std::launch::async,
                              [impl, pts = std::move(points), idx = std::move(idx),
                               name = debugName]() mutable {
                                  return impl->cookTriangleMesh(std::move(pts), std::move(idx),
                                                                std::move(name));
                              })
                       .share();
        m_impl->triCache.emplace(key, fut);
        return fut;
    }
#else
    Q_UNUSED(vertices); Q_UNUSED(indices); Q_UNUSED(debugName);
    std::promise<physx::PxTriangleMesh*> p;
    p.set_value(nullptr);
    return p.get_future().share();
#endif
}

std::shared_future<physx::PxConvexMesh*>
CollisionCookingService::requestConvexHull(const std::vector<Vertex>& vertices,
                                           const std::string& debugName)
{
#if defined(KR_WITH_PHYSX)
    if (!isInitialized() || vertices.size() < 4) {
        std::promise<PxConvexMesh*> p;
        p.set_value(nullptr);
        return p.get_future().share();
    }

    static const std::vector<unsigned int> kNoIndices;
    const uint64_t key = hashGeometry(vertices, kNoIndices);
    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        auto it = m_impl->hullCache.find(key);
        if (it != m_impl->hullCache.end()) {
            const bool failed = it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready
                                && it->second.get() == nullptr;
            if (!failed) return it->second;
            m_impl->hullCache.erase(it);
        }

        // Hulls don't need every vertex of a dense mesh; subsample huge inputs.
        std::vector<PxVec3> points;
        const size_t stride = std::max<size_t>(1, vertices.size() / 2048);
        points.reserve(vertices.size() / stride + 1);
        for (size_t i = 0; i < vertices.size(); i += stride) {
            const auto& p = vertices[i].position;
            points.emplace_back(p.x, p.y, p.z);
        }

        Impl* impl = m_impl;
        auto fut = std::async(std::launch::async,
                              [impl, pts = std::move(points), name = debugName]() mutable {
                                  return impl->cookConvexHull(std::move(pts), std::move(name));
                              })
                       .share();
        m_impl->hullCache.emplace(key, fut);
        return fut;
    }
#else
    Q_UNUSED(vertices); Q_UNUSED(debugName);
    std::promise<physx::PxConvexMesh*> p;
    p.set_value(nullptr);
    return p.get_future().share();
#endif
}

std::shared_ptr<const std::vector<glm::vec3>>
CollisionCookingService::debugEdges(const std::vector<Vertex>& vertices,
                                    const std::vector<unsigned int>& indices,
                                    bool exactTrimesh)
{
#if defined(KR_WITH_PHYSX)
    if (!isInitialized()) return nullptr;

    const void* meshPtr = nullptr;
    if (exactTrimesh) {
        auto fut = requestTriangleMesh(vertices, indices, "debug-edges");
        if (!fut.valid() || fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return nullptr;
        meshPtr = fut.get();
    }
    else {
        auto fut = requestConvexHull(vertices, "debug-edges");
        if (!fut.valid() || fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return nullptr;
        meshPtr = fut.get();
    }
    if (!meshPtr) return nullptr;

    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        auto it = m_impl->edgeCache.find(meshPtr);
        if (it != m_impl->edgeCache.end()) return it->second;
    }

    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::unordered_set<uint64_t> seen;
    std::shared_ptr<const std::vector<glm::vec3>> result;

    if (exactTrimesh) {
        const auto* tm = static_cast<const PxTriangleMesh*>(meshPtr);
        const PxU32 nbTris = tm->getNbTriangles();
        edges.reserve(nbTris * 3);
        seen.reserve(nbTris * 3);
        const bool sixteenBit = tm->getTriangleMeshFlags() & PxTriangleMeshFlag::e16_BIT_INDICES;
        const void* tris = tm->getTriangles();
        for (PxU32 i = 0; i < nbTris; ++i) {
            uint32_t a, b, c;
            if (sixteenBit) {
                const PxU16* t = static_cast<const PxU16*>(tris) + i * 3;
                a = t[0]; b = t[1]; c = t[2];
            }
            else {
                const PxU32* t = static_cast<const PxU32*>(tris) + i * 3;
                a = t[0]; b = t[1]; c = t[2];
            }
            addEdge(edges, seen, a, b);
            addEdge(edges, seen, b, c);
            addEdge(edges, seen, c, a);
        }
        result = buildEdgeList(tm->getVertices(), edges);
    }
    else {
        const auto* hull = static_cast<const PxConvexMesh*>(meshPtr);
        const PxU8* idx = hull->getIndexBuffer();
        for (PxU32 p = 0; p < hull->getNbPolygons(); ++p) {
            PxHullPolygon poly;
            if (!hull->getPolygonData(p, poly)) continue;
            for (PxU16 v = 0; v < poly.mNbVerts; ++v) {
                const uint32_t a = idx[poly.mIndexBase + v];
                const uint32_t b = idx[poly.mIndexBase + (v + 1) % poly.mNbVerts];
                addEdge(edges, seen, a, b);
            }
        }
        result = buildEdgeList(hull->getVertices(), edges);
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->cacheMutex);
        m_impl->edgeCache.emplace(meshPtr, result);
    }
    return result;
#else
    Q_UNUSED(vertices); Q_UNUSED(indices); Q_UNUSED(exactTrimesh);
    return nullptr;
#endif
}
