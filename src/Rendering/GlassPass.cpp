#include "GlassPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Cubemap.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>

void GlassPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    Q_UNUSED(gl);
}

void GlassPass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    auto& reg = context.registry;

    auto view = reg.view<GlassComponent, RenderableMeshComponent, TransformComponent>();
    bool any = false;
    for (auto e : view) {
        if (!reg.any_of<HiddenComponent>(e)) { any = true; break; }
    }
    if (!any) return;

    Shader* shader = context.renderer.getShader("glass");
    if (!gl || !shader) return;

    const int w = context.targetFBOs.w;
    const int h = context.targetFBOs.h;
    if (w == 0 || h == 0) return;

    // Per-viewport copies of the pre-glass frame (color + depth: a texture
    // can't be sampled while attached to the active framebuffer).
    Scratch& s = m_scratch[context.viewport];
    if (s.w != w || s.h != h || s.colorTex == 0) {
        if (s.colorTex) gl->glDeleteTextures(1, &s.colorTex);
        if (s.depthTex) gl->glDeleteTextures(1, &s.depthTex);
        if (s.depthFBO) gl->glDeleteFramebuffers(1, &s.depthFBO);
        gl->glGenTextures(1, &s.colorTex);
        gl->glBindTexture(GL_TEXTURE_2D, s.colorTex);
        gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glGenTextures(1, &s.depthTex);
        gl->glBindTexture(GL_TEXTURE_2D, s.depthTex);
        gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w, h);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glGenFramebuffers(1, &s.depthFBO);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, s.depthFBO);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   s.depthTex, 0);
        s.w = w;
        s.h = h;
    }
    gl->glCopyImageSubData(context.targetFBOs.finalColorTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                           s.colorTex, GL_TEXTURE_2D, 0, 0, 0, 0, w, h, 1);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.depthFBO);
    gl->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glViewport(0, 0, w, h);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glEnable(GL_CULL_FACE);
    gl->glCullFace(GL_BACK); // front faces only: one refraction interface

    shader->use(gl);
    shader->setMat4(gl, "view", context.view);
    shader->setMat4(gl, "projection", context.projection);
    shader->setVec3(gl, "u_camPos", context.camera.getPosition());
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, s.colorTex);
    shader->setInt(gl, "u_sceneColor", 0);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, s.depthTex);
    shader->setInt(gl, "u_sceneDepth", 1);
    const auto env = context.renderer.getPrefilteredEnvMap();
    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, env ? env->getID() : 0);
    shader->setInt(gl, "u_prefilteredEnv", 2);

    for (auto e : view) {
        if (reg.any_of<HiddenComponent>(e)) continue;
        const auto& glass = view.get<GlassComponent>(e);
        const auto& mesh = view.get<RenderableMeshComponent>(e);
        const auto& xf = view.get<TransformComponent>(e);
        if (mesh.indices.empty()) continue;

        shader->setMat4(gl, "model", xf.getTransform());
        shader->setFloat(gl, "u_ior", glass.ior);
        shader->setVec3(gl, "u_tint", glass.tint);
        shader->setFloat(gl, "u_thickness", glass.thickness);
        shader->setFloat(gl, "u_dispersion", glass.dispersion);
        const MaterialComponent* mat = reg.try_get<MaterialComponent>(e);
        shader->setFloat(gl, "u_roughness", mat ? mat->roughness * 0.5f : 0.05f);

        const auto& buf = context.renderer.getOrCreateMeshBuffers(
            gl, QOpenGLContext::currentContext(), e);
        gl->glBindVertexArray(buf.VAO);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
    }

    gl->glBindVertexArray(0);
    gl->glDisable(GL_CULL_FACE);
    gl->glDepthFunc(GL_LESS);
    gl->glActiveTexture(GL_TEXTURE0);
}
