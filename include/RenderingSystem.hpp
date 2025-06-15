#pragma once

#include "Scene.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "components.hpp"
#include <QOpenGLFunctions_3_3_Core>
#include <glm/glm.hpp>

namespace RenderingSystem
{
    // Acquire OpenGL functions from the current context
    void initialize();

    void shutdown(Scene* scene);
    void uploadMeshes(Scene* scene);
    void render(Scene* scene,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix,
                const glm::vec3& cameraPos);
}
