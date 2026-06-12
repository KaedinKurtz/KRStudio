#pragma once

#include "IRenderPass.hpp"
#include "FluidPass.hpp"

#include <unordered_map>

class ViewportWidget;

/**
 * @brief Screen-space fluid surface (van der Laan 2009 pipeline with the
 * Truong & Yuksel 2018 narrow-range depth filter):
 *
 *   1. sphere-imposter depth sprites  -> R32F view-z (+ D32F occlusion)
 *   2. narrow-range filter            -> separable ping-pong smoothing
 *   3. additive thickness             -> R16F at half resolution
 *   4. composite                      -> refraction (pre-fluid scene copy),
 *      per-channel Beer-Lambert absorption, turbidity scattering, Fresnel
 *      IBL reflection, key-light specular; writes gl_FragDepth
 *
 * FluidAppearance.renderMode == Particles falls back to the legacy point
 * sprite renderer (owned internally) as a debug view.
 */
class FluidSurfacePass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;

private:
    struct SsfBuffers {
        int w = 0, h = 0;
        GLuint depthFBO = 0, depthTex = 0, depthRB = 0; // R32F + depth32
        GLuint smoothFBO[2] = { 0, 0 };
        GLuint smoothTex[2] = { 0, 0 };
        GLuint thickFBO = 0, thickTex = 0;              // R16F, half res
        GLuint sceneCopyTex = 0;                        // RGBA16F, full res
        GLuint sceneDepthCopyFBO = 0, sceneDepthCopyTex = 0; // D32F copy (no feedback loop)
        GLuint backFBO = 0, backTex = 0, backRB = 0;    // RGBA16F exit normal+z (High)
    };

    SsfBuffers& buffersFor(const RenderFrameContext& context);
    void releaseBuffers(QOpenGLFunctions_4_3_Core* gl, SsfBuffers& b);
    void updateFoamAccum(const RenderFrameContext& context);

    std::unordered_map<ViewportWidget*, SsfBuffers> m_buffers;
    FluidPass m_particleFallback; // legacy dots (debug view)
    GLuint m_emptyVao = 0;

    // World-anchored (top-down XZ over the fluid domain) lingering-foam
    // accumulation: max-style additive inject + blur*decay ping-pong.
    // Shared across viewports; stepped once per engine frame.
    static constexpr int kFoamRes = 512;
    GLuint m_foamTex[2] = { 0, 0 };
    GLuint m_foamFBO[2] = { 0, 0 };
    int m_foamIndex = 0;
    float m_lastFoamTime = -1.0f;
};
