#pragma once

#include "IRenderPass.hpp"

#include <QtGui/qopengl.h>
#include <unordered_map>

class ViewportWidget;

/**
 * @brief Transparent refractive surfaces (GlassComponent): runs after the
 * water composite and before the tonemap, so glass refracts the lit HDR
 * scene INCLUDING the water. Copies the pre-glass frame aside per viewport,
 * then renders each glass mesh forward with screen-space refraction.
 */
class GlassPass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;

private:
    struct Scratch {
        GLuint colorTex = 0;
        GLuint depthFBO = 0, depthTex = 0;
        int w = 0, h = 0;
    };
    std::unordered_map<ViewportWidget*, Scratch> m_scratch;
};
