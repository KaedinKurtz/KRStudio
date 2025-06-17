// In SceneBuilder.hpp
#include "RobotDescription.hpp"
#include "Scene.hpp" // Needs to know about the scene
#include <vector>          
#include <functional>

struct RobotDescription;
class Scene;

// A static utility class for populating a Scene from a RobotDescription.
// This decouples the scene creation logic from the UI and parsers.
class SceneBuilder
{
public:
    // Takes a scene and a robot description, and creates all the necessary
    // entities and components in the scene's registry.
    static void spawnRobot(Scene& scene, const RobotDescription& description);

    static entt::entity createCamera(entt::registry&,
        const glm::vec3& position,
        const glm::vec3& colour = { 1,1,0 });

    static entt::entity makeCR(entt::registry&,
        const std::vector<glm::vec3>&,
        const glm::vec4& colour = { 1,1,1,1 });

    static entt::entity makeParam(entt::registry&,
        std::function<glm::vec3(float)>,
        const glm::vec4& colour = { 1,1,1,1 });
};