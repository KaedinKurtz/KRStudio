#include "GizmoPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "GizmoSystem.hpp" // For GizmoHandleComponent

#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLContext>

void GizmoPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    // This pass uses a simple, unlit shader to draw the gizmo with flat colors.
    // Your "emissive_solid" shader is perfect for this purpose.
    // No specific initialization is needed here as the shader is retrieved dynamically during execute().
}

void GizmoPass::execute(const RenderFrameContext& context)
{
    Shader* shader = context.renderer.getShader("gizmo_highlight");
    if (!shader) return;

    auto* gl = context.gl;
    auto& registry = context.registry;

    // Gizmo should self-occlude but never be occluded by scene depth
    gl->glClear(GL_DEPTH_BUFFER_BIT);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDepthFunc(GL_LESS);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);

    shader->use(gl);
    shader->setMat4(gl, "view", context.view);
    shader->setMat4(gl, "projection", context.projection);
    shader->setVec3(gl, "uViewPos", context.camera.getPosition());
    shader->setVec3(gl, "uHighlightColor", glm::vec3(1.0f)); // white tint

    // If ANY handle is active, dim all others
    const auto activeView = registry.view<ActiveGizmoTag>();
    const bool anyActive = !activeView.empty();

    // Draw visible gizmo handles
    auto view = registry.view<
        GizmoHandleComponent,
        TransformComponent,
        RenderableMeshComponent,
        MaterialComponent
    >(entt::exclude<HiddenComponent>);

    for (entt::entity e : view)
    {
        const auto& meshComp = view.get<RenderableMeshComponent>(e);
        if (meshComp.indices.empty()) continue;

        // Model matrix (prefer cached world transform if present)
        glm::mat4 model = view.get<TransformComponent>(e).getTransform();
        if (const auto* w = registry.try_get<WorldTransformComponent>(e))
            model = w->matrix;

        const auto& material = view.get<MaterialComponent>(e);

        const bool isHovered = registry.all_of<HoveredGizmoTag>(e);
        const bool isActive = registry.all_of<ActiveGizmoTag>(e);

        // --- Uniforms per handle ---
        shader->setMat4(gl, "model", model);
        shader->setVec3(gl, "uBaseColor", material.albedoColor);
        shader->setInt(gl, "uIsHovered", isHovered ? 1 : 0);
        shader->setInt(gl, "uIsActive", isActive ? 1 : 0);
        shader->setFloat(gl, "uDimFactor", anyActive ? 1.0f : 0.0f); // grey others when any active

        // --- Draw ---
        const auto& buffers = context.renderer.getOrCreateMeshBuffers(
            gl, QOpenGLContext::currentContext(), e
        );
        gl->glBindVertexArray(buffers.VAO);
        gl->glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(meshComp.indices.size()),
            GL_UNSIGNED_INT, nullptr);
    }

    gl->glBindVertexArray(0);
    // Leave state as-is; later passes will set what they need
}