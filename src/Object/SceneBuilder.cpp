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
