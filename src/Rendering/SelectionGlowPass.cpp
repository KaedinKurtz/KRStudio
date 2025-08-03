#include "SelectionGlowPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "PrimitiveBuilders.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

SelectionGlowPass::~SelectionGlowPass() { /* ... destructor ... */ }

void SelectionGlowPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {

}

void SelectionGlowPass::execute(const RenderFrameContext& context) {
    Shader* emissiveSolidShader = context.renderer.getShader("emissive_solid");
    Shader* blurShader = context.renderer.getShader("blur");
    if (!emissiveSolidShader || !blurShader) return;

    auto* gl = context.gl;
    auto& registry = context.registry;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    const PostProcessingFBO* ppFBOs = context.renderer.getPPFBOs();
    auto viewSelected = registry.view<SelectedComponent, RenderableMeshComponent, TransformComponent>();

    // --- Pass 1: Render selected objects to the first ping-pong FBO ---
    gl->glBindFramebuffer(GL_FRAMEBUFFER, ppFBOs[0].fbo);
    gl->glViewport(0, 0, ppFBOs[0].w, ppFBOs[0].h);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, context.renderer.getGBuffer().fbo);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppFBOs[0].fbo);
    gl->glBlitFramebuffer(0, 0, context.renderer.getGBuffer().w, context.renderer.getGBuffer().h,
        0, 0, ppFBOs[0].w, ppFBOs[0].h,
        GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // THE FIX: Replace viewSelected.empty() with viewSelected.size_hint() == 0
    if (viewSelected.size_hint() == 0) {
        return;
    }

    emissiveSolidShader->use(gl);
    emissiveSolidShader->setMat4(gl, "view", context.view);
    emissiveSolidShader->setMat4(gl, "projection", context.projection);
    emissiveSolidShader->setVec3(gl, "emissiveColor", glm::vec3(1.0f, 0.75f, 0.1f));

    for (auto entity : viewSelected) {
        const auto& [mesh, transform] = viewSelected.get<const RenderableMeshComponent, const TransformComponent>(entity);
        emissiveSolidShader->setMat4(gl, "model", transform.getTransform());

        const auto& buffers = context.renderer.getOrCreateMeshBuffers(gl, ctx, entity);
        if (buffers.VAO != 0) {
            gl->glBindVertexArray(buffers.VAO);
            gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
    }

    // --- Pass 2: Apply Gaussian blur ---
    gl->glDisable(GL_DEPTH_TEST);
    blurShader->use(gl);
    blurShader->setInt(gl, "screenTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);

    if (!m_compositeVAOs.contains(ctx)) {
        createCompositeVAOForContext(ctx, gl);
    }
    gl->glBindVertexArray(m_compositeVAOs[ctx]);

    bool horizontal = true;
    unsigned int amount = 10;
    for (unsigned int i = 0; i < amount; i++) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, ppFBOs[horizontal].fbo);
        blurShader->setBool(gl, "horizontal", horizontal);
        gl->glBindTexture(GL_TEXTURE_2D, ppFBOs[!horizontal].colorTexture);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        horizontal = !horizontal;
    }

    // --- Final Step: Additive Blend ---
    gl->glBindFramebuffer(GL_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_ONE, GL_ONE);

    Shader* compositeShader = context.renderer.getShader("composite");
    if (compositeShader) {
        compositeShader->use(gl);
        compositeShader->setInt(gl, "screenTexture", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, ppFBOs[0].colorTexture);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // --- State Reset ---
    gl->glDisable(GL_BLEND);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glBindVertexArray(0);
}

void SelectionGlowPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_compositeVAOs.contains(dyingContext)) {
        gl->glDeleteVertexArrays(1, &m_compositeVAOs[dyingContext]);
        m_compositeVAOs.remove(dyingContext);
    }
}

void SelectionGlowPass::createCompositeVAOForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    setupFullscreenQuadAttribs(gl, vao);
    m_compositeVAOs[ctx] = vao;
}