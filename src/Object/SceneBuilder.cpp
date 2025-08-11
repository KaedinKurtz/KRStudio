#include "SceneBuilder.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "Mesh.hpp"
#include "MeshUtils.hpp"
#include "PrimitiveBuilders.hpp"
#include "ResourceManager.hpp" // For loading meshes
#include "SceneQuery.hpp"      // For SpawnCommand, SurfaceQueryFunction, etc.

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtc/constants.hpp>
#include <QDebug>
#include <random>    // For high-quality random number generation
#include <QFileInfo> // For getting base names from paths

namespace {
    // A static generator ensures it's seeded only once for true randomness.
    std::mt19937& getRandomGenerator() {
        static std::random_device rd;
        static std::mt19937 generator(rd());
        return generator;
    }

    float randomFloat(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(getRandomGenerator());
    }
    static std::mt19937& rng() { static std::mt19937 gen{ std::random_device{}() }; return gen; }
    static int   randomInt(int a, int b) { std::uniform_int_distribution<int> d(a, b); return d(rng()); }


    // Build an ONB where local + Z looks along dir; upHint defines roll if not parallel.
        inline glm::mat3 basisLookZ(const glm::vec3 & dirWorld, const glm::vec3 & upHintWorld = glm::vec3(0, 1, 0)) {
        glm::vec3 Z = glm::length2(dirWorld) > 0 ? glm::normalize(dirWorld) : glm::vec3(0, 0, 1);
        glm::vec3 Uhint = (glm::length2(upHintWorld) > 0) ? glm::normalize(upHintWorld) : glm::vec3(0, 1, 0);
        // If parallel, pick a safe hint
        if (std::abs(glm::dot(Z, Uhint)) > 0.999f) Uhint = std::abs(Z.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 X = glm::normalize(glm::cross(Uhint, Z));
        glm::vec3 Y = glm::normalize(glm::cross(Z, X));
        return glm::mat3(X, Y, Z); // columns
    }

        inline glm::mat3 basisLookDir(const glm::vec3& forward, const glm::vec3& upHint = glm::vec3(0, 1, 0)) {
            glm::vec3 Z = glm::length2(forward) > 0 ? glm::normalize(forward) : glm::vec3(0, 0, 1);
            glm::vec3 U = glm::length2(upHint) > 0 ? glm::normalize(upHint) : glm::vec3(0, 1, 0);
            if (std::abs(glm::dot(Z, U)) > 0.999f) U = std::abs(Z.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 X = glm::normalize(glm::cross(U, Z));
            glm::vec3 Y = glm::normalize(glm::cross(Z, X));
            return glm::mat3(X, Y, Z);
        }

    inline glm::quat quatFromBasis(const glm::mat3 & R) {
        return glm::quat_cast(R);
    }

    // Compute local half-extents from mesh data (centered AABB).
    inline glm::vec3 meshLocalHalfExtents(MeshID meshId) {
        const RenderableMeshComponent* mesh = ResourceManager::instance().getMesh(meshId);
        if (!mesh || mesh->vertices.empty()) return glm::vec3(0.5f);
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(-std::numeric_limits<float>::max());
        for (const auto& v : mesh->vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        return 0.5f * (mx - mn);
    }

    // Raycast helper (returns whatever your SceneQuery function returns)
    template <typename HitT>
    inline std::optional<SurfaceHit>
        castInDirection(const glm::vec3& origin,
            const glm::vec3& dir,
            const SurfaceQueryFunction& query)
    {
        return query(origin, dir);
    }

    inline glm::vec3 bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
        float u = 1.0f - t;
        return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
    }
    inline glm::vec3 bezierTangent(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
        float u = 1.0f - t;
        return 3.0f * u * u * (p1 - p0) + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
    }
}


// ===================================================================
// ==                  MASTER INSTANCING FUNCTION                   ==
// ===================================================================
// This is the only function that creates entities. All others call it.

entt::entity SceneBuilder::spawnMeshInstance(Scene& scene, MeshID meshId,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec3& scale)
{
    // 1. Get the mesh data from the ResourceManager.
    const RenderableMeshComponent* meshData = ResourceManager::instance().getMesh(meshId);

    // --- Memory Logging ---
    qDebug() << "[SceneBuilder] Received mesh data from ResourceManager at address:" << meshData;
    if (!meshData) {
        qWarning() << "  [!!!] SceneBuilder received a null pointer! Cannot spawn mesh.";
        return entt::null;
    }
    qDebug() << "  - Received Vertex Count:" << meshData->vertices.size()
        << "| Received Index Count:" << meshData->indices.size();
    // ----------------------

    // 2. Create a new entity in the scene.
    auto& registry = scene.getRegistry();
    auto newEntity = registry.create();

    // 3. Add the core components by COPYING the data from the manager.
    //    We use emplace_or_replace to prevent crashes if a component (like a Transform)
    //    was already added to the entity for some reason.
    auto& newMeshComp = registry.emplace_or_replace<RenderableMeshComponent>(newEntity, *meshData);
    registry.emplace_or_replace<TransformComponent>(newEntity, position, rotation, scale);
    registry.emplace_or_replace<TagComponent>(newEntity, QFileInfo(QString::fromStdString(meshData->sourcePath)).baseName().toStdString());


    // --- Memory Logging ---
    qDebug() << "[SceneBuilder] Emplaced new RenderableMeshComponent on entity" << (uint32_t)newEntity;
    qDebug() << "  - New component's address:" << &newMeshComp;
    qDebug() << "  - New component's Vertex Count:" << newMeshComp.vertices.size()
        << "| New component's Index Count:" << newMeshComp.indices.size();
    if (newMeshComp.indices.empty() && meshData->indices.size() > 0) { // Check against source
        qWarning() << "  [!!!] CRITICAL: Data was lost during emplace! The component is empty.";
    }
    // ----------------------

    // 4. Add the correct material rendering tag based on the mesh's data.
    if (meshData->hasUVs && meshData->hasTangents) {
        registry.emplace_or_replace<UVTexturedMaterialTag>(newEntity);
    }
    else {
        registry.emplace_or_replace<TriPlanarMaterialTag>(newEntity);
    }

    return newEntity;
}

// ===================================================================
// ==                   SPAWNING FROM FILE PATH                     ==
// ===================================================================

entt::entity SceneBuilder::spawnMesh(Scene& scene, const QString& meshPath, const TransformComponent& transform)
{
    // 1. Ask the ResourceManager to load the mesh. It handles caching.
    MeshID meshId = ResourceManager::instance().loadMesh(meshPath);

    if (meshId == MeshID::None) {
        qWarning() << "Could not spawn mesh because it failed to load from path:" << meshPath;
        return entt::null;
    }

    // 2. Now that we have a valid ID, call the corresponding instance spawner.
    return spawnMeshInstance(scene, meshId, transform);
}

entt::entity SceneBuilder::spawnMesh(Scene& scene, const QString& meshPath, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
{
    // This is just a convenience overload that builds a TransformComponent.
    return spawnMesh(scene, meshPath, TransformComponent{ position, rotation, scale });
}


// ===================================================================
// ==                SIMPLE INSTANCING OVERLOADS                    ==
// ===================================================================

entt::entity SceneBuilder::spawnMeshInstance(Scene& scene, MeshID meshId)
{
    // Calls the master function with a default (identity) transform.
    return spawnMeshInstance(scene, meshId, glm::vec3(0.0f), glm::quat(1.0f, 0, 0, 0), glm::vec3(1.0f));
}

entt::entity SceneBuilder::spawnMeshInstance(Scene& scene, MeshID meshId, const TransformComponent& transform)
{
    // Calls the master function using the values from the component.
    return spawnMeshInstance(scene, meshId, transform.translation, transform.rotation, transform.scale);
}

entt::entity SceneBuilder::spawnMeshInstance(Scene& scene, MeshID meshId, const glm::mat4& transformMatrix)
{
    // First, we need to break the matrix down into its parts.
    glm::vec3 scale, position, skew;
    glm::quat rotation;
    glm::vec4 perspective;
    glm::decompose(transformMatrix, scale, rotation, position, skew, perspective);

    // Then, we call the master function with those parts.
    return spawnMeshInstance(scene, meshId, position, rotation, scale);
}


// ===================================================================
// ==                    HIERARCHICAL SPAWNING                      ==
// ===================================================================

entt::entity SceneBuilder::spawnMeshInstanceAsChild(Scene& scene, MeshID meshId, entt::entity parent,
    const glm::vec3& localPosition,
    const glm::quat& localRotation,
    const glm::vec3& localScale)
{
    // Spawn the mesh normally using its local transform.
    auto childEntity = spawnMeshInstance(scene, meshId, localPosition, localRotation, localScale);

    if (childEntity != entt::null) {
        // If created successfully, add a ParentComponent to link it to its parent.
        // Your Transform/Physics system will then use this to calculate the final world position.
        scene.getRegistry().emplace<ParentComponent>(childEntity, parent);
    }

    return childEntity;
}


// ===================================================================
// ==                    PROCEDURAL SPAWNING                        ==
// ===================================================================

std::vector<entt::entity> SceneBuilder::spawnMeshInstanceGrid(Scene& scene, MeshID meshId,
    int countX, int countZ, float spacing,
    const glm::vec3& origin)
{
    std::vector<entt::entity> spawnedEntities;
    spawnedEntities.reserve(static_cast<size_t>(countX) * countZ);

    for (int x = 0; x < countX; ++x) {
        for (int z = 0; z < countZ; ++z) {
            glm::vec3 position = origin + glm::vec3(static_cast<float>(x) * spacing, 0.0f, static_cast<float>(z) * spacing);
            auto newEntity = spawnMeshInstance(scene, meshId, position);
            if (newEntity != entt::null) {
                spawnedEntities.push_back(newEntity);
            }
        }
    }
    return spawnedEntities;
}

std::vector<entt::entity> SceneBuilder::spawnMeshInstanceCircle(Scene& scene, MeshID meshId,
    int count, float radius,
    const glm::vec3& center)
{
    std::vector<entt::entity> spawnedEntities;
    if (count <= 0) return spawnedEntities;
    spawnedEntities.reserve(count);
    float angleStep = (2.0f * glm::pi<float>()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        float angle = static_cast<float>(i) * angleStep;
        glm::vec3 position = center + glm::vec3(cos(angle) * radius, 0.0f, sin(angle) * radius);
        auto newEntity = spawnMeshInstance(scene, meshId, position);
        if (newEntity != entt::null) {
            spawnedEntities.push_back(newEntity);
        }
    }
    return spawnedEntities;
}

std::vector<entt::entity> SceneBuilder::spawnMeshInstanceRandom(Scene& scene, MeshID meshId,
    int count, const glm::vec3& areaMin, const glm::vec3& areaMax)
{
    std::vector<entt::entity> spawnedEntities;
    spawnedEntities.reserve(count);
    for (int i = 0; i < count; ++i) {
        glm::vec3 position(randomFloat(areaMin.x, areaMax.x),
            randomFloat(areaMin.y, areaMax.y),
            randomFloat(areaMin.z, areaMax.z));
        auto newEntity = spawnMeshInstance(scene, meshId, position);
        if (newEntity != entt::null) {
            spawnedEntities.push_back(newEntity);
        }
    }
    return spawnedEntities;
}

std::vector<entt::entity> SceneBuilder::spawnOrientedRandom(
    Scene& scene, MeshID meshId, int count,
    const glm::vec3& areaMin, const glm::vec3& areaMax,
    const glm::vec3& targetZ_World,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomRollRangeDegrees)
{
    std::vector<entt::entity> out;
    out.reserve(std::max(0, count));

    glm::vec3 Z = glm::normalize(targetZ_World);
    glm::mat3 R = basisLookZ(Z);               // columns = X,Y,Z
    glm::quat baseQ = quatFromBasis(R);

    for (int i = 0; i < count; ++i) {
        glm::vec3 p(
            randomFloat(areaMin.x, areaMax.x),
            randomFloat(areaMin.y, areaMax.y),
            randomFloat(areaMin.z, areaMax.z)
        );

        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        float rr = glm::radians(randomFloat(randomRollRangeDegrees.x, randomRollRangeDegrees.y));
        glm::quat rollQ = glm::angleAxis(rr, Z);

        auto e = spawnMeshInstance(scene, meshId, p, rollQ * baseQ, glm::vec3(s));
        if (e != entt::null) out.push_back(e);
    }
    return out;
}

std::vector<entt::entity> SceneBuilder::spawnOrientedRandom(
    Scene& scene, MeshID meshId, int count,
    const glm::vec3& areaMin, const glm::vec3& areaMax,
    const glm::mat3& rotationBasisWorld,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomRollRangeDegrees)
{
    std::vector<entt::entity> out;
    out.reserve(std::max(0, count));

    glm::mat3 R = rotationBasisWorld;
    // Keep it orthonormal-ish
    glm::vec3 X = glm::normalize(R[0]);
    glm::vec3 Y = glm::normalize(R[1]);
    glm::vec3 Z = glm::normalize(R[2]);
    // Re-orthogonalize quickly
    X = glm::normalize(glm::cross(Y, Z));
    Y = glm::normalize(glm::cross(Z, X));
    R = glm::mat3(X, Y, Z);
    glm::quat baseQ = quatFromBasis(R);

    for (int i = 0; i < count; ++i) {
        glm::vec3 p(
            randomFloat(areaMin.x, areaMax.x),
            randomFloat(areaMin.y, areaMax.y),
            randomFloat(areaMin.z, areaMax.z)
        );

        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        float rr = glm::radians(randomFloat(randomRollRangeDegrees.x, randomRollRangeDegrees.y));
        glm::quat rollQ = glm::angleAxis(rr, Z);

        auto e = spawnMeshInstance(scene, meshId, p, rollQ * baseQ, glm::vec3(s));
        if (e != entt::null) out.push_back(e);
    }
    return out;
}


entt::entity SceneBuilder::spawnPrimitive(
    Scene& scene,
    Primitive type,
    const TransformComponent& transform,
    const std::string& name)
{
    auto& registry = scene.getRegistry();
    entt::entity e = registry.create();

    // Transform + tag
    registry.emplace_or_replace<TransformComponent>(e, transform);
    registry.emplace_or_replace<TagComponent>(e, name);

    // Mesh
    auto& mesh = registry.emplace_or_replace<RenderableMeshComponent>(e);
    switch (type) {
    case Primitive::Cube:      buildUnitCube(mesh.vertices, mesh.indices); registry.emplace_or_replace<TagComponent>(e, name + "_Cube"); break;
    case Primitive::Quad:      buildQuad(mesh.vertices, mesh.indices);     registry.emplace_or_replace<TagComponent>(e, name + "_Quad"); break;
    case Primitive::Cylinder:  buildCylinder(mesh.vertices, mesh.indices); registry.emplace_or_replace<TagComponent>(e, name + "_Cylinder"); break;
    case Primitive::Cone:      buildCone(mesh.vertices, mesh.indices);     registry.emplace_or_replace<TagComponent>(e, name + "_Cone"); break;
    case Primitive::Torus:     buildTorus(mesh.vertices, mesh.indices);    registry.emplace_or_replace<TagComponent>(e, name + "_Torus"); break;
    case Primitive::IcoSphere: buildIcoSphere(mesh.vertices, mesh.indices); registry.emplace_or_replace<TagComponent>(e, name + "_IcoSphere"); break;
    }

    // Material tag parity with imported meshes: prefer tri-planar when UVs/tangents are absent
    registry.emplace_or_replace<TriPlanarMaterialTag>(e);

    return e;
}

// ===================================================================
// ==                     ADVANCED SPAWNING                         ==
// ===================================================================

std::vector<entt::entity> SceneBuilder::spawnOnSurface(Scene& scene, MeshID meshId, int count,
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    const SurfaceQueryFunction& queryFunc,
    bool alignToNormal, float maxSlopeAngleDegrees,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees)
{
    std::vector<entt::entity> spawnedEntities;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const float minDotForSlope = cos(glm::radians(maxSlopeAngleDegrees));
    int attempts = 0;
    const int maxAttempts = count * 5; // Try a max of 5 times per requested object to avoid infinite loops

    while (spawnedEntities.size() < count && attempts < maxAttempts) {
        attempts++;
        // 1. Pick a random spot from a top-down perspective.
        float randX = randomFloat(boundsMin.x, boundsMax.x);
        float randZ = randomFloat(boundsMin.z, boundsMax.z);

        // 2. Cast a ray straight down from the top of the bounding box.
        Ray ray = { glm::vec3(randX, boundsMax.y, randZ), glm::vec3(0.0f, -1.0f, 0.0f) };
        auto hit = queryFunc(ray.origin, ray.direction);

        if (hit) {
            // 3. Check if the surface slope is acceptable.
            if (glm::dot(hit->normal, worldUp) >= minDotForSlope) {
                // 4. Calculate random scale and rotation.
                float scale = randomFloat(randomScaleRange.x, randomScaleRange.y);
                float yaw = glm::radians(randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y));
                glm::quat randomYaw = glm::angleAxis(yaw, worldUp);

                glm::quat finalRotation = randomYaw;
                if (alignToNormal) {
                    // Create a rotation that aligns the object's Up vector (0,1,0) with the surface normal.
                    glm::quat alignment = glm::rotation(worldUp, hit->normal);
                    finalRotation = alignment * randomYaw;
                }

                auto newEntity = spawnMeshInstance(scene, meshId, hit->position, finalRotation, glm::vec3(scale));
                if (newEntity != entt::null) {
                    spawnedEntities.push_back(newEntity);
                }
            }
        }
    }
    return spawnedEntities;
}

std::vector<entt::entity> SceneBuilder::spawnOrientedRandomOnSurface(
    Scene& scene, MeshID meshId, int count,
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    const SurfaceQueryFunction& queryFunc,
    const glm::vec3& modelUp_Local,
    const glm::vec3& surfaceUp_World,
    float upToleranceDegrees,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    bool alignToSurfaceNormal)
{
    std::vector<entt::entity> out;
    out.reserve(std::max(0, count));

    const glm::vec3 worldUp = glm::normalize(surfaceUp_World);
    const float cosTol = std::cos(glm::radians(std::max(0.0f, upToleranceDegrees)));

    // Precompute mesh half extents to lift by local up
    const glm::vec3 he = meshLocalHalfExtents(meshId);
    const glm::vec3 mUpL = glm::normalize(modelUp_Local);

    int attempts = 0, maxAttempts = std::max(count * 10, 100);

    while ((int)out.size() < count && attempts++ < maxAttempts) {
        // 1) sample horizontal within bounds (project onto plane orthogonal to surfaceUp)
        float rx = randomFloat(boundsMin.x, boundsMax.x);
        float rz = randomFloat(boundsMin.z, boundsMax.z);
        // We’ll cast from the top bound along -surfaceUp
        glm::vec3 castOrigin = glm::vec3(rx, boundsMax.y, rz);
        glm::vec3 castDir = -worldUp;

        // 2) raycast
        auto hit = queryFunc(castOrigin, castDir);
        if (!hit) continue;

        // 3) up tolerance check: only accept surfaces whose normal is near surfaceUp
        glm::vec3 n = glm::normalize(hit->normal);
        if (glm::dot(n, worldUp) < cosTol) continue;

        // 4) build rotation
        // Choose the "target up" we will align modelUp to
        glm::vec3 targetUp = alignToSurfaceNormal ? n : worldUp;

        // Create a basis where model local +Y (or provided modelUp) maps to targetUp
        // We'll use "lookZ" with Z from a sideways yaw, then swap to align up
        // Better: directly rotate modelUp_Local to targetUp.
        glm::quat alignQ = glm::rotation(mUpL, targetUp); // local->world up

        // Add random yaw about the chosen up axis
        float yaw = glm::radians(randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y));
        glm::quat yawQ = glm::angleAxis(yaw, targetUp);

        glm::quat finalQ = yawQ * alignQ;

        // 5) choose scale
        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        glm::vec3 scale(s);

        // 6) lift by half-extent along modelUp, which now maps to targetUp
        // Local AABB extent along modelUp = dot(|modelUp|, halfExtents)
        glm::vec3 absUpL = glm::abs(glm::normalize(mUpL));
        float halfAlongUpLocal = he.x * absUpL.x + he.y * absUpL.y + he.z * absUpL.z;
        float lift = halfAlongUpLocal * s; // uniform scale

        glm::vec3 pos = hit->position + targetUp * lift;

        // 7) spawn
        auto e = spawnMeshInstance(scene, meshId, pos, finalQ, scale);
        if (e != entt::null) out.push_back(e);
    }

    return out;
}

// ---- Grid in box ----
std::vector<entt::entity> SceneBuilder::spawnGridInBox(
    Scene& scene, MeshID meshId,
    const glm::vec3& boxMin, const glm::vec3& boxMax,
    const glm::ivec3& counts,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    const glm::mat3* worldBasisOptional)
{
    std::vector<entt::entity> out;
    if (counts.x <= 0 || counts.y <= 0 || counts.z <= 0) return out;

    glm::vec3 step = (boxMax - boxMin) / glm::vec3(counts - glm::ivec3(1));
    glm::mat3 B = worldBasisOptional ? *worldBasisOptional : glm::mat3(1);

    for (int ix = 0; ix < counts.x; ++ix) {
        for (int iy = 0; iy < counts.y; ++iy) {
            for (int iz = 0; iz < counts.z; ++iz) {
                glm::vec3 p = boxMin + glm::vec3(ix * step.x, iy * step.y, iz * step.z);
                float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
                float yaw = glm::radians(randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y));
                glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
                glm::mat3 R = glm::mat3_cast(q) * B;

                auto e = spawnMeshInstance(scene, meshId, p, glm::quat_cast(R), glm::vec3(s));
                if (e != entt::null) out.push_back(e);
            }
        }
    }
    return out;
}

