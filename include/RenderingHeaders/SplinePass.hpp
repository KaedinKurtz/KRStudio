#pragma once
#include "IRenderPass.hpp"
#include <QtGui/qopengl.h>
#include <QHash>

class Shader;
class QOpenGLContext;

class SplinePass : public IRenderPass {
public:
    ~SplinePass() override;

    // Gets the "glow" and "cap" shaders from the RenderingSystem.
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    // Renders all SplineComponent entities.
    void execute(const RenderFrameContext& context) override;

    // Cleans up the line and cap VAOs/VBOs for a dying context.
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    // Helper to create the VAOs/VBOs for a new context.
    void createPrimitivesForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);

    // --- Member Variables ---
    Shader* m_glowShader = nullptr;
    Shader* m_capShader = nullptr;

    // Each context gets its own set of primitives.
    QHash<QOpenGLContext*, GLuint> m_lineVAOs;
    QHash<QOpenGLContext*, GLuint> m_lineVBOs;
    QHash<QOpenGLContext*, GLuint> m_capVAOs;
    QHash<QOpenGLContext*, GLuint> m_capVBOs;
};