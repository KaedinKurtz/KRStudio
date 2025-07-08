#pragma once

#include "IRenderPass.hpp"
#include <QtGui/qopengl.h>
#include <QHash>

// Forward declarations
class QOpenGLContext;
class Shader;

class GridPass : public IRenderPass {
public:
    // The destructor is important for final cleanup, though onContextDestroyed is primary.
    ~GridPass() override;

    // Gets the shared grid shader from the RenderingSystem.
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    // Executes the grid rendering logic for a single frame.
    void execute(const RenderFrameContext& context) override;

    // Cleans up GPU resources for a context that is about to be destroyed.
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    // A helper function to create the VAO/VBO for a specific context.
    void createGridForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);

    // Each OpenGL context gets its own VAO and VBO for the grid plane.
    QHash<QOpenGLContext*, GLuint> m_gridVAOs;
    QHash<QOpenGLContext*, GLuint> m_gridVBOs;
};