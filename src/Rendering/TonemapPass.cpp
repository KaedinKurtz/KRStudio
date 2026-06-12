#include "TonemapPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QtGlobal>

void TonemapPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    if (m_emptyVao == 0) gl->glGenVertexArrays(1, &m_emptyVao);
}

void TonemapPass::execute(const RenderFrameContext& context)
{
    if (!RenderingSystem::hdrEnabled()) return;
    auto* gl = context.gl;
    Shader* shader = context.renderer.getShader("tonemap");
    if (!gl || !shader) return;

    const int w = context.targetFBOs.w;
    const int h = context.targetFBOs.h;
    if (w == 0 || h == 0) return;

    // Copy the HDR result aside — a texture can't be sampled while bound to
    // the framebuffer being written.
    Scratch& s = m_scratch[context.viewport];
    if (s.w != w || s.h != h || s.tex == 0) {
        if (s.tex) gl->glDeleteTextures(1, &s.tex);
        gl->glGenTextures(1, &s.tex);
        gl->glBindTexture(GL_TEXTURE_2D, s.tex);
        gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        s.w = w;
        s.h = h;
    }
    gl->glCopyImageSubData(context.targetFBOs.finalColorTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                           s.tex, GL_TEXTURE_2D, 0, 0, 0, 0, w, h, 1);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glViewport(0, 0, w, h);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glDisable(GL_BLEND);

    shader->use(gl);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, s.tex);
    shader->setInt(gl, "u_hdr", 0);
    shader->setFloat(gl, "u_exposure", 1.0f);

    gl->glBindVertexArray(m_emptyVao);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    gl->glBindVertexArray(0);

    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);
}
