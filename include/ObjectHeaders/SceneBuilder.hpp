// In SceneBuilder.hpp
#include "RobotDescription.hpp"
#include "Scene.hpp" // Needs to know about the scene
#include "Types.hpp"
#include "Components.hpp" // Needs to know about components it will add
#include "PrimitiveBuilders.hpp"
#include "SceneQuery.hpp"
#include <vector>          
#include <functional>

struct RobotDescription;
class Scene;
class ResourceManager;

struct SpawnCommand {
    MeshID meshId = MeshID::None;
    TransformComponent transform = {}; // Default transform (position 0,0,0, no rotation, scale 1)

    // Optional: If this entity should be a child of another.
    // Note: The parent entity must already exist when this command is processed.
    std::optional<entt::entity> parent = std::nullopt;

    // Optional: If you want to override the default material for this specific instance.
    std::optional<MaterialComponent> materialOverride = std::nullopt;
};

// A static utility class for populating a Scene from a RobotDescription.
// This decouples the scene creation logic from the UI and parsers.
class SceneBuilder
{
public:
    // Takes a scene and a robot description, and creates all the necessary
    // entities and components in the scene's registry.
    static void spawnRobot(Scene& scene, const RobotDescription& description);

    static entt::entity spawnPrimitiveAsChild(Scene& scene, Primitive type, entt::entity parent);

    static entt::entity spawnMesh(Scene& scene, const QString& meshPath,
        const TransformComponent& transform = TransformComponent{});

    static entt::entity spawnMesh(Scene& scene, const QString& meshPath,
        const glm::vec3& position,
        const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    // --- Spawning from MeshID (Faster, for instancing) ---
    // These functions assume the mesh is already loaded and work with its ID.

    // This is our "MASTER" function. All other overloads will call this one.
    static entt::entity spawnMeshInstance(Scene& scene, MeshID meshId,
        const glm::vec3& position,
        const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    // Convenience overloads that chain to the master function.
    static entt::entity spawnMeshInstance(Scene& scene, MeshID meshId);
    static entt::entity spawnMeshInstance(Scene& scene, MeshID meshId, const TransformComponent& transform);
    static entt::entity spawnMeshInstance(Scene& scene, MeshID meshId, const glm::mat4& transformMatrix);

    // --- Hierarchical Spawning ---
    static entt::entity spawnMeshInstanceAsChild(Scene& scene, MeshID meshId, entt::entity parent,
        const glm::vec3& localPosition = glm::vec3(0.0f),
        const glm::quat& localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        const glm::vec3& localScale = glm::vec3(1.0f));

    // --- Procedural Spawning ---
    static std::vector<entt::entity> spawnMeshInstanceGrid(Scene& scene, MeshID meshId,
        int countX, int countZ, float spacing,
        const glm::vec3& origin = glm::vec3(0.0f));

    static std::vector<entt::entity> spawnMeshInstanceCircle(Scene& scene, MeshID meshId,
        int count, float radius,
        const glm::vec3& center = glm::vec3(0.0f));

    static std::vector<entt::entity> spawnMeshInstanceRandom(Scene& scene, MeshID meshId,
        int count, const glm::vec3& areaMin, const glm::vec3& areaMax);

    // Note: This CPU-based version is for demonstration and can be slow.
    static std::vector<entt::entity> spawnMeshInstanceNoisePattern(Scene& scene, MeshID meshId,
        TextureID noiseTextureId,
        const glm::vec2& dimensions,
        float textureScale = 1.0f,
        float densityThreshold = 0.5f);

	// --- Advanced Spawning ---

    /**
     * @brief Spawns multiple mesh instances at random locations on valid surfaces within a bounding box.
     * @param queryFunc A function provided by the engine to perform a raycast and find a surface.
     * @param alignToNormal If true, objects will tilt to match the surface normal (e.g., mushrooms on a tree).
     * If false, objects will remain upright (e.g., trees on a hill).
     * @param maxSlopeAngleDegrees The maximum steepness a surface can have to be considered a valid spawn point.
     */
    static std::vector<entt::entity> spawnOnSurface(
        Scene& scene,
        MeshID meshId,
        int count,
        const glm::vec3& boundsMin,
        const glm::vec3& boundsMax,
        const SurfaceQueryFunction& queryFunc,
        bool alignToNormal = false,
        float maxSlopeAngleDegrees = 15.0f,
        const glm::vec2& randomScaleRange = { 0.8f, 1.2f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 360.0f }
    );

    /**
     * @brief Spawns objects in randomized clusters or "piles".
     * @param meshIds A list of different MeshIDs to choose from when spawning.
     * @param weights A parallel list of floats determining the spawn chance for each mesh. Must sum to 1.0.
     * @param randomRotationStrength A value from 0.0 (all objects are perfectly upright) to 1.0 (all objects
     * have a completely random, tumbled rotation).
     */
    static std::vector<entt::entity> spawnInClumps(
        Scene& scene,
        const std::vector<MeshID>& meshIds,
        const std::vector<float>& weights,
        int clumpCount,
        int avgObjectsPerClump,
        float clumpRadius,
        const glm::vec3& areaMin,
        const glm::vec3& areaMax,
        float randomRotationStrength = 1.0f
    );

    /**
     * @brief Spawns a batch of entities from a pre-defined list of commands.
     * This is highly efficient for loading saved scenes or creating complex, pre-designed prefabs.
     * @param commands A list of SpawnCommand structs, each defining one entity to create.
     */
    static std::vector<entt::entity> executeSpawnList(
        Scene& scene,
        const std::vector<SpawnCommand>& commands
    );

    // ---- Orientation/random placement in a volume ----
    static std::vector<entt::entity> spawnOrientedRandom(
        Scene& scene,
        MeshID meshId,
        int count,
        const glm::vec3& areaMin,
        const glm::vec3& areaMax,
        // Option A: align local +Z to this world direction (normalized inside)
        const glm::vec3& targetZ_World,
        // Optional jitter
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomRollRangeDegrees = { 0.0f, 0.0f }
    );

    // Overload Option B: provide a full rotation basis for the instances
    static std::vector<entt::entity> spawnOrientedRandom(
        Scene& scene,
        MeshID meshId,
        int count,
        const glm::vec3& areaMin,
        const glm::vec3& areaMax,
        const glm::mat3& rotationBasisWorld,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomRollRangeDegrees = { 0.0f, 0.0f }
    );

    // ---- Oriented placement ON a surface with tolerance ----
    static std::vector<entt::entity> spawnOrientedRandomOnSurface(
        Scene& scene,
        MeshID meshId,
        int count,
        const glm::vec3& boundsMin,
        const glm::vec3& boundsMax,
        const SurfaceQueryFunction& queryFunc,
        // Local axis in the MODEL we consider "up" (e.g., {0,1,0})
        const glm::vec3& modelUp_Local,
        // The desired world "up" for the SURFACE (e.g., {0,1,0})
        const glm::vec3& surfaceUp_World,
        float upToleranceDegrees,
        // Placement options
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        bool alignToSurfaceNormal = false // if true, align to hit.normal; else align to surfaceUp_World
    );

    // ---- Primitives: same treatment as imported meshes ----
    static entt::entity spawnPrimitive(
        Scene& scene,
        Primitive type,
        const TransformComponent& transform = TransformComponent{},
        const std::string& name = "Primitive"
    );

    // ===== SceneBuilder.hpp additions =====

// Optional density function for 2D scattering (returns [0,1] probability)
    using DensityFn2D = std::function<float(const glm::vec2&)>;

    // Grid (in 3D box)
    static std::vector<entt::entity> spawnGridInBox(
        Scene& scene,
        MeshID meshId,
        const glm::vec3& boxMin,
        const glm::vec3& boxMax,
        const glm::ivec3& counts,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        const glm::mat3* worldBasisOptional = nullptr);

    // Grid projected onto a surface via raycast along -surfaceUp
    static std::vector<entt::entity> spawnGridOnSurface(
        Scene& scene,
        MeshID meshId,
        const glm::vec2& areaMinXZ,
        const glm::vec2& areaMaxXZ,
        const glm::ivec2& countsXZ,
        const SurfaceQueryFunction& queryFunc,
        const glm::vec3& modelUp_Local,
        const glm::vec3& surfaceUp_World,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        bool alignToSurfaceNormal = false);

    // Poisson-disk sampling on a surface (XZ domain) with Bridson’s algorithm
    static std::vector<entt::entity> spawnPoissonDisk2DOnSurface(
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
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        bool alignToSurfaceNormal = false,
        DensityFn2D densityFn = nullptr); // optional density mask

    // Clustered (clumps) distribution on a surface
    static std::vector<entt::entity> spawnClustersOnSurface(
        Scene& scene,
        MeshID meshId,
        int   clusterCount,
        int   minPerCluster,
        int   maxPerCluster,
        float clusterRadius,
        const glm::vec2& areaMinXZ,
        const glm::vec2& areaMaxXZ,
        const SurfaceQueryFunction& queryFunc,
        const glm::vec3& modelUp_Local,
        const glm::vec3& surfaceUp_World,
        float upToleranceDegrees,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        bool alignToSurfaceNormal = false);

    // Place along a polyline at fixed spacing, aligned to tangent (+ optional up)
    static std::vector<entt::entity> spawnAlongPolyline(
        Scene& scene,
        MeshID meshId,
        const std::vector<glm::vec3>& points,
        float spacing,
        const glm::vec3& modelForward_Local = glm::vec3(0, 0, 1),
        const glm::vec3& worldUp = glm::vec3(0, 1, 0),
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomRollRangeDegrees = { 0.0f, 0.0f });

    // Place along a cubic Bezier (4 control points) at fixed count/spacing
    static std::vector<entt::entity> spawnAlongBezier(
        Scene& scene,
        MeshID meshId,
        const glm::vec3& p0,
        const glm::vec3& p1,
        const glm::vec3& p2,
        const glm::vec3& p3,
        int   count,
        const glm::vec3& modelForward_Local = glm::vec3(0, 0, 1),
        const glm::vec3& worldUp = glm::vec3(0, 1, 0),
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomRollRangeDegrees = { 0.0f, 0.0f });

    // Stacks: spawn N vertical stacks at random surface points
    static std::vector<entt::entity> spawnStacksOnSurface(
        Scene& scene,
        MeshID meshId,
        int   stackCount,
        const glm::ivec2& stackHeightRange,     // [min,max] per stack
        const glm::vec2& areaMinXZ,
        const glm::vec2& areaMaxXZ,
        const SurfaceQueryFunction& queryFunc,
        const glm::vec3& modelUp_Local,
        const glm::vec3& surfaceUp_World,
        float upToleranceDegrees,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        bool alignToSurfaceNormal = false);

    // Non-overlapping placement inside a box using simple rejection (sphere approx)
    static std::vector<entt::entity> spawnInBoxNoOverlap(
        Scene& scene,
        MeshID meshId,
        int count,
        const glm::vec3& boxMin,
        const glm::vec3& boxMax,
        float minCenterDistance,
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        const glm::mat3* worldBasisOptional = nullptr);

    // From fixed anchor points (with jitter)
    static std::vector<entt::entity> spawnFromPoints(
        Scene& scene,
        MeshID meshId,
        const std::vector<glm::vec3>& anchors,
        const glm::vec3& jitterXYZ = glm::vec3(0.0f),
        const glm::vec2& randomScaleRange = { 1.0f, 1.0f },
        const glm::vec2& randomYawRangeDegrees = { 0.0f, 0.0f },
        const glm::mat3* worldBasisOptional = nullptr);



	// --- Camera Creation ---
    static entt::entity createCamera(Scene& scene,
        const glm::vec3& position,
        const glm::vec3& colour);

    static entt::entity makeCR(entt::registry& r,
        const std::vector<glm::vec3>& cps,
        const glm::vec4& coreColour,      // Changed parameter name
        const glm::vec4& glowColour,
        float glowThickness);

    static entt::entity makeParam(entt::registry& r,
        std::function<glm::vec3(float)> f,
        const glm::vec4& coreColour,      // Changed parameter name
        const glm::vec4& glowColour,
        float glowThickness);

    static entt::entity makeLinear(entt::registry& r,
        const std::vector<glm::vec3>& cps,
        const glm::vec4& coreColour,      // Changed parameter name
        const glm::vec4& glowColour,
        float glowThickness);

    static entt::entity makeBezier(entt::registry& r,
        const std::vector<glm::vec3>& cps,
        const glm::vec4& coreColour,      // Changed parameter name
        const glm::vec4& glowColour,
        float glowThickness);
};