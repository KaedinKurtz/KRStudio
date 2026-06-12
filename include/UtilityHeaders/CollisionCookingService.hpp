#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>

struct Vertex; // components.hpp

namespace physx {
class PxPhysics;
class PxTriangleMesh;
class PxConvexMesh;
}

/**
 * @brief Central cooking + cache for physics collision geometry.
 *
 * Every mesh that enters the scene gets real-shape collision: statics and
 * kinematics use exact cooked triangle meshes, dynamics use convex hulls
 * (PhysX cannot simulate dynamic trimeshes on the CPU solver). Cooking is
 * expensive, so:
 *
 *  - Results are cached by an FNV-1a hash of the raw geometry (positions +
 *    indices). Ten copies of the same dragon cook once; primitives with no
 *    sourcePath get correct keys for free.
 *  - Geometry is cooked UNSCALED — the entity's scale is applied at shape
 *    creation time via PxMeshScale, so one cooked mesh serves every instance
 *    at every scale.
 *  - Cooking runs on worker threads (PhysX cooking is thread-safe); only the
 *    final PxPhysics object creation is serialized. Spawn paths request
 *    cooks speculatively so the data is warm before the user presses Play.
 *
 * Lifetime: initialize() after PxPhysics exists, shutdown() before it is
 * released (waits for in-flight cooks and drops the cache references).
 */
class CollisionCookingService
{
public:
    static CollisionCookingService& instance();

    void initialize(physx::PxPhysics* physics);
    void shutdown();
    bool isInitialized() const;

    /// Exact triangle mesh (static/kinematic actors). Future yields nullptr
    /// on cooking failure or when the service is unavailable.
    std::shared_future<physx::PxTriangleMesh*>
    requestTriangleMesh(const std::vector<Vertex>& vertices,
                        const std::vector<unsigned int>& indices,
                        const std::string& debugName);

    /// Vertex-limited convex hull (dynamic actors).
    std::shared_future<physx::PxConvexMesh*>
    requestConvexHull(const std::vector<Vertex>& vertices,
                      const std::string& debugName);

    static uint64_t hashGeometry(const std::vector<Vertex>& vertices,
                                 const std::vector<unsigned int>& indices);

private:
    CollisionCookingService();
    ~CollisionCookingService();
    CollisionCookingService(const CollisionCookingService&) = delete;
    CollisionCookingService& operator=(const CollisionCookingService&) = delete;

    struct Impl;
    Impl* m_impl; // raw: singleton, freed in shutdown-safe dtor
};
