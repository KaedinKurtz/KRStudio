#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Camera.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>

void OpaquePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    // Get the shader we will use for this pass from the central renderer.
    m_phongShader = renderer.getShader("phong");
    if (!m_phongShader) {
        qFatal("OpaquePass failed to initialize: Could not find 'phong' shader.");
    }
}

void OpaquePass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    auto& registry = context.registry;
    auto& renderer = context.renderer;

    // --- Shader and Uniform Setup ---
    m_phongShader->use(gl);
    m_phongShader->setMat4(gl, "view", context.view);
    m_phongShader->setMat4(gl, "projection", context.projection);
    m_phongShader->setVec3(gl, "viewPos", context.camera.getPosition());

    // Hardcoded light for now, this could be moved to a component later.
    m_phongShader->setVec3(gl, "lightColor", glm::vec3(1.0f));
    m_phongShader->setVec3(gl, "lightPos", glm::vec3(5.0f, 10.0f, 5.0f));

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // --- Render Loop ---
    auto view = registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto entity : view) {
        // Don't draw the camera's own geometry.
        if (entity == renderer.getCurrentCameraEntity()) {
            continue;
        }

        // Get the required components from the entity.
        auto& mesh = view.get<RenderableMeshComponent>(entity);
        auto& xf = view.get<TransformComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        // Ask the RenderingSystem to get (or create) the GPU buffers for this mesh.
        // This single call replaces the entire buffer creation block.
        const auto& buffers = renderer.getOrCreateMeshBuffers(gl, ctx, entity);

        // If the buffers are not valid, skip.
        if (buffers.VAO == 0) continue;

        // Set per-object uniforms.
        m_phongShader->setVec3(gl, "objectColor", mat ? mat->albedo : glm::vec3(0.8f));

        glm::mat4 modelMatrix = registry.all_of<WorldTransformComponent>(entity)
            ? registry.get<WorldTransformComponent>(entity).matrix
            : xf.getTransform();
        m_phongShader->setMat4(gl, "model", modelMatrix);

        // Bind the VAO and draw the mesh.
        gl->glBindVertexArray(buffers.VAO);
        gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    gl->glBindVertexArray(0);
}