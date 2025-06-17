#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <QtGui/qopengl.h>
#include <entt/entt.hpp> // Use the full include to prevent redefinition errors

// Forward declarations
class QOpenGLFunctions_4_1_Core;
class Shader;

class RenderingSystem {
public:
    RenderingSystem(QOpenGLFunctions_4_1_Core* gl);
    ~RenderingSystem();

    void initialize();
    void shutdown(); // Shutdown no longer needs the registry

    // Public render functions for a controlled drawing order from ViewportWidget
    void renderMeshes(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition);
    void renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
    void renderSplines(entt::registry& r, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye);
    void drawIntersections(const std::vector<std::vector<glm::vec3>>& allOutlines, const glm::mat4& view, const glm::mat4& proj);
    void setCurrentCamera(entt::entity e) { m_currentCamera = e; }
    void updateCameraTransforms(entt::registry& r);

    static bool isDescendantOf(entt::registry&,
        entt::entity,
        entt::entity);
private:
    // This private function is no longer needed with the new robust z-fighting solution
    // float depthOffsetOnePx(const glm::mat4& proj, float camDepth) const;
    entt::entity m_currentCamera{ entt::null };
    QOpenGLFunctions_4_1_Core* m_gl;
    int m_depthBits;

    // Shaders
    std::unique_ptr<Shader> m_phongShader;
    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Shader> m_outlineShader;
    std::unique_ptr<Shader> m_splineShader;

    // Grid Resources
    GLuint m_gridQuadVAO;
    GLuint m_gridQuadVBO;

    // Intersection Outline Resources
    GLuint m_intersectionVAO;
    GLuint m_intersectionVBO;

    GLuint m_tmpVAO{ 0 }, m_tmpVBO{ 0 };
};
