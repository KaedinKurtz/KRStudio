#include "SplinePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "Scene.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

// A helper struct to manage GL state changes safely.
struct GLStateSnapshot {
    GLboolean blendEnabled, depthMask, cullFaceEnabled;
};

void saveGLState(QOpenGLFunctions_4_3_Core* gl, GLStateSnapshot& snapshot) {
    gl->glGetBooleanv(GL_BLEND, &snapshot.blendEnabled);
    gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &snapshot.depthMask);
    gl->glGetBooleanv(GL_CULL_FACE, &snapshot.cullFaceEnabled);
}

void restoreGLState(QOpenGLFunctions_4_3_Core* gl, const GLStateSnapshot& snapshot) {
    if (snapshot.blendEnabled) gl->glEnable(GL_BLEND); else gl->glDisable(GL_BLEND);
    gl->glDepthMask(snapshot.depthMask);
    if (snapshot.cullFaceEnabled) gl->glEnable(GL_CULL_FACE); else gl->glDisable(GL_CULL_FACE);
}


// --- Class Implementation ---

SplinePass::~SplinePass() {
    if (!m_lineVAOs.isEmpty()) { // FIX: Use isEmpty() for QHash
        qWarning() << "SplinePass destroyed, but resources still exist.";
    }
}

void SplinePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    m_glowShader = renderer.getShader("glow");
    m_capShader = renderer.getShader("cap");
    if (!m_glowShader || !m_capShader) {
        qFatal("SplinePass failed to initialize: Could not find 'glow' or 'cap' shader.");
    }
}

void SplinePass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    if (!m_lineVAOs.contains(ctx)) {
        createPrimitivesForContext(ctx, gl);
    }

    GLStateSnapshot stateBefore;
    saveGLState(gl, stateBefore);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDepthMask(GL_FALSE);
    gl->glDisable(GL_CULL_FACE);

    auto& registry = context.registry;
    auto splineView = registry.view<const SplineComponent>();

    for (auto entity : splineView) {
        const auto& sp = splineView.get<const SplineComponent>(entity);
        if (sp.cachedVertices.size() < 2) continue;

        // --- Draw Glow Lines ---
        m_glowShader->use(gl);
        m_glowShader->setMat4(gl, "u_view", context.view);
        m_glowShader->setMat4(gl, "u_proj", context.projection);
        m_glowShader->setFloat(gl, "u_thickness", sp.thickness);
        m_glowShader->setVec2(gl, "u_viewport_size", glm::vec2(context.viewportWidth, context.viewportHeight));
        m_glowShader->setVec4(gl, "u_glowColour", sp.glowColour);
        m_glowShader->setVec4(gl, "u_coreColour", sp.coreColour);

        std::vector<glm::vec3> lineSegments;
        lineSegments.reserve((sp.cachedVertices.size() - 1) * 2);
        for (size_t i = 0; i < sp.cachedVertices.size() - 1; ++i) {
            lineSegments.push_back(sp.cachedVertices[i]);
            lineSegments.push_back(sp.cachedVertices[i + 1]);
        }

        gl->glBindVertexArray(m_lineVAOs.value(ctx));
        gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVBOs.value(ctx));
        gl->glBufferData(GL_ARRAY_BUFFER, lineSegments.size() * sizeof(glm::vec3), lineSegments.data(), GL_DYNAMIC_DRAW);
        gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineSegments.size()));

        // --- Draw End Caps ---
        m_capShader->use(gl);
        m_capShader->setMat4(gl, "u_view", context.view);
        m_capShader->setMat4(gl, "u_proj", context.projection);
        m_capShader->setVec2(gl, "u_viewport_size", glm::vec2(context.viewportWidth, context.viewportHeight));
        m_capShader->setFloat(gl, "u_thickness", sp.thickness);
        m_capShader->setVec4(gl, "u_glowColour", sp.glowColour);
        m_capShader->setVec4(gl, "u_coreColour", sp.coreColour);

        std::vector<glm::vec3> capPoints;
        if (sp.type == SplineType::Linear) {
            capPoints = sp.controlPoints;
        }
        else {
            capPoints = { sp.cachedVertices.front(), sp.cachedVertices.back() };
        }

        if (!capPoints.empty()) {
            gl->glBindVertexArray(m_capVAOs.value(ctx));
            gl->glBindBuffer(GL_ARRAY_BUFFER, m_capVBOs.value(ctx));
            gl->glBufferData(GL_ARRAY_BUFFER, capPoints.size() * sizeof(glm::vec3), capPoints.data(), GL_DYNAMIC_DRAW);
            gl->glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(capPoints.size()));
        }
    }

    restoreGLState(gl, stateBefore);
    gl->glBindVertexArray(0);
}

void SplinePass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_lineVAOs.contains(dyingContext)) {
        qDebug() << "SplinePass: Cleaning up resources for context" << dyingContext;

        // FIX: The "lvalue" fix for glDelete* functions.
        GLuint lineVAO_ID = m_lineVAOs.value(dyingContext);
        GLuint lineVBO_ID = m_lineVBOs.value(dyingContext);
        GLuint capVAO_ID = m_capVAOs.value(dyingContext);
        GLuint capVBO_ID = m_capVBOs.value(dyingContext);

        gl->glDeleteVertexArrays(1, &lineVAO_ID);
        gl->glDeleteBuffers(1, &lineVBO_ID);
        gl->glDeleteVertexArrays(1, &capVAO_ID);
        gl->glDeleteBuffers(1, &capVBO_ID);

        m_lineVAOs.remove(dyingContext);
        m_lineVBOs.remove(dyingContext);
        m_capVAOs.remove(dyingContext);
        m_capVBOs.remove(dyingContext);
    }
}

void SplinePass::createPrimitivesForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    qDebug() << "SplinePass: Creating line and cap primitives for context" << ctx;

    GLuint lineVAO = 0, lineVBO = 0, capVAO = 0, capVBO = 0;

    gl->glGenVertexArrays(1, &lineVAO);
    gl->glGenBuffers(1, &lineVBO);
    gl->glBindVertexArray(lineVAO);
    gl->glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    gl->glGenVertexArrays(1, &capVAO);
    gl->glGenBuffers(1, &capVBO);
    gl->glBindVertexArray(capVAO);
    gl->glBindBuffer(GL_ARRAY_BUFFER, capVBO);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    gl->glBindVertexArray(0);

    // FIX: Use non-const operator[] for writing to the QHash.
    m_lineVAOs[ctx] = lineVAO;
    m_lineVBOs[ctx] = lineVBO;
    m_capVAOs[ctx] = capVAO;
    m_capVBOs[ctx] = capVBO;
}