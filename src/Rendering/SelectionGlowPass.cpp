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
    auto& target = context.targetFBOs;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    auto viewSelected = registry.view<SelectedComponent, RenderableMeshComponent, TransformComponent>();

    // --- Pass 1: Render selected objects to the glow FBO ---
    gl->glBindFramebuffer(GL_FRAMEBUFFER, target.glowFBO);
    gl->glViewport(0, 0, target.w, target.h);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT); // Clear the glow texture

    // If nothing is selected, we still need to clear the texture, then we can exit.
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

        // Use the renderer's helper to get the mesh buffers
        const auto& buffers = context.renderer.getOrCreateMeshBuffers(gl, ctx, entity);
        if (buffers.VAO != 0) {
            gl->glBindVertexArray(buffers.VAO);
            gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
    }

    // --- Pass 2: Apply Gaussian blur using ping-pong FBOs ---
    gl->glDisable(GL_DEPTH_TEST);
    blurShader->use(gl);
    blurShader->setInt(gl, "screenTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);

    // Ensure the composite VAO exists for this context.
    if (!m_compositeVAOs.contains(ctx)) {
        createCompositeVAOForContext(ctx, gl);
    }
    gl->glBindVertexArray(m_compositeVAOs[ctx]);

    bool horizontal = true;
    bool first_iteration = true;
    unsigned int amount = 10; // A 10-pass blur is smoother
    for (unsigned int i = 0; i < amount; i++) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, target.pingpongFBO[horizontal]);
        blurShader->setBool(gl, "horizontal", horizontal);

        // Bind the texture from the previous step to read from.
        gl->glBindTexture(GL_TEXTURE_2D, first_iteration ? target.glowTexture : target.pingpongTexture[!horizontal]);

        gl->glDrawArrays(GL_TRIANGLES, 0, 3); // Draw the fullscreen triangle.

        horizontal = !horizontal;
        if (first_iteration) {
            first_iteration = false;
        }
    }

    // --- State Reset ---
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