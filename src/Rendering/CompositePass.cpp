#include "CompositePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>

CompositePass::~CompositePass() { /* ... destructor ... */ }

void CompositePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    
}

void CompositePass::execute(const RenderFrameContext& context) {
    Shader* compositeShader = context.renderer.getShader("composite");
    if (!compositeShader) return;
    
    auto* gl = context.gl;
    auto& target = context.targetFBOs;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // --- This is the logic from the end of the old renderView ---

    // 1. Bind the default framebuffer (i.e., the screen).
    // The default FBO ID is obtained from the QOpenGLWidget's context.
    gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());
    gl->glViewport(0, 0, context.viewportWidth, context.viewportHeight); // Use the actual window size

    // 2. Set up the state for a 2D fullscreen draw.
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glDisable(GL_BLEND);
    gl->glEnable(GL_FRAMEBUFFER_SRGB); // Enable for correct gamma correction on output.

    // 3. Use the composite shader and set its textures.
    compositeShader->use(gl);
    compositeShader->setInt(gl, "sceneTexture", 0);
    compositeShader->setInt(gl, "glowTexture", 1);

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, target.mainColorTexture); // The result of all opaque/grid/etc. passes.

    gl->glActiveTexture(GL_TEXTURE1);
    // The result of the SelectionGlowPass's blur is in pingpongTexture[0].
    gl->glBindTexture(GL_TEXTURE_2D, target.pingpongTexture[0]);

    // 4. Draw the fullscreen triangle.
    if (!m_compositeVAOs.contains(ctx)) {
        createCompositeVAOForContext(ctx, gl);
    }
    gl->glBindVertexArray(m_compositeVAOs[ctx]);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // 5. Restore state for the next frame or UI rendering.
    gl->glBindVertexArray(0);
    gl->glDisable(GL_FRAMEBUFFER_SRGB);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glActiveTexture(GL_TEXTURE0); // Reset to default texture unit.
}

void CompositePass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_compositeVAOs.contains(dyingContext)) {
        gl->glDeleteVertexArrays(1, &m_compositeVAOs[dyingContext]);
        m_compositeVAOs.remove(dyingContext);
    }
}

void CompositePass::createCompositeVAOForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    setupFullscreenQuadAttribs(gl, vao);
    m_compositeVAOs[ctx] = vao;
}