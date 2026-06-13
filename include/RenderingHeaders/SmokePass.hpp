#pragma once

#include "IRenderPass.hpp"

#include <QtGui/qopengl.h>
#include <unordered_map>

class ViewportWidget;

/**
 * @brief Volumetric render of the SmokeSystem gas grid. Runs after the
 * water/glass composite and before the tonemap so smoke blends in linear
 * HDR and tonemaps with the rest of the frame; reads a depth copy so it is
 * occluded by scene geometry.
 */
class SmokePass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;

private:
    struct Buffers {
        GLuint depthCopyFBO = 0, depthCopyTex = 0;
        int w = 0, h = 0;
    };
    std::unordered_map<ViewportWidget*, Buffers> m_buffers;
    GLuint m_emptyVao = 0;
};