// ---- Grid on surface (raycast) ----
std::vector<entt::entity> SceneBuilder::spawnGridOnSurface(
    Scene& scene,
    MeshID meshId,
    const glm::vec2& areaMinXZ,
    const glm::vec2& areaMaxXZ,
    const glm::ivec2& countsXZ,
    const SurfaceQueryFunction& queryFunc,
    const glm::vec3& modelUp_Local,
    const glm::vec3& surfaceUp_World,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    bool alignToSurfaceNormal)
{
    std::vector<entt::entity> out;
    if (countsXZ.x <= 0 || countsXZ.y <= 0) return out;

    glm::vec2 step = (areaMaxXZ - areaMinXZ) / glm::vec2(countsXZ - glm::ivec2(1));
    glm::vec3 he = meshLocalHalfExtents(meshId);
    glm::vec3 wUp = glm::normalize(surfaceUp_World);
    glm::vec3 mUpL = glm::normalize(modelUp_Local);
    float cosTol = std::cos(glm::radians(90.0f)); // accept any facing in this API

    for (int ix = 0; ix < countsXZ.x; ++ix) {
        for (int iz = 0; iz < countsXZ.y; ++iz) {
            glm::vec2 xz = areaMinXZ + glm::vec2(ix * step.x, iz * step.y);
            glm::vec3 origin(xz.x, 1e6f, xz.y);
            glm::vec3 dir = -wUp;

            auto hit = queryFunc(origin, dir);
            if (!hit) continue;

            glm::vec3 n = glm::normalize(hit->normal);
            if (glm::dot(n, wUp) < cosTol) continue;

            glm::vec3 targetUp = alignToSurfaceNormal ? n : wUp;
            glm::quat alignQ = glm::rotation(mUpL, targetUp);

            float yawDeg = randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y);
            glm::quat yawQ = glm::angleAxis(glm::radians(yawDeg), targetUp);
            glm::quat q = yawQ * alignQ;

            float s = randomFloat(randomScaleRange.x, randomScaleRange.y);

            // lift by half extent along modelUp mapped axis
            glm::vec3 absUpL = glm::abs(mUpL);
            float halfAlongUpLocal = he.x * absUpL.x + he.y * absUpL.y + he.z * absUpL.z;
            glm::vec3 pos = hit->position + targetUp * (halfAlongUpLocal * s);

            auto e = spawnMeshInstance(scene, meshId, pos, q, glm::vec3(s));
            if (e != entt::null) out.push_back(e);
        }
    }
    return out;
}

