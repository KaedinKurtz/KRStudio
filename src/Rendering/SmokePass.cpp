#include "SmokePass.hpp"
#include "RenderingSystem.hpp"
#include "SmokeSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/gtc/matrix_inverse.hpp>

void SmokePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    if (m_emptyVao == 0) gl->glGenVertexArrays(1, &m_emptyVao);
}

void SmokePass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    SmokeSystem* smoke = context.renderer.getSmokeSystem();
    if (!gl || !smoke || !smoke->active()) return;

    Shader* shader = context.renderer.getShader("smoke_raymarch");
    if (!shader) return;

    const int w = context.targetFBOs.w;
    const int h = context.targetFBOs.h;
    if (w == 0 || h == 0) return;

    // Depth copy (can't sample the depth attached to finalFBO).
    Buffers& b = m_buffers[context.viewport];
    if (b.w != w || b.h != h || b.depthCopyTex == 0) {
        if (b.depthCopyTex) gl->glDeleteTextures(1, &b.depthCopyTex);
        if (b.depthCopyFBO) gl->glDeleteFramebuffers(1, &b.depthCopyFBO);
        gl->glGenTextures(1, &b.depthCopyTex);
        gl->glBindTexture(GL_TEXTURE_2D, b.depthCopyTex);
        gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w, h);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glGenFramebuffers(1, &b.depthCopyFBO);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, b.depthCopyFBO);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   b.depthCopyTex, 0);
        b.w = w;
        b.h = h;
    }
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b.depthCopyFBO);
    gl->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glViewport(0, 0, w, h);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied over

    shader->use(gl);
    const glm::mat4 invVP = glm::inverse(context.projection * context.view);
    shader->setMat4(gl, "u_invViewProj", invVP);
    shader->setVec3(gl, "u_camPos", context.camera.getPosition());
    shader->setVec3(gl, "u_origin", smoke->origin());
    shader->setVec3(gl, "u_size", smoke->extent());
    shader->setVec3(gl, "u_smokeColor", smoke->params().smokeColor);
    shader->setVec3(gl, "u_lightDir", glm::normalize(glm::vec3(0.35f, -0.85f, 0.4f)));
    shader->setFloat(gl, "u_densityScale", smoke->params().densityScale);
    shader->setInt(gl, "u_steps", smoke->gridN() >= 128 ? 80 : 56);
    shader->setInt(gl, "u_fireEnabled", smoke->params().fireEnabled ? 1 : 0);
    static const int dbg = qEnvironmentVariable("KRS_SMOKE_DEBUG").toInt();
    shader->setInt(gl, "u_debug", dbg);

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_3D, smoke->scalarsTexture());
    shader->setInt(gl, "u_scalars", 0);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, b.depthCopyTex);
    shader->setInt(gl, "u_sceneDepth", 1);

    gl->glBindVertexArray(m_emptyVao);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    gl->glBindVertexArray(0);

    gl->glDisable(GL_BLEND);
    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glActiveTexture(GL_TEXTURE0);
}
