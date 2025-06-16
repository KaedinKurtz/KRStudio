#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <entt/fwd.hpp>

// Forward declarations
class Shader;
class QOpenGLFunctions_3_3_Core;
class Scene;

class RenderingSystem {
public:
    RenderingSystem(QOpenGLFunctions_3_3_Core* gl);
    ~RenderingSystem();

    void initialize();
    void shutdown(entt::registry& registry);
    void render(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition);

private:
    // This function will render standard meshes with lighting.
    void renderMeshes(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition);
    // This new function will render the dynamic grid.
    void renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition);

    // --- Member Variables ---
    QOpenGLFunctions_3_3_Core* m_gl; // Non-owning pointer to the GL context.

    // GPU resources for meshes
    std::unique_ptr<Shader> m_phongShader;

    // GPU resources for the grid
    std::unique_ptr<Shader> m_gridShader; // The shader for drawing the grid.
    unsigned int m_gridQuadVAO = 0;       // The VAO for the grid's geometry (a simple quad).
    unsigned int m_gridQuadVBO = 0;       // The VBO for the grid's geometry.
};
