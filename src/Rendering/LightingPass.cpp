#include "LightingPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs
#include "Texture2D.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QtGlobal>
#include <QString>

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

    // --- Orbiting point light (RESTORED to the pre-sprint behaviour) ---
    // The settings sprint replaced this moving key light with a static directional
    // sun and set activeLightCount=0. With a dim/wrong sun colour that left the
    // scene lit ONLY by the blue-sky IBL -> cyan ground + void-black robot + no
    // moving light. Restoring the orbiting point light brings the key illumination
    // (and the motion) back; the directional sun + IBL below remain Settings-driven.
    // NEAR orbiting key light: a tight orbit (was radius 10 -> ~1.6 radiance at the
    // robot with 1/d^2, too dim to light the textures) brought in close + brightened
    // so it actually illuminates the robot (the directional sun + IBL are fill).
    // Orbit at ROBOT HEIGHT (was y=4, above the arm -> only lit the tops like the sun,
    // leaving the vertical faces the camera sees dark under a dark-horizon env). At
    // y~2 / radius 3.5 the key light sweeps the SIDES of the robot, so its visible
    // faces get a bright, hue-preserving white key as it orbits.
    const float lightRadius = 3.5f;
    const float lightSpeed  = 1.5f;
    const float lx = lightRadius * cos(context.elapsedTime * lightSpeed);
    const float lz = lightRadius * sin(context.elapsedTime * lightSpeed);
    glm::vec3 animatedLightPos = glm::vec3(lx, 2.0f, lz);
    // Dev aid: KRS_LIGHTPOS="x,y,z" pins the key light (deterministic lighting for
    // verification grabs, instead of the nondeterministic orbit phase at grab time).
    if (qEnvironmentVariableIsSet("KRS_LIGHTPOS")) {
        const QStringList p = qEnvironmentVariable("KRS_LIGHTPOS").split(',');
        if (p.size() == 3) animatedLightPos = glm::vec3(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
    }

    // --- Static directional sun (Settings-controlled supplementary key light) ---
    const glm::vec3 sunDir   = renderer.getSunDirection();                      // live (Settings)
    const glm::vec3 sunColor = renderer.getSunColor() * renderer.getSunIntensity(); // live tint * intensity

    lightingShd->use(gl);
    lightingShd->setVec3(gl, "viewPos", context.camera.getPosition());
    lightingShd->setVec3(gl, "lightPositions[0]", animatedLightPos);
    lightingShd->setVec3(gl, "lightColors[0]", glm::vec3(300.0f, 270.0f, 240.0f)); // fairly bright warm key (1/d^2 -> ~8 radiance at the robot)
    lightingShd->setInt(gl, "activeLightCount", 1);  // orbiting point light RESTORED
    lightingShd->setVec3(gl, "u_sunDir", sunDir);
    lightingShd->setVec3(gl, "u_sunColor", sunColor);
    // Per-pixel view-ray reconstruction so the lighting pass can blend silhouette
    // /background fragments into the sky instead of a near-black coverage band.
    lightingShd->setMat4(gl, "u_invViewProj", glm::inverse(context.projection * context.view));
    lightingShd->setInt(gl, "u_hdrEnabled", renderer.getHdrEnabled() ? 1 : 0);
    // IBL fill strength. HDR irradiance is ~PI*sky-radiance, which blows albedo
    // to flat white; ~0.3 restores texture contrast. Tune 0.2-0.4 to taste.
    lightingShd->setFloat(gl, "u_iblIntensity", renderer.getIblIntensity());
    lightingShd->setFloat(gl, "u_specClamp", renderer.getSpecFireflyClamp());

    // --- TEMP empirical desaturation probes (env-overridable; remove after diagnosis) ---
    // KRS_DBG=1..7 isolates a single lighting term to the screen (run with KRS_HDR=0).
    // KRS_IBL=<f> live-overrides the IBL fill strength so we can dial it without a rebuild.
    {
        const int dbg = qEnvironmentVariableIsSet("KRS_DBG") ? qEnvironmentVariable("KRS_DBG").toInt() : 0;
        lightingShd->setInt(gl, "u_debugView", dbg);
        if (qEnvironmentVariableIsSet("KRS_IBL"))
            lightingShd->setFloat(gl, "u_iblIntensity", qEnvironmentVariable("KRS_IBL").toFloat());
        // Default 1.0: the specular IBL is now albedo-tinted (hue-preserving) in the
        // shader, so we keep its full brightness as the robot's fill instead of cutting it.
        const float specScale = qEnvironmentVariableIsSet("KRS_SPECIBL") ? qEnvironmentVariable("KRS_SPECIBL").toFloat() : 1.0f;
        lightingShd->setFloat(gl, "u_iblSpecScale", specScale);
        const float iblTint = qEnvironmentVariableIsSet("KRS_TINT") ? qEnvironmentVariable("KRS_TINT").toFloat() : 1.0f;
        lightingShd->setFloat(gl, "u_iblTint", iblTint);
    }

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