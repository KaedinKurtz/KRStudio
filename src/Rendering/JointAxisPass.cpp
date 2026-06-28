#include "JointAxisPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"   // JointAxisComponent, TransformComponent, RenderableMeshComponent, MaterialComponent

#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLContext>

void JointAxisPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    // Shader ("gizmo_highlight", a flat unlit shader) is fetched dynamically in execute().
}

void JointAxisPass::execute(const RenderFrameContext& context)
{
    auto& registry = context.registry;

    // Cheap early-out: only the Robot View spawns these; the main scene has none.
    auto view = registry.view<
        JointAxisComponent,
        TransformComponent,
        RenderableMeshComponent,
        MaterialComponent
    >(entt::exclude<HiddenComponent>);
    if (view.begin() == view.end()) return;

    Shader* shader = context.renderer.getShader("gizmo_highlight");
    if (!shader) return;

    auto* gl = context.gl;

    // Like the gizmo: self-occlude but never be occluded by the scene -- clear depth first.
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
    shader->setVec3(gl, "uHighlightColor", glm::vec3(1.0f));

    for (entt::entity e : view)
    {
        const auto& meshComp = view.get<RenderableMeshComponent>(e);
        if (meshComp.indices.empty()) continue;

        glm::mat4 model = view.get<TransformComponent>(e).getTransform();
        if (const auto* w = registry.try_get<WorldTransformComponent>(e))
            model = w->matrix;

        const auto& material = view.get<MaterialComponent>(e);

        shader->setMat4(gl, "model", model);
        shader->setVec3(gl, "uBaseColor", material.albedoColor);
        shader->setInt(gl, "uIsHovered", 0);
        shader->setInt(gl, "uIsActive", 0);
        shader->setFloat(gl, "uDimFactor", 0.0f);

        const auto& buffers = context.renderer.getOrCreateMeshBuffers(
            gl, QOpenGLContext::currentContext(), e
        );
        gl->glBindVertexArray(buffers.VAO);
        gl->glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(meshComp.indices.size()),
            GL_UNSIGNED_INT, nullptr);
    }

    gl->glBindVertexArray(0);
}