// ---- Poisson disk 2D on surface ----
std::vector<entt::entity> SceneBuilder::spawnPoissonDisk2DOnSurface(
    Scene& scene,
    MeshID meshId,
    const glm::vec2& areaMinXZ,
    const glm::vec2& areaMaxXZ,
    float minDistance,
    int   newPointTries,
    int   maxInstances,
    const SurfaceQueryFunction& queryFunc,
    const glm::vec3& modelUp_Local,
    const glm::vec3& surfaceUp_World,
    float upToleranceDegrees,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    bool alignToSurfaceNormal,
    DensityFn2D densityFn)
{
    std::vector<entt::entity> out;
    if (minDistance <= 0.0f || maxInstances <= 0) return out;

    const glm::vec2 size = areaMaxXZ - areaMinXZ;
    const float cell = minDistance / glm::sqrt(2.0f);
    const glm::ivec2 gridSize = glm::max(glm::ivec2(1), glm::ivec2(glm::ceil(size / cell)));

    std::vector<int> grid(gridSize.x * gridSize.y, -1);
    std::vector<glm::vec2> points;
    points.reserve(maxInstances);

    auto gridIndex = [&](const glm::vec2& p)->glm::ivec2 {
        glm::vec2 rel = (p - areaMinXZ) / size;
        glm::ivec2 idx = glm::clamp(glm::ivec2(rel * glm::vec2(gridSize)), glm::ivec2(0), gridSize - glm::ivec2(1));
        return idx;
        };
    auto fits = [&](const glm::vec2& p)->bool {
        glm::ivec2 gi = gridIndex(p);
        for (int dz = -2; dz <= 2; ++dz) for (int dx = -2; dx <= 2; ++dx) {
            glm::ivec2 nb = gi + glm::ivec2(dx, dz);
            if (nb.x < 0 || nb.y < 0 || nb.x >= gridSize.x || nb.y >= gridSize.y) continue;
            int id = grid[nb.y * gridSize.x + nb.x];
            if (id >= 0 && glm::length(points[id] - p) < minDistance) return false;
        }
        return true;
        };
    auto pushPoint = [&](const glm::vec2& p) {
        glm::ivec2 gi = gridIndex(p);
        int id = (int)points.size();
        points.push_back(p);
        grid[gi.y * gridSize.x + gi.x] = id;
        };

    // Seed
    glm::vec2 seed(randomFloat(areaMinXZ.x, areaMaxXZ.x), randomFloat(areaMinXZ.y, areaMaxXZ.y));
    pushPoint(seed);
    std::vector<int> active = { 0 };

    // Bridson
    while (!active.empty() && (int)points.size() < maxInstances) {
        int idx = active[randomInt(0, (int)active.size() - 1)];
        glm::vec2 base = points[idx];
        bool found = false;

        for (int k = 0; k < newPointTries; ++k) {
            float r = randomFloat(minDistance, 2.0f * minDistance);
            float a = randomFloat(0.0f, glm::two_pi<float>());
            glm::vec2 cand = base + r * glm::vec2(std::cos(a), std::sin(a));
            if (cand.x < areaMinXZ.x || cand.x > areaMaxXZ.x ||
                cand.y < areaMinXZ.y || cand.y > areaMaxXZ.y) continue;

            if (densityFn) {
                float relx = (cand.x - areaMinXZ.x) / size.x;
                float relz = (cand.y - areaMinXZ.y) / size.y;
                if (randomFloat(0.0f, 1.0f) > glm::clamp(densityFn(glm::vec2(relx, relz)), 0.0f, 1.0f))
                    continue;
            }

            if (fits(cand)) { pushPoint(cand); active.push_back((int)points.size() - 1); found = true; break; }
        }
        if (!found) {
            active.erase(std::remove(active.begin(), active.end(), idx), active.end());
        }
    }

    // Project to surface
    glm::vec3 wUp = glm::normalize(surfaceUp_World);
    glm::vec3 mUpL = glm::normalize(modelUp_Local);
    float cosTol = std::cos(glm::radians(std::max(0.0f, upToleranceDegrees)));
    glm::vec3 he = meshLocalHalfExtents(meshId);

    for (auto& xz : points) {
        glm::vec3 origin(xz.x, 1e6f, xz.y);
        glm::vec3 dir = -wUp;

        auto hit = queryFunc(origin, dir);
        if (!hit) continue;
        glm::vec3 n = glm::normalize(hit->normal);
        if (glm::dot(n, wUp) < cosTol) continue;

        glm::vec3 targetUp = alignToSurfaceNormal ? n : wUp;
        glm::quat alignQ = glm::rotation(mUpL, targetUp);
        float yawDeg = randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y);
        glm::quat yawQ = glm::angleAxis(glm::radians(yawDeg), targetUp);
        glm::quat q = yawQ * alignQ;

        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        glm::vec3 absUpL = glm::abs(mUpL);
        float halfAlongUpLocal = he.x * absUpL.x + he.y * absUpL.y + he.z * absUpL.z;
        glm::vec3 pos = hit->position + targetUp * (halfAlongUpLocal * s);

        auto e = spawnMeshInstance(scene, meshId, pos, q, glm::vec3(s));
        if (e != entt::null) out.push_back(e);
    }
    return out;
}

