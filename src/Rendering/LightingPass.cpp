#include "LightingPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>

LightingPass::~LightingPass() {}

void LightingPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    // Shaders are retrieved during execute()
}

void LightingPass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    auto& renderer = context.renderer;
    Shader* lightingShd = renderer.getShader("lighting");
    if (!gl || !lightingShd) {
        qWarning() << "[LightingPass] Missing GL functions or shader!";
        return;
    }

    // --- Backup state ---
    GLint prevDrawFBO = 0;
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
    GLboolean prevDepthTest = gl->glIsEnabled(GL_DEPTH_TEST);

    auto restoreState = [&]() {
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        if (prevDepthTest) gl->glEnable(GL_DEPTH_TEST);
        else               gl->glDisable(GL_DEPTH_TEST);
        };

    // --- Bind final FBO and clear ---
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.targetFBOs.finalFBO);
    GLenum buf = GL_COLOR_ATTACHMENT0;
    gl->glDrawBuffers(1, &buf);
    gl->glViewport(0, 0, context.targetFBOs.w, context.targetFBOs.h);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glClearColor(0.f, 0.f, 0.f, 1.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // --- Set common uniforms ---
    lightingShd->use(gl);
    lightingShd->setVec3(gl, "viewPos", context.camera.getPosition());
    lightingShd->setVec3(gl, "lightPos", glm::vec3(5.f, 10.f, 5.f));
    lightingShd->setVec3(gl, "lightColor", glm::vec3(1.f));

    // --- Helper to bind & assign sampler uniforms ---
    auto bindTex = [&](GLuint tex, const char* name, int unit, GLenum target = GL_TEXTURE_2D) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(target, tex);
        lightingShd->setInt(gl, name, unit);
        };

    // --- Bind G-Buffer textures ---
    const auto& g = renderer.getGBuffer();
    bindTex(g.positionTexture, "gPosition", 0);
    bindTex(g.normalTexture, "gNormal", 1);
    bindTex(g.albedoAOTexture, "gAlbedoAO", 2);
    bindTex(g.metalRougTexture, "gMetalRoug", 3);
    bindTex(g.emissiveTexture, "gEmissive", 4);

    // --- Bind IBL maps (or default to 0) ---
    int unit = 5;
    if (auto env = renderer.getEnvCubemap()) {
        bindTex(env->getID(), "environmentMap", unit++, GL_TEXTURE_CUBE_MAP);
    }
    else {
        lightingShd->setInt(gl, "environmentMap", 0);
    }

    if (auto irrad = renderer.getIrradianceMap()) {
        bindTex(irrad->getID(), "irradianceMap", unit++, GL_TEXTURE_CUBE_MAP);
    }
    else {
        lightingShd->setInt(gl, "irradianceMap", 0);
    }

    if (auto pre = renderer.getPrefilteredEnvMap()) {
        bindTex(pre->getID(), "prefilteredEnvMap", unit++, GL_TEXTURE_CUBE_MAP);
    }
    else {
        lightingShd->setInt(gl, "prefilteredEnvMap", 0);
    }

    if (auto lut = renderer.getBRDFLUT()) {
        bindTex(lut->getID(), "brdfLUT", unit++);
    }
    else {
        lightingShd->setInt(gl, "brdfLUT", 0);
    }

    // --- Draw full-screen triangle ---
    QOpenGLContext* qc = QOpenGLContext::currentContext();
    if (!m_fullscreenVAOs.contains(qc))
        createFullscreenQuad(qc, gl);
    gl->glBindVertexArray(m_fullscreenVAOs[qc]);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Cleanup & restore ---
    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    restoreState();
}

void LightingPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_fullscreenVAOs.contains(dyingContext)) {
        GLuint vao_id = m_fullscreenVAOs.value(dyingContext);
        gl->glDeleteVertexArrays(1, &vao_id);
        m_fullscreenVAOs.remove(dyingContext);
    }
}

void LightingPass::createFullscreenQuad(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    // This helper function (already in your PrimitiveBuilders file)
    // sets up a VAO for drawing a fullscreen triangle without a VBO.
    setupFullscreenQuadAttribs(gl, vao);
    m_fullscreenVAOs[ctx] = vao;
}