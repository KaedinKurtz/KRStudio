#include "FluidSurfacePass.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "Shader.hpp"
#include "Cubemap.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

#include <glm/gtc/matrix_inverse.hpp>

namespace {

GLuint makeColorTexture(QOpenGLFunctions_4_3_Core* gl, int w, int h, GLenum internalFormat)
{
    GLuint tex = 0;
    gl->glGenTextures(1, &tex);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

} // namespace

void FluidSurfacePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    m_particleFallback.initialize(renderer, gl);
    if (m_emptyVao == 0) gl->glGenVertexArrays(1, &m_emptyVao);
}

void FluidSurfacePass::releaseBuffers(QOpenGLFunctions_4_3_Core* gl, SsfBuffers& b)
{
    GLuint fbos[5] = { b.depthFBO, b.smoothFBO[0], b.smoothFBO[1], b.thickFBO,
                       b.sceneDepthCopyFBO };
    gl->glDeleteFramebuffers(5, fbos);
    GLuint texs[6] = { b.depthTex, b.smoothTex[0], b.smoothTex[1], b.thickTex, b.sceneCopyTex,
                       b.sceneDepthCopyTex };
    gl->glDeleteTextures(6, texs);
    if (b.depthRB) gl->glDeleteRenderbuffers(1, &b.depthRB);
    b = SsfBuffers{};
}

FluidSurfacePass::SsfBuffers& FluidSurfacePass::buffersFor(const RenderFrameContext& ctx)
{
    auto* gl = ctx.gl;
    SsfBuffers& b = m_buffers[ctx.viewport];
    const int w = ctx.targetFBOs.w;
    const int h = ctx.targetFBOs.h;
    if (b.w == w && b.h == h && b.depthFBO) return b;
    if (b.depthFBO) releaseBuffers(gl, b);

    b.w = w;
    b.h = h;

    // Depth-sprite target: R32F view-z + real depth buffer for occlusion.
    b.depthTex = makeColorTexture(gl, w, h, GL_R32F);
    gl->glGenRenderbuffers(1, &b.depthRB);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, b.depthRB);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
    gl->glGenFramebuffers(1, &b.depthFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, b.depthFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.depthTex, 0);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, b.depthRB);

    for (int i = 0; i < 2; ++i) {
        b.smoothTex[i] = makeColorTexture(gl, w, h, GL_R32F);
        gl->glGenFramebuffers(1, &b.smoothFBO[i]);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, b.smoothFBO[i]);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   b.smoothTex[i], 0);
    }

    b.thickTex = makeColorTexture(gl, std::max(1, w / 2), std::max(1, h / 2), GL_R16F);
    gl->glGenFramebuffers(1, &b.thickFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, b.thickFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.thickTex, 0);

    b.sceneCopyTex = makeColorTexture(gl, w, h, GL_RGBA16F);

    // Scene-depth copy: the composite renders INTO finalFBO whose depth
    // attachment is finalDepthTexture — sampling it there would be a
    // feedback loop. Blit into this copy instead.
    gl->glGenTextures(1, &b.sceneDepthCopyTex);
    gl->glBindTexture(GL_TEXTURE_2D, b.sceneDepthCopyTex);
    gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w, h);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glGenFramebuffers(1, &b.sceneDepthCopyFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, b.sceneDepthCopyFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               b.sceneDepthCopyTex, 0);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return b;
}

void FluidSurfacePass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    FluidSystem* fluid = context.renderer.getFluidSystem();
    if (!gl || !fluid || fluid->particleCount() == 0) return;

    const FluidAppearance& look = fluid->appearance();
    if (look.renderMode == FluidRenderMode::Particles) {
        m_particleFallback.execute(context);
        return;
    }

    Shader* depthShader = context.renderer.getShader("fluid_ssf_depth");
    Shader* smoothShader = context.renderer.getShader("fluid_ssf_smooth");
    Shader* thickShader = context.renderer.getShader("fluid_ssf_thickness");
    Shader* compositeShader = context.renderer.getShader("fluid_ssf_composite");
    if (!depthShader || !smoothShader || !thickShader || !compositeShader) return;

    SsfBuffers& b = buffersFor(context);
    const int w = b.w, h = b.h;
    const float radius = fluid->particleRadius();
    const int count = fluid->particleCount();

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, fluid->particleBuffer());
    gl->glBindVertexArray(m_emptyVao);
    gl->glEnable(GL_PROGRAM_POINT_SIZE);

    auto bindSprites = [&](Shader* s) {
        s->use(gl);
        s->setMat4(gl, "u_view", context.view);
        s->setMat4(gl, "u_projection", context.projection);
        s->setFloat(gl, "u_particleRadius", radius);
        s->setFloat(gl, "u_sizeScale", look.sizeScale);
        s->setFloat(gl, "u_viewportHeight", float(h));
        s->setInt(gl, "u_sceneDepth", 5);
        gl->glActiveTexture(GL_TEXTURE5);
        gl->glBindTexture(GL_TEXTURE_2D, context.targetFBOs.finalDepthTexture);
    };

    // ---- 1) Sphere depth sprites -------------------------------------
    gl->glBindFramebuffer(GL_FRAMEBUFFER, b.depthFBO);
    gl->glViewport(0, 0, w, h);
    const float zero[4] = { 0, 0, 0, 0 };
    gl->glClearBufferfv(GL_COLOR, 0, zero);
    gl->glClearDepthf(1.0f);
    gl->glClear(GL_DEPTH_BUFFER_BIT);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    bindSprites(depthShader);
    depthShader->setVec2(gl, "u_invTargetSize", glm::vec2(1.0f / w, 1.0f / h));
    gl->glDrawArrays(GL_POINTS, 0, count);

    // Numeric pipeline probe: stats of the raw sprite depth output.
    static const int dbgEnv = qEnvironmentVariable("KRS_SSF_DEBUG").toInt();
    if (dbgEnv > 0) {
        static int probeFrame = 0;
        if ((++probeFrame % 120) == 0) {
            std::vector<float> pix(size_t(w) * size_t(h));
            gl->glBindTexture(GL_TEXTURE_2D, b.depthTex);
            gl->glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, pix.data());
            float mn = 1e9f, mx = -1e9f;
            int nonZero = 0;
            for (float v : pix) {
                if (v == 0.0f) continue;
                ++nonZero;
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            const glm::vec3 camPos = glm::vec3(glm::inverse(context.view)[3]);
            qInfo() << "[SSF] depthTex" << w << "x" << h << "nonZero" << nonZero
                    << "min" << (nonZero ? mn : 0.0f) << "max" << (nonZero ? mx : 0.0f)
                    << "particles" << count << "radius" << radius << "cam" << camPos.x
                    << camPos.y << camPos.z << "vp" << (const void*)context.viewport;
        }
    }

    // ---- 2) Narrow-range filter (separable, ping-pong) ----------------
    gl->glDisable(GL_DEPTH_TEST);
    smoothShader->use(gl);
    smoothShader->setFloat(gl, "u_particleRadius", radius * look.sizeScale);
    smoothShader->setFloat(gl, "u_projScaleY", context.projection[1][1] * h * 0.5f);
    smoothShader->setInt(gl, "u_depth", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    GLuint src = b.depthTex;
    int dst = 0;
    const int iterations = glm::clamp(look.smoothIterations, 1, 4);
    for (int i = 0; i < iterations * 2; ++i) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, b.smoothFBO[dst]);
        const glm::vec2 dir = (i % 2 == 0) ? glm::vec2(1.0f / w, 0.0f)
                                           : glm::vec2(0.0f, 1.0f / h);
        smoothShader->setVec2(gl, "u_dir", dir);
        gl->glBindTexture(GL_TEXTURE_2D, src);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        src = b.smoothTex[dst];
        dst = 1 - dst;
    }
    const GLuint smoothedDepth = src;

    // ---- 3) Thickness (half res, additive) ----------------------------
    gl->glBindFramebuffer(GL_FRAMEBUFFER, b.thickFBO);
    gl->glViewport(0, 0, std::max(1, w / 2), std::max(1, h / 2));
    gl->glClearBufferfv(GL_COLOR, 0, zero);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_ONE, GL_ONE);
    bindSprites(thickShader);
    thickShader->setVec2(gl, "u_invTargetSize",
                         glm::vec2(2.0f / w, 2.0f / h)); // half-res fragcoords
    thickShader->setFloat(gl, "u_viewportHeight", float(h) * 0.5f);
    gl->glDrawArrays(GL_POINTS, 0, count);
    gl->glDisable(GL_BLEND);

    // ---- 4) Composite over the scene ----------------------------------
    // Copy what the world looks like BEFORE the water so refraction can
    // sample it (can't read a texture bound to the active framebuffer).
    gl->glCopyImageSubData(context.targetFBOs.finalColorTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                           b.sceneCopyTex, GL_TEXTURE_2D, 0, 0, 0, 0, w, h, 1);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b.sceneDepthCopyFBO);
    gl->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, context.targetFBOs.finalFBO);
    gl->glViewport(0, 0, w, h);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDepthFunc(GL_LEQUAL);

    compositeShader->use(gl);
    auto bindTex = [&](int unit, GLenum target, GLuint id, const char* name) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(target, id);
        compositeShader->setInt(gl, name, unit);
    };
    static const int dbgMode = qEnvironmentVariable("KRS_SSF_DEBUG").toInt();
    bindTex(0, GL_TEXTURE_2D, dbgMode == 4 ? b.depthTex : smoothedDepth, "u_fluidDepth");
    bindTex(1, GL_TEXTURE_2D, b.thickTex, "u_thickness");
    bindTex(2, GL_TEXTURE_2D, b.sceneCopyTex, "u_sceneColor");
    bindTex(3, GL_TEXTURE_2D, b.sceneDepthCopyTex, "u_sceneDepth");
    const auto env = context.renderer.getPrefilteredEnvMap();
    bindTex(4, GL_TEXTURE_CUBE_MAP, env ? env->getID() : 0, "u_prefilteredEnv");

    compositeShader->setMat4(gl, "u_projection", context.projection);
    compositeShader->setMat4(gl, "u_invView", glm::inverse(context.view));
    compositeShader->setVec2(gl, "u_texel", glm::vec2(1.0f / w, 1.0f / h));
    compositeShader->setVec2(gl, "u_projScale",
                             glm::vec2(context.projection[0][0], context.projection[1][1]));
    compositeShader->setVec3(gl, "u_waterColor", look.color);
    compositeShader->setFloat(gl, "u_turbidity", look.turbidity);
    compositeShader->setFloat(gl, "u_emissivity", look.emissivity);
    compositeShader->setFloat(gl, "u_ior", look.ior);
    compositeShader->setFloat(gl, "u_absorptionScale", look.absorptionScale);
    compositeShader->setFloat(gl, "u_refractScale", look.refractScale);
    // Debug views: 1 = filtered depth, 2 = thickness, 3 = normals, 4 = raw depth.
    compositeShader->setInt(gl, "u_debugMode", dbgMode == 4 ? 1 : dbgMode);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- 5) Whitewater sprites over the surface -----------------------
    if (Shader* foam = context.renderer.getShader("fluid_foam_render");
        foam && look.foaminess > 0.001f) {
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, fluid->diffuseBuffer());
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_ONE, GL_ONE); // additive: whitewater glows over water
        gl->glDepthMask(GL_FALSE);       // test against fluid/scene, don't write
        foam->use(gl);
        foam->setMat4(gl, "u_view", context.view);
        foam->setMat4(gl, "u_projection", context.projection);
        foam->setFloat(gl, "u_particleRadius", radius);
        foam->setFloat(gl, "u_viewportHeight", float(h));
        gl->glDrawArrays(GL_POINTS, 0, FluidSystem::kMaxDiffuse);
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
    }

    // ---- restore expected overlay-stage state -------------------------
    gl->glBindVertexArray(0);
    gl->glDisable(GL_PROGRAM_POINT_SIZE);
    gl->glDepthFunc(GL_LESS);
    gl->glActiveTexture(GL_TEXTURE0);
}