// ---- Clusters on surface ----
std::vector<entt::entity> SceneBuilder::spawnClustersOnSurface(
    Scene& scene,
    MeshID meshId,
    int clusterCount,
    int minPerCluster,
    int maxPerCluster,
    float clusterRadius,
    const glm::vec2& areaMinXZ,
    const glm::vec2& areaMaxXZ,
    const SurfaceQueryFunction& queryFunc,
    const glm::vec3& modelUp_Local,
    const glm::vec3& surfaceUp_World,
    float upToleranceDegrees,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    bool alignToSurfaceNormal)
{
    std::vector<entt::entity> out;
    if (clusterCount <= 0) return out;

    // Choose cluster centers via Poisson-like spacing
    auto centers = spawnPoissonDisk2DOnSurface(
        scene, meshId, areaMinXZ, areaMaxXZ,
        clusterRadius * 0.75f, 15, clusterCount, // spacing a bit smaller than radius
        queryFunc, modelUp_Local, surfaceUp_World, upToleranceDegrees,
        { 1.0f,1.0f }, { 0.0f,0.0f }, alignToSurfaceNormal);

    // Extract positions of their first entity to use as centers
    std::vector<glm::vec3> centerPos;
    centerPos.reserve(centers.size());
    auto& reg = scene.getRegistry();
    for (auto e : centers) {
        if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) continue;
        centerPos.push_back(reg.get<TransformComponent>(e).translation);
    }

    // For each center, populate around with gaussian jitter (raycast down again)
    glm::vec3 wUp = glm::normalize(surfaceUp_World);
    glm::vec3 mUpL = glm::normalize(modelUp_Local);
    float cosTol = std::cos(glm::radians(std::max(0.0f, upToleranceDegrees)));
    glm::vec3 he = meshLocalHalfExtents(meshId);

    std::normal_distribution<float> gauss(0.0f, clusterRadius * 0.35f);

    for (const auto& c : centerPos) {
        int n = randomInt(minPerCluster, maxPerCluster);
        for (int i = 0; i < n; ++i) {
            glm::vec2 offset(gauss(rng()), gauss(rng()));
            glm::vec2 xz(c.x + offset.x, c.z + offset.y);
            glm::vec3 origin(xz.x, 1e6f, xz.y);
            glm::vec3 dir = -wUp;

            auto hit = queryFunc(origin, dir);
            if (!hit) continue;
            glm::vec3 nrm = glm::normalize(hit->normal);
            if (glm::dot(nrm, wUp) < cosTol) continue;

            glm::vec3 targetUp = alignToSurfaceNormal ? nrm : wUp;
            glm::quat alignQ = glm::rotation(mUpL, targetUp);
            float yawDeg = randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y);
            glm::quat yawQ = glm::angleAxis(glm::radians(yawDeg), targetUp);
            glm::quat q = yawQ * alignQ;

            float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
            glm::vec3 absUpL = glm::abs(mUpL);
            float halfAlongUpLocal = he.x * absUpL.x + he.y * absUpL.y + he.z * absUpL.z;
            glm::vec3 pos = hit->position + targetUp * (halfAlongUpLocal * s);

            auto e = spawnMeshInstance(scene, meshId, pos, q, glm::vec3(s));
            if (e != entt::null) out.push_back(e);
        }
    }
    return out;
}

