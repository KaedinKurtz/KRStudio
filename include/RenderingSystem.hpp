#pragma once

#include <glm/glm.hpp>

// Forward declarations
class Scene;

class RenderingSystem {
public:
    // No changes to the public interface
    static void initialize();
    static void shutdown(Scene* scene);
    static void render(Scene* scene, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition);

private:
    // This flag will prevent multiple shutdowns
    static bool s_isInitialized;
};