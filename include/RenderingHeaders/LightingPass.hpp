#pragma once
#include "IRenderPass.hpp"
#include <QHash>

// Forward declarations
class Shader;
class QOpenGLContext;

class LightingPass : public IRenderPass {
public:
    ~LightingPass() override;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    void createFullscreenQuad(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);

    // Each context gets its own VAO for drawing the fullscreen quad.
    QHash<QOpenGLContext*, GLuint> m_fullscreenVAOs;
};