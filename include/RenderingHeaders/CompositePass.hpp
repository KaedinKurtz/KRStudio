#pragma once
#include "IRenderPass.hpp"
#include "components.hpp"
#include <QHash>

class Shader;
class QOpenGLContext;

class CompositePass : public IRenderPass {
public:
    ~CompositePass() override;

    // Gets the composite shader from the RenderingSystem.
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    // Executes the final composite to the screen.
    void execute(const RenderFrameContext& context) override;

    // Cleans up the fullscreen quad VAO for a dying context.
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    // A helper to create the fullscreen quad VAO for a new context.
    void createCompositeVAOForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);

    // Each context gets its own VAO for the fullscreen effect.
    QHash<QOpenGLContext*, GLuint> m_compositeVAOs;
};