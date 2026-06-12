#pragma once

#include "IRenderPass.hpp"

#include <QtGui/qopengl.h>
#include <unordered_map>

class ViewportWidget;

/**
 * @brief End-of-frame display transform (ACES + gamma). The lighting pass
 * outputs linear HDR radiance so the fluid composite can refract/absorb in
 * physical units; this pass converts finalColorTexture to display space
 * right before the display-space overlays (gizmo). KRS_HDR=0 disables the
 * whole HDR ordering (lighting falls back to in-shader Reinhard) and this
 * pass becomes a no-op.
 */
class TonemapPass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;

private:
    struct Scratch {
        GLuint tex = 0;
        int w = 0, h = 0;
    };
    std::unordered_map<ViewportWidget*, Scratch> m_scratch;
    GLuint m_emptyVao = 0;
};
