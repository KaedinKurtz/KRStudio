#include "GridPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "Scene.hpp"
#include "Camera.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

GridPass::~GridPass() {
    if (!m_gridVAOs.isEmpty()) { // Use isEmpty() for QHash
        qWarning() << "GridPass destroyed, but" << m_gridVAOs.size() << "VAOs still exist. This might be a resource leak.";
    }
}

void GridPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    m_gridShader = renderer.getShader("grid");
    if (!m_gridShader) {
        qFatal("GridPass failed to initialize: Could not find 'grid' shader in RenderingSystem.");
    }
}

void GridPass::execute(const RenderFrameContext& context) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    if (!m_gridVAOs.contains(ctx)) {
        createGridForContext(ctx, context.gl);
    }

    auto& registry = context.registry;
    auto* gl = context.gl;

    auto viewG = registry.view<GridComponent, TransformComponent>();
    // FIX: Use size_hint() to check if the view is empty.
    if (viewG.size_hint() == 0) return;

    m_gridShader->use(gl);
    gl->glEnable(GL_POLYGON_OFFSET_FILL);

    // FIX: Use .value() for read-only access to the QHash.
    gl->glBindVertexArray(m_gridVAOs.value(ctx));

    for (auto entity : viewG) {
        auto& grid = viewG.get<GridComponent>(entity);
        if (!grid.masterVisible) continue;

        auto& xf = viewG.get<TransformComponent>(entity);
        // FIX: Get camera position from the context object.
        const float camDist = glm::length(context.camera.getPosition() - xf.translation);

        // FIX: Use the context object for all rendering data.
        m_gridShader->setMat4(gl, "u_viewMatrix", context.view);
        m_gridShader->setMat4(gl, "u_projectionMatrix", context.projection);
        m_gridShader->setMat4(gl, "u_gridModelMatrix", xf.getTransform());
        m_gridShader->setVec3(gl, "u_cameraPos", context.camera.getPosition());
        // ... (set all other grid and fog uniforms) ...

        // This Z-fighting mitigation logic is correct.
        gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl->glDepthMask(GL_TRUE);
        gl->glPolygonOffset(1.0f, 1.0f);
        gl->glDrawArrays(GL_TRIANGLES, 0, 6);

        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->glDepthMask(GL_FALSE);
        gl->glPolygonOffset(-1.0f, -1.0f);
        gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    gl->glDisable(GL_POLYGON_OFFSET_FILL);
    gl->glDepthMask(GL_TRUE);
    gl->glBindVertexArray(0);
}

void GridPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_gridVAOs.contains(dyingContext)) {
        qDebug() << "GridPass: Cleaning up resources for context" << dyingContext;

        // FIX: The "lvalue" fix. Store the ID in a local variable before taking its address.
        GLuint vaoID = m_gridVAOs.value(dyingContext);
        GLuint vboID = m_gridVBOs.value(dyingContext);

        gl->glDeleteVertexArrays(1, &vaoID);
        gl->glDeleteBuffers(1, &vboID);

        m_gridVAOs.remove(dyingContext);
        m_gridVBOs.remove(dyingContext);
    }
}

void GridPass::createGridForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    qDebug() << "GridPass: Creating grid primitives for new context" << ctx;
    GLuint vao = 0, vbo = 0;
    float gridPlaneVertices[] = { -2000.f,0,-2000.f, 2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,2000.f };

    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo);
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices), gridPlaneVertices, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    gl->glBindVertexArray(0);

    // FIX: Use non-const operator[] for writing to the QHash.
    m_gridVAOs[ctx] = vao;
    m_gridVBOs[ctx] = vbo;
}