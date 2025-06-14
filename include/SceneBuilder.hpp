// In SceneBuilder.hpp
#include "RobotDescription.hpp"
#include "Scene.hpp" // Needs to know about the scene

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
};