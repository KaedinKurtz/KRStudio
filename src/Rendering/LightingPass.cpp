#include "LightingPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs
#include "Texture2D.hpp"

#include "components.hpp"   // LightComponent, TransformComponent
#include <glm/gtc/quaternion.hpp>  // glm::mat3_cast for spot/directional facing
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QtGlobal>
#include <QString>
#include <string>

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

    // --- Static directional sun (Settings-controlled key light, in LUX) ---
    // The legacy orbiting fallback light is GONE: scenes are lit by ECS light entities
    // (seeded by default) plus this sun and the IBL. The sun intensity is illuminance
    // in lux (physically-based); the global exposure (TonemapPass) brings it to display.
    const glm::vec3 sunDir   = renderer.getSunDirection();
    const glm::vec3 sunColor = renderer.getSunColor() * renderer.getSunIntensity(); // lux

    lightingShd->use(gl);
    lightingShd->setVec3(gl, "viewPos", context.camera.getPosition());

    // --- Gather lights from the ECS (addable light entities) ---
    // A light is an entity with LightComponent + TransformComponent. Point lights fill
    // lightPositions[]/lightColors[] (<=8). Rect AREA lights fill areaLights[] (<=8, LTC):
    // world corners from the transform (unit rect in the local XY plane, half-size =
    // lc.size*0.5, facing +localZ, CCW from the front). The legacy orbiting key light is
    // used ONLY as a fallback when the scene has NO light entities of ANY type.
    int pointCount = 0, areaCount = 0, dirCount = 0, spotCount = 0;
    {
        auto& registry = context.registry;
        auto lightView = registry.view<LightComponent, TransformComponent>();
        for (auto ent : lightView) {
            const auto& lc = lightView.get<LightComponent>(ent);
            if (!lc.enabled) continue;
            const auto& xf = lightView.get<TransformComponent>(ent);
            if (lc.type == LightComponent::Type::Point && pointCount < 8) {
                const std::string pn = "lightPositions[" + std::to_string(pointCount) + "]";
                const std::string cn = "lightColors["    + std::to_string(pointCount) + "]";
                lightingShd->setVec3(gl, pn.c_str(), xf.translation);
                // Point intensity is luminous power in LUMENS; convert to luminous
                // intensity (candela) for an isotropic emitter: I = lm / (4*pi). The
                // shader then applies 1/d^2 to get illuminance (lux) at the surface.
                const float candela = lc.intensity * (1.0f / (4.0f * 3.14159265358979f));
                lightingShd->setVec3(gl, cn.c_str(), lc.color * candela);
                ++pointCount;
            }
            else if (lc.type == LightComponent::Type::RectArea && areaCount < 8) {
                // The visible quad is the UNIT quad (+-0.5) scaled by the transform, so the
                // LTC rectangle must use the same unit half-extent -- the transform's scale
                // sets the world size. (Emitters keep transform.scale.xy == lc.size, so the
                // lit rectangle == the editor's Width/Height == the glowing panel.) Using
                // lc.size*0.5 here too would apply the size twice (size*scale).
                const glm::mat4 M = xf.getTransform();
                const float hw = 0.5f, hh = 0.5f;
                const glm::vec3 c0 = glm::vec3(M * glm::vec4(-hw, -hh, 0.0f, 1.0f));
                const glm::vec3 c1 = glm::vec3(M * glm::vec4( hw, -hh, 0.0f, 1.0f));
                const glm::vec3 c2 = glm::vec3(M * glm::vec4( hw,  hh, 0.0f, 1.0f));
                const glm::vec3 c3 = glm::vec3(M * glm::vec4(-hw,  hh, 0.0f, 1.0f));
                const std::string b = "areaLights[" + std::to_string(areaCount) + "].";
                lightingShd->setVec3(gl, (b + "p0").c_str(), c0);
                lightingShd->setVec3(gl, (b + "p1").c_str(), c1);
                lightingShd->setVec3(gl, (b + "p2").c_str(), c2);
                lightingShd->setVec3(gl, (b + "p3").c_str(), c3);
                lightingShd->setVec3(gl, (b + "color").c_str(), lc.color);
                lightingShd->setFloat(gl, (b + "intensity").c_str(), lc.intensity);
                lightingShd->setInt(gl, (b + "twoSided").c_str(), lc.twoSided ? 1 : 0);
                ++areaCount;
            }
            else if (lc.type == LightComponent::Type::Directional && dirCount < 4) {
                // Travel direction = the entity's local +Z, rotated into world space
                // (rotation only, no scale skew). Same convention as RectArea facing.
                const glm::vec3 fwd = glm::normalize(glm::mat3_cast(xf.rotation) * glm::vec3(0.0f, 0.0f, 1.0f));
                const std::string dn = "dirLightDirections[" + std::to_string(dirCount) + "]";
                const std::string dc = "dirLightColors["     + std::to_string(dirCount) + "]";
                lightingShd->setVec3(gl, dn.c_str(), fwd);
                lightingShd->setVec3(gl, dc.c_str(), lc.color * lc.intensity);
                ++dirCount;
            }
            else if (lc.type == LightComponent::Type::Spot && spotCount < 8) {
                const glm::vec3 fwd = glm::normalize(glm::mat3_cast(xf.rotation) * glm::vec3(0.0f, 0.0f, 1.0f));
                const std::string b = "spotLights[" + std::to_string(spotCount) + "].";
                lightingShd->setVec3(gl, (b + "position").c_str(),  xf.translation);
                lightingShd->setVec3(gl, (b + "direction").c_str(), fwd);
                lightingShd->setVec3(gl, (b + "color").c_str(),     lc.color * lc.intensity);
                lightingShd->setFloat(gl, (b + "range").c_str(),    lc.range);
                // cos is monotonically DECREASING, so a smaller half-angle -> larger cosine.
                // Guarantee cosInner >= cosOuter even if the user sets inner > outer, else the
                // shader's smoothing denominator inverts into a razor-thin hard edge.
                float ci = glm::cos(glm::radians(lc.innerConeDeg));
                float co = glm::cos(glm::radians(lc.outerConeDeg));
                if (ci < co) { const float t = ci; ci = co; co = t; }
                lightingShd->setFloat(gl, (b + "cosInner").c_str(), ci);
                lightingShd->setFloat(gl, (b + "cosOuter").c_str(), co);
                ++spotCount;
            }
        }
        // No orbiting fallback light any more: a scene with no light entities is lit by
        // the directional sun + IBL only (the default scene seeds a key + fill).
        lightingShd->setInt(gl, "activeLightCount", pointCount);
        lightingShd->setInt(gl, "activeAreaLightCount", areaCount);
        lightingShd->setInt(gl, "activeDirLightCount", dirCount);
        lightingShd->setInt(gl, "activeSpotLightCount", spotCount);
        static int s_loggedArea = -99;
        if (areaCount != s_loggedArea) { qDebug() << "[LightingPass] activeAreaLightCount =" << areaCount << " activeLightCount =" << pointCount; s_loggedArea = areaCount; }
    }

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

    // Bind LTC LUTs for rectangle area lights (units 8-9)
    if (auto l1 = renderer.getLtc1()) bindTex(l1->getID(), "ltc_1", GL_TEXTURE_2D);
    else                              bindTex(renderer.getDefaultEmissive()->getID(), "ltc_1", GL_TEXTURE_2D);
    if (auto l2 = renderer.getLtc2()) bindTex(l2->getID(), "ltc_2", GL_TEXTURE_2D);
    else                              bindTex(renderer.getDefaultEmissive()->getID(), "ltc_2", GL_TEXTURE_2D);


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