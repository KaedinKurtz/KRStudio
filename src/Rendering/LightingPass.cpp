#include "LightingPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs
#include "Texture2D.hpp"

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

    // --- State backup and FBO setup (Your existing code is good here) ---
    GLint prevDrawFBO = 0;
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
    GLboolean prevDepthTest = gl->glIsEnabled(GL_DEPTH_TEST);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.targetFBOs.finalFBO);
    GLenum buf = GL_COLOR_ATTACHMENT0;
    gl->glDrawBuffers(1, &buf);
    gl->glViewport(0, 0, context.targetFBOs.w, context.targetFBOs.h);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // --- Animated light (Your existing code is good here) ---
    float radius = 10.0f;
    float speed = 1.5f;
	float baseHeight = 5.0f; // Fixed height for the light
	float heightAmplitude = 3.0f; // No vertical oscillation
    float x = radius * cos(context.elapsedTime * speed);
    float z = radius * sin(context.elapsedTime * speed);
    float y = baseHeight + heightAmplitude * sin(context.elapsedTime * speed); // Fixed height for the light
    glm::vec3 animatedLightPos = glm::vec3(x, 5.0f, z);

    lightingShd->use(gl);
    lightingShd->setVec3(gl, "viewPos", context.camera.getPosition());
    lightingShd->setVec3(gl, "lightPositions[0]", animatedLightPos);
    lightingShd->setVec3(gl, "lightColors[0]", glm::vec3(200.0, 150.0, 150.0)); // Increased intensity for physical correctness
    lightingShd->setInt(gl, "activeLightCount", 1);

    // --- Texture Binding ---
    int unit = 0;
    auto bindTex = [&](GLuint texID, const char* name, GLenum target) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(target, texID);
        lightingShd->setInt(gl, name, unit);
        unit++;
        };

    // Bind G-Buffer textures (unit 0-4)
    const auto& g = renderer.getGBuffer();
    bindTex(g.positionTexture, "gPosition", GL_TEXTURE_2D);
    bindTex(g.normalTexture, "gNormal", GL_TEXTURE_2D);
    bindTex(g.albedoAOTexture, "gAlbedoAO", GL_TEXTURE_2D);
    bindTex(g.metalRougTexture, "gMetalRough", GL_TEXTURE_2D);
    bindTex(g.emissiveTexture, "gEmissive", GL_TEXTURE_2D);

    // --- FIXED IBL TEXTURE BINDING (unit 5-7) ---
    // This new logic safely handles cases where IBL maps might not have been generated.

    // Bind Irradiance Map (unit 5)
    if (auto irrad = renderer.getIrradianceMap()) {
        bindTex(irrad->getID(), "irradianceMap", GL_TEXTURE_CUBE_MAP);
    }
    else {
        bindTex(renderer.getDefaultEmissive()->getID(), "irradianceMap", GL_TEXTURE_CUBE_MAP); // Bind default black
    }

    // Bind Prefiltered Environment Map (unit 6)
    if (auto pre = renderer.getPrefilteredEnvMap()) {
        bindTex(pre->getID(), "prefilteredEnvMap", GL_TEXTURE_CUBE_MAP);
    }
    else {
        bindTex(renderer.getDefaultEmissive()->getID(), "prefilteredEnvMap", GL_TEXTURE_CUBE_MAP); // Bind default black
    }

    // Bind BRDF LUT (unit 7)
    if (auto lut = renderer.getBRDFLUT()) {
        bindTex(lut->getID(), "brdfLUT", GL_TEXTURE_2D);
    }
    else {
        bindTex(renderer.getDefaultEmissive()->getID(), "brdfLUT", GL_TEXTURE_2D); // Bind default black
    }


    // --- Draw full-screen triangle ---
    QOpenGLContext* qc = QOpenGLContext::currentContext();
    if (!m_fullscreenVAOs.contains(qc)) {
        createFullscreenQuad(qc, gl);
    }
    gl->glBindVertexArray(m_fullscreenVAOs[qc]);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    gl->glBindVertexArray(0);

    // --- Cleanup & restore ---
    gl->glBindFramebuffer(GL_FRAMEBUFFER, prevDrawFBO);
    if (prevDepthTest) gl->glEnable(GL_DEPTH_TEST);
    else gl->glDisable(GL_DEPTH_TEST);
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