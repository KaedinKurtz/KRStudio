// In SceneBuilder.hpp
#include "RobotDescription.hpp"
#include "Scene.hpp" // Needs to know about the scene
#include "Types.hpp"
#include "Components.hpp" // Needs to know about components it will add
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