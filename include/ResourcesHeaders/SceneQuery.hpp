#pragma once

#include "Types.hpp"          // For MeshID, etc.
#include "components.hpp"     // For TransformComponent

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <optional>
#include <vector>

// --- Core Data Structures for Queries ---

// A simple structure representing a ray in 3D space.
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// A structure to hold the results of a successful raycast against a surface.
struct SurfaceHit {
    entt::entity entity = entt::null; // Which entity was hit.
    glm::vec3 position;             // The exact 3D point in world space where the ray hit.
    glm::vec3 normal;               // The surface normal at the point of impact.
    float distance;                 // The distance from the ray's origin to the hit point.
};

// A simple sphere defined by a center and radius.
struct Sphere {
    glm::vec3 center;
    float radius;
};

// --- Function Pointer Definitions ---

// Defines the signature for a generic raycasting function.
using RaycastFunction = std::function<std::optional<SurfaceHit>(const Ray& ray, float maxDistance)>;

/**
 * @brief Defines the signature for a generic surface query function.
 *
 * This is a powerful C++ type alias that defines a "contract" for a function.
 * Any function that matches this signature can be used by the SceneBuilder.
 *
 * @param start The starting point of the ray in world space.
 * @param dir The direction vector of the ray (should be normalized).
 * @return std::optional<SurfaceHit> An optional containing the hit information.
 * If the ray hits nothing, it will return std::nullopt.
 */
using SurfaceQueryFunction = std::function<std::optional<SurfaceHit>(const glm::vec3& start, const glm::vec3& dir)>;

// --- A static class for organizing all scene query functions ---
// In your C++ files, you would implement these functions. They would typically
// loop through the entities in the scene's registry and perform intersection tests.
class SceneQuery {
public:

    // ===================================================================
    // ==                          RAYCASTING                           ==
    // ===================================================================
    // Used for line-of-sight, picking objects with the mouse, finding the ground, etc.

    /**
     * @brief Casts a ray into the scene and finds the FIRST object it hits.
     * @return An optional containing the hit data, or std::nullopt if nothing was hit.
     */
    static std::optional<SurfaceHit> raycast(Scene& scene, const Ray& ray, float maxDistance = 1000.0f);

    /**
     * @brief Casts a ray into the scene and finds ALL objects it hits along its path.
     * @return A vector of all hits, sorted by distance from the origin.
     */
    static std::vector<SurfaceHit> raycastAll(Scene& scene, const Ray& ray, float maxDistance = 1000.0f);


    // ===================================================================
    // ==                         OVERLAP QUERIES                         ==
    // ===================================================================
    // Used to find all objects currently inside a given shape or volume.

    /**
     * @brief Finds all entities whose bounding boxes are inside or intersecting with a sphere.
     * @return A vector of entity handles.
     */
    static std::vector<entt::entity> overlapSphere(Scene& scene, const Sphere& sphere);

    /**
     * @brief Finds all entities whose bounding boxes are inside or intersecting with an AABB.
     * @return A vector of entity handles.
     */
    static std::vector<entt::entity> overlapBox(Scene& scene, const AABB& box);


    // ===================================================================
    // ==                          SWEEP TESTS                          ==
    // ===================================================================
    // Used to see if a volume (like a character's collision capsule) can move
    // along a path without hitting anything.

    /**
     * @brief Sweeps a sphere along a direction and finds the first object it collides with.
     * @return A SurfaceHit containing information about the first point of contact.
     */
    static std::optional<SurfaceHit> sphereCast(Scene& scene, const Sphere& sphere, const glm::vec3& direction, float maxDistance);


    // ===================================================================
    // ==                       RENDERING QUERIES                       ==
    // ===================================================================
    // Used by the rendering system to optimize by not drawing things that aren't visible.

    /**
     * @brief Finds all entities whose bounding boxes are inside the camera's view frustum.
     * @param frustumPlanes An array of 6 planes defining the camera's view volume.
     * @return A vector of entities that are potentially visible to the camera.
     */
    static std::vector<entt::entity> frustumCull(Scene& scene, const std::array<glm::vec4, 6>& frustumPlanes);
};