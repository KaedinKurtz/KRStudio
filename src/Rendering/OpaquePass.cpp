#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "BlackBox.hpp"
#include "ViewportWidget.hpp" 

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>

struct GLStateSaver {
    GLStateSaver(QOpenGLFunctions_4_3_Core* funcs) : gl(funcs) {
        gl->glGetBooleanv(GL_BLEND, &blendEnabled);
        gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskEnabled);
        gl->glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
    }
    ~GLStateSaver() {
        if (blendEnabled) gl->glEnable(GL_BLEND); else gl->glDisable(GL_BLEND);
        gl->glDepthMask(depthMaskEnabled);
        if (cullFaceEnabled) gl->glEnable(GL_CULL_FACE); else gl->glDisable(GL_CULL_FACE);
    }
    QOpenGLFunctions_4_3_Core* gl;
    GLboolean blendEnabled, depthMaskEnabled, cullFaceEnabled;
};

void OpaquePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {

}

void OpaquePass::execute(const RenderFrameContext& context) {
    
#ifdef ENABLE_BLACKBOX_LOGGING
    dbg::BlackBox::instance().dumpState("OpaquePass - START", context.renderer, static_cast<QOpenGLWidget*>(context.viewport), context.gl);
#endif


    Shader* phongShader = context.renderer.getShader("phong");
    if (!phongShader) {
        // If there's no shader for this context, we can't do anything.
        return;
    }
    
    auto* gl = context.gl;
    auto& registry = context.registry;
    auto& renderer = context.renderer;
    auto  curCam = renderer.getCurrentCameraEntity();

    // --- Shader and Uniform Setup ---
    phongShader->use(gl);
    phongShader->setMat4(gl, "view", context.view);
    phongShader->setMat4(gl, "projection", context.projection);
    phongShader->setVec3(gl, "viewPos", context.camera.getPosition());

    // Hardcoded light for now, this could be moved to a component later.
    phongShader->setVec3(gl, "lightColor", glm::vec3(1.0f));
    phongShader->setVec3(gl, "lightPos", glm::vec3(5.0f, 10.0f, 5.0f));

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // --- Render Loop ---
    auto view = registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto entity : view) {
        // Don't draw the camera's own geometry.
        if (entity == curCam ||
            isDescendantOf(registry, entity, curCam))
            continue;

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
        phongShader->setVec3(gl, "objectColor", mat ? mat->albedo : glm::vec3(0.8f));

        glm::mat4 modelMatrix = registry.all_of<WorldTransformComponent>(entity)
            ? registry.get<WorldTransformComponent>(entity).matrix
            : xf.getTransform();
        phongShader->setMat4(gl, "model", modelMatrix);

        // Bind the VAO and draw the mesh.
        gl->glBindVertexArray(buffers.VAO);
        gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    gl->glBindVertexArray(0);

#ifdef ENABLE_BLACKBOX_LOGGING
    dbg::BlackBox::instance().dumpState("OpaquePass - END", context.renderer, static_cast<QOpenGLWidget*>(context.viewport), context.gl);
#endif
}