// ---- Along polyline ----
std::vector<entt::entity> SceneBuilder::spawnAlongPolyline(
    Scene& scene,
    MeshID meshId,
    const std::vector<glm::vec3>& points,
    float spacing,
    const glm::vec3& modelForward_Local,
    const glm::vec3& worldUp,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomRollRangeDegrees)
{
    std::vector<entt::entity> out;
    if (points.size() < 2 || spacing <= 0.0f) return out;

    glm::vec3 wU = glm::normalize(worldUp);

    float distAccum = 0.0f;
    glm::vec3 last = points.front();

    auto place = [&](const glm::vec3& pos, const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::mat3 R = basisLookDir(fwd, wU);
        glm::quat q = quatFromBasis(R);
        float roll = glm::radians(randomFloat(randomRollRangeDegrees.x, randomRollRangeDegrees.y));
        q = glm::angleAxis(roll, fwd) * q;
        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        auto e = spawnMeshInstance(scene, meshId, pos, q, glm::vec3(s));
        if (e != entt::null) out.push_back(e);
        };

    for (size_t i = 1; i < points.size(); ++i) {
        glm::vec3 a = last, b = points[i];
        glm::vec3 ab = b - a;
        float seg = glm::length(ab);
        if (seg < 1e-6f) continue;
        glm::vec3 dir = ab / seg;

        float t = 0.0f;
        while (distAccum + spacing <= seg + 1e-6f) {
            float step = spacing - distAccum;
            t += step / seg;
            glm::vec3 p = glm::mix(a, b, t);
            place(p, dir);
            distAccum = 0.0f;
            a = p; seg = glm::length(b - a);
            if (seg < 1e-6f) break;
        }
        distAccum = glm::clamp(spacing - seg, 0.0f, spacing);
        last = b;
    }
    return out;
}

// ---- Along Bezier ----
std::vector<entt::entity> SceneBuilder::spawnAlongBezier(
    Scene& scene,
    MeshID meshId,
    const glm::vec3& p0,
    const glm::vec3& p1,
    const glm::vec3& p2,
    const glm::vec3& p3,
    int   count,
    const glm::vec3& modelForward_Local,
    const glm::vec3& worldUp,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomRollRangeDegrees)
{
    std::vector<entt::entity> out;
    if (count <= 0) return out;

    glm::vec3 wU = glm::normalize(worldUp);
    for (int i = 0; i < count; ++i) {
        float t = (count == 1) ? 0.5f : (float(i) / (count - 1));
        glm::vec3 p = bezier(p0, p1, p2, p3, t);
        glm::vec3 d = bezierTangent(p0, p1, p2, p3, t);
        if (glm::length2(d) < 1e-8f) d = glm::vec3(0, 0, 1);
        glm::mat3 R = basisLookDir(glm::normalize(d), wU);
        glm::quat q = quatFromBasis(R);
        float roll = glm::radians(randomFloat(randomRollRangeDegrees.x, randomRollRangeDegrees.y));
        q = glm::angleAxis(roll, glm::normalize(d)) * q;
        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        auto e = spawnMeshInstance(scene, meshId, p, q, glm::vec3(s));
        if (e != entt::null) out.push_back(e);
    }
    return out;
}

// ---- Stacks on surface ----
std::vector<entt::entity> SceneBuilder::spawnStacksOnSurface(
    Scene& scene,
    MeshID meshId,
    int   stackCount,
    const glm::ivec2& stackHeightRange,
    const glm::vec2& areaMinXZ,
    const glm::vec2& areaMaxXZ,
    const SurfaceQueryFunction& queryFunc,
    const glm::vec3& modelUp_Local,
    const glm::vec3& surfaceUp_World,
    float upToleranceDegrees,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    bool alignToSurfaceNormal)
{
    std::vector<entt::entity> out;
    if (stackCount <= 0) return out;

    glm::vec3 wUp = glm::normalize(surfaceUp_World);
    glm::vec3 mUpL = glm::normalize(modelUp_Local);
    float cosTol = std::cos(glm::radians(std::max(0.0f, upToleranceDegrees)));
    glm::vec3 he = meshLocalHalfExtents(meshId);

    for (int k = 0; k < stackCount; ++k) {
        // find a base
        glm::vec2 xz(randomFloat(areaMinXZ.x, areaMaxXZ.x), randomFloat(areaMinXZ.y, areaMaxXZ.y));
        glm::vec3 origin(xz.x, 1e6f, xz.y);
        glm::vec3 dir = -wUp;
        auto hit = queryFunc(origin, dir);
        if (!hit) continue;
        glm::vec3 n = glm::normalize(hit->normal);
        if (glm::dot(n, wUp) < cosTol) continue;

        glm::vec3 targetUp = alignToSurfaceNormal ? n : wUp;
        glm::quat alignQ = glm::rotation(mUpL, targetUp);
        float yawDeg = randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y);
        glm::quat yawQ = glm::angleAxis(glm::radians(yawDeg), targetUp);
        glm::quat qBase = yawQ * alignQ;

        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        glm::vec3 absUpL = glm::abs(mUpL);
        float halfAlongUpLocal = he.x * absUpL.x + he.y * absUpL.y + he.z * absUpL.z;
        glm::vec3 basePos = hit->position + targetUp * (halfAlongUpLocal * s);

        // number in this stack
        int h = glm::clamp(randomInt(stackHeightRange.x, stackHeightRange.y), 1, 1000);
        glm::vec3 step = targetUp * (2.0f * halfAlongUpLocal * s);

        for (int i = 0; i < h; ++i) {
            glm::vec3 pos = basePos + float(i) * step;
            auto e = spawnMeshInstance(scene, meshId, pos, qBase, glm::vec3(s));
            if (e != entt::null) out.push_back(e);
        }
    }
    return out;
}

// ---- Non-overlapping in box (sphere approx) ----
std::vector<entt::entity> SceneBuilder::spawnInBoxNoOverlap(
    Scene& scene,
    MeshID meshId,
    int count,
    const glm::vec3& boxMin,
    const glm::vec3& boxMax,
    float minCenterDistance,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    const glm::mat3* worldBasisOptional)
{
    std::vector<entt::entity> out;
    if (count <= 0) return out;

    std::vector<glm::vec3> placed;
    placed.reserve(count);

    glm::mat3 B = worldBasisOptional ? *worldBasisOptional : glm::mat3(1);
    int attempts = 0, maxAttempts = count * 100;

    while ((int)placed.size() < count && attempts++ < maxAttempts) {
        glm::vec3 p(randomFloat(boxMin.x, boxMax.x),
            randomFloat(boxMin.y, boxMax.y),
            randomFloat(boxMin.z, boxMax.z));
        bool ok = true;
        for (auto& q : placed) {
            if (glm::distance2(p, q) < minCenterDistance * minCenterDistance) { ok = false; break; }
        }
        if (!ok) continue;

        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        float yaw = glm::radians(randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y));
        glm::quat qrot = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        glm::mat3 R = glm::mat3_cast(qrot) * B;

        auto e = spawnMeshInstance(scene, meshId, p, glm::quat_cast(R), glm::vec3(s));
        if (e != entt::null) {
            out.push_back(e);
            placed.push_back(p);
        }
    }
    return out;
}

// ---- From points (with jitter) ----
std::vector<entt::entity> SceneBuilder::spawnFromPoints(
    Scene& scene,
    MeshID meshId,
    const std::vector<glm::vec3>& anchors,
    const glm::vec3& jitterXYZ,
    const glm::vec2& randomScaleRange,
    const glm::vec2& randomYawRangeDegrees,
    const glm::mat3* worldBasisOptional)
{
    std::vector<entt::entity> out; out.reserve(anchors.size());
    glm::mat3 B = worldBasisOptional ? *worldBasisOptional : glm::mat3(1);

    for (auto a : anchors) {
        glm::vec3 p = a + glm::vec3(randomFloat(-jitterXYZ.x, jitterXYZ.x),
            randomFloat(-jitterXYZ.y, jitterXYZ.y),
            randomFloat(-jitterXYZ.z, jitterXYZ.z));
        float s = randomFloat(randomScaleRange.x, randomScaleRange.y);
        float yaw = glm::radians(randomFloat(randomYawRangeDegrees.x, randomYawRangeDegrees.y));
        glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        glm::mat3 R = glm::mat3_cast(q) * B;

        auto e = spawnMeshInstance(scene, meshId, p, glm::quat_cast(R), glm::vec3(s));
        if (e != entt::null) out.push_back(e);
    }
    return out;
}

std::vector<entt::entity> SceneBuilder::spawnInClumps(Scene& scene, const std::vector<MeshID>& meshIds,
    const std::vector<float>& weights,
    int clumpCount, int avgObjectsPerClump,
    float clumpRadius, const glm::vec3& areaMin,
    const glm::vec3& areaMax, float randomRotationStrength)
{
    std::vector<entt::entity> spawnedEntities;
    if (meshIds.empty() || meshIds.size() != weights.size()) {
        qWarning() << "spawnInClumps failed: MeshIDs and weights must be non-empty and have the same size.";
        return spawnedEntities;
    }

    // This distribution uses the weights to correctly pick a random mesh index.
    std::discrete_distribution<> weightedPicker(weights.begin(), weights.end());

    for (int i = 0; i < clumpCount; ++i) {
        // 1. Pick a center for the clump.
        glm::vec3 clumpCenter(randomFloat(areaMin.x, areaMax.x),
            randomFloat(areaMin.y, areaMax.y),
            randomFloat(areaMin.z, areaMax.z));

        // 2. Spawn a variable number of objects in the clump for a more natural look.
        int objectsInThisClump = avgObjectsPerClump / 2 + (getRandomGenerator()() % avgObjectsPerClump);

        for (int j = 0; j < objectsInThisClump; ++j) {
            // 3. Pick a random mesh type based on the provided weights.
            MeshID chosenMeshId = meshIds[weightedPicker(getRandomGenerator())];

            // 4. Find a random position within the clump's radius.
            float angle = randomFloat(0, 2.0f * glm::pi<float>());
            float radius = randomFloat(0, clumpRadius);
            glm::vec3 position = clumpCenter + glm::vec3(cos(angle) * radius, 0.0f, sin(angle) * radius);

            // 5. Calculate rotation, blending between "upright" and "fully random".
            glm::quat upright = glm::quat(1.0f, 0, 0, 0);
            glm::quat random = glm::normalize(glm::quat(randomFloat(0, 1), randomFloat(0, 1), randomFloat(0, 1), randomFloat(0, 1)));
            glm::quat finalRotation = glm::slerp(upright, random, randomRotationStrength);

            auto newEntity = spawnMeshInstance(scene, chosenMeshId, position, finalRotation);
            if (newEntity != entt::null) {
                spawnedEntities.push_back(newEntity);
            }
        }
    }
    return spawnedEntities;
}


// ===================================================================
// ==                       BATCH SPAWNING                          ==
// ===================================================================

std::vector<entt::entity> SceneBuilder::executeSpawnList(Scene& scene, const std::vector<SpawnCommand>& commands)
{
    auto& registry = scene.getRegistry();
    std::vector<entt::entity> spawnedEntities;
    spawnedEntities.reserve(commands.size());

    // This map is crucial for resolving parent-child relationships within the same batch.
    std::unordered_map<int, entt::entity> commandIndexToEntity;

    // First pass: Create all entities without parenting.
    for (int i = 0; i < commands.size(); ++i) {
        const auto& cmd = commands[i];
        entt::entity newEntity = spawnMeshInstance(scene, cmd.meshId, cmd.transform);

        if (newEntity != entt::null) {
            spawnedEntities.push_back(newEntity);
            commandIndexToEntity[i] = newEntity; // Map the command's index to the new entity.
        }
    }

    // Second pass: Apply parenting and overrides.
    for (int i = 0; i < commands.size(); ++i) {
        const auto& cmd = commands[i];
        auto it = commandIndexToEntity.find(i);
        if (it == commandIndexToEntity.end()) continue; // Skip if this entity failed to spawn.

        entt::entity currentEntity = it->second;

        if (cmd.parent.has_value()) {
            registry.emplace<ParentComponent>(currentEntity, *cmd.parent);
        }

        if (cmd.materialOverride.has_value()) {
            registry.emplace_or_replace<MaterialComponent>(currentEntity, *cmd.materialOverride);
        }
    }

    return spawnedEntities;
}

entt::entity SceneBuilder::createCamera(Scene& scene,
    const glm::vec3& position,
    const glm::vec3& colour)
{
    auto& registry = scene.getRegistry();

    // 1 – Create the main camera entity (which is invisible).
    entt::entity camE = registry.create();
    registry.emplace<CameraComponent>(camE, colour);
    registry.emplace<TransformComponent>(camE, position);
    registry.emplace<TagComponent>(camE, "Camera");

    // 2 – Spawn the visible camera model (gizmo) as a child of the main entity.
    MeshID gizmoMeshId = ResourceManager::instance().loadMesh(":/external/miniViewportCamera.stl");
    if (gizmoMeshId != MeshID::None) {
        glm::vec3 localPos(0.0f, 0.0f, -0.35f);
        glm::quat localRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)) *
            glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
        glm::vec3 localScale(0.12f);

        auto gizmoEntity = spawnMeshInstanceAsChild(scene, gizmoMeshId, camE, localPos, localRot, localScale);
        if (gizmoEntity != entt::null) {
            registry.emplace<CameraGizmoTag>(gizmoEntity);

            // --- THE FIX ---
            // 1. Create a default material component first.
            auto& gizmoMaterial = registry.emplace<MaterialComponent>(gizmoEntity);
            // 2. Explicitly set the albedo color. This is unambiguous and safe.
            gizmoMaterial.albedoColor = colour;
        }
    }

    // 3 – Spawn the blinking "REC" LED as a child of the GIZMO.
    entt::entity ledE = registry.create();
    registry.emplace<ParentComponent>(ledE, camE);
    registry.emplace<RecordLedTag>(ledE);
    registry.emplace<PulsingLightComponent>(ledE, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.3f, 0.0f, 0.0f), 6.0f);
    registry.emplace<TransformComponent>(ledE, glm::vec3(0.1f, -0.115f, 0.275f), glm::quat(), glm::vec3(0.1f));

    auto& ledMesh = registry.emplace<RenderableMeshComponent>(ledE);
    buildIcoSphere(ledMesh.vertices, ledMesh.indices);
    auto& ledMaterial = registry.emplace<MaterialComponent>(ledE);
    ledMaterial.albedoColor = glm::vec3(1.0f, 0.0f, 0.0f);

    return camE;
}

void SceneBuilder::spawnRobot(Scene& scene, const RobotDescription& description)
{
    auto& registry = scene.getRegistry();

    // Clean up only the entities that are part of the old robot.
    auto view = registry.view<LinkComponent>();
    registry.destroy(view.begin(), view.end());

    std::unordered_map<std::string, entt::entity> linkNameToEntity;

    // First pass: Create all link entities and their meshes.
    for (const auto& linkDesc : description.links)
    {
        entt::entity linkEntity = entt::null;

        // Use the SceneBuilder to spawn the mesh. This handles all loading, caching,
        // and material tag logic automatically.
        if (!linkDesc.mesh_filepath.empty()) {
            linkEntity = spawnMesh(scene, QString::fromStdString(linkDesc.mesh_filepath));
        }
        else {
            // If there's no mesh, create an empty entity to serve as a transform node.
            linkEntity = registry.create();
            registry.emplace<TransformComponent>(linkEntity);
        }

        if (linkEntity != entt::null) {
            linkNameToEntity[linkDesc.name] = linkEntity;
            // Add the specific robot-related components.
            registry.emplace_or_replace<TagComponent>(linkEntity, linkDesc.name);
            registry.emplace<LinkComponent>(linkEntity, linkDesc);
        }
    }

    // Second pass: Connect the links with joints by setting up the parent-child hierarchy.
    for (const auto& jointDesc : description.joints)
    {
        entt::entity parentEntity = entt::null;
        entt::entity childEntity = entt::null;

        if (linkNameToEntity.count(jointDesc.parent_link_name)) {
            parentEntity = linkNameToEntity.at(jointDesc.parent_link_name);
        }
        if (linkNameToEntity.count(jointDesc.child_link_name)) {
            childEntity = linkNameToEntity.at(jointDesc.child_link_name);
        }

        if (registry.valid(parentEntity) && registry.valid(childEntity))
        {
            // Add the parent component to the child.
            registry.emplace<ParentComponent>(childEntity, parentEntity);

            // Set the child's LOCAL transform relative to the parent.
            auto& childTransform = registry.get<TransformComponent>(childEntity);
            childTransform.translation = jointDesc.origin_xyz;
            // Note: You would also apply the joint's rotation here.
        }
    }
}

// --- Spline creation functions (no changes needed) ---

entt::entity SceneBuilder::makeCR(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::CatmullRom;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeParam(entt::registry& r,
    std::function<glm::vec3(float)> f,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    entt::entity e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Parametric;
    sp.parametric.func = std::move(f);
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeLinear(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Linear;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeBezier(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Bezier;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

/**
 * @brief Spawns a procedurally generated primitive mesh as a child of another entity.
 * @param scene The scene to spawn the entity in.
 * @param type The type of primitive to generate (e.g., Cube, Cylinder).
 * @param parent The entity that will be the parent of the new primitive.
 * @return The newly created entity handle.
 */
entt::entity SceneBuilder::spawnPrimitiveAsChild(Scene& scene, Primitive type, entt::entity parent)
{
    auto& registry = scene.getRegistry();

    // 1. Create a new entity that will hold our primitive mesh.
    auto newEntity = registry.create();

    // 2. Add the core components that every scene object needs.
    registry.emplace<TransformComponent>(newEntity);
    registry.emplace<ParentComponent>(newEntity, parent);

    // 3. Create a mesh component and populate it based on the requested primitive type.
    auto& meshComp = registry.emplace<RenderableMeshComponent>(newEntity);

    switch (type)
    {
    case Primitive::Cube:
        buildUnitCube(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_Cube");
        break;
    case Primitive::Quad:
        buildQuad(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_Quad");
        break;
    case Primitive::Cylinder:
        buildCylinder(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_Cylinder");
        break;
    case Primitive::Cone:
        buildCone(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_Cone");
        break;
    case Primitive::Torus:
        // Using default parameters for the torus here.
        buildTorus(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_Torus");
        break;
    case Primitive::IcoSphere:
        buildIcoSphere(meshComp.vertices, meshComp.indices);
        registry.emplace<TagComponent>(newEntity, "Gizmo_IcoSphere");
        break;
    }

    // 4. Since these are simple primitives, we'll tag them for tri-planar mapping by default.
    registry.emplace<TriPlanarMaterialTag>(newEntity);

    return newEntity;
}
