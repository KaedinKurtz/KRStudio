#include "SplinePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "Scene.hpp"
#include "PrimitiveBuilders.hpp"

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

struct GLStateSaver {
    GLStateSaver(QOpenGLFunctions_4_3_Core* funcs) : gl(funcs) {
        gl->glGetBooleanv(GL_BLEND, &blendEnabled);
        gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        gl->glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
    }
    // ...and its destructor automatically restores the state when it goes out of scope.
    ~GLStateSaver() {
        if (blendEnabled) gl->glEnable(GL_BLEND); else gl->glDisable(GL_BLEND);
        gl->glDepthMask(depthMask); // This is the most critical line to fix the bug.
        if (cullFaceEnabled) gl->glEnable(GL_CULL_FACE); else gl->glDisable(GL_CULL_FACE);
    }
    QOpenGLFunctions_4_3_Core* gl;
    GLboolean blendEnabled, depthMask, cullFaceEnabled;
};

// --- Class Implementation ---

SplinePass::~SplinePass() {
    if (!m_lineVAOs.isEmpty()) { // FIX: Use isEmpty() for QHash
        qWarning() << "SplinePass destroyed, but resources still exist.";
    }
}

void SplinePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {

}

void SplinePass::execute(const RenderFrameContext& context) {
    Shader* glowShader = context.renderer.getShader("glow");
    Shader* capShader = context.renderer.getShader("cap");
    if (!glowShader || !capShader) return;

    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    if (!m_lineVAOs.contains(ctx)) {
        createPrimitivesForContext(ctx, gl);
    }

    // REMOVED: The GLStateSaver is no longer used.
    // GLStateSaver stateSaver(gl);

    // --- Manually set the state needed for this pass ---
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDepthMask(GL_FALSE);      // Disable depth writes for the glow effect.
    gl->glDisable(GL_CULL_FACE);    // Splines are 2D so they don't need culling.

    auto& registry = context.registry;
    auto splineView = registry.view<const SplineComponent>();

    for (auto entity : splineView) {
        const auto& sp = splineView.get<const SplineComponent>(entity);
        if (sp.cachedVertices.size() < 2) continue;

        // --- Draw Glow Lines ---
        glowShader->use(gl);
        glowShader->setMat4(gl, "u_view", context.view);
        glowShader->setMat4(gl, "u_proj", context.projection);
        glowShader->setFloat(gl, "u_thickness", sp.thickness);
        glowShader->setVec2(gl, "u_viewport_size", glm::vec2(context.viewportWidth, context.viewportHeight));
        glowShader->setVec4(gl, "u_glowColour", sp.glowColour);
        glowShader->setVec4(gl, "u_coreColour", sp.coreColour);

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
        capShader->use(gl);
        capShader->setMat4(gl, "u_view", context.view);
        capShader->setMat4(gl, "u_proj", context.projection);
        capShader->setVec2(gl, "u_viewport_size", glm::vec2(context.viewportWidth, context.viewportHeight));
        capShader->setFloat(gl, "u_thickness", sp.thickness);
        capShader->setVec4(gl, "u_glowColour", sp.glowColour);
        capShader->setVec4(gl, "u_coreColour", sp.coreColour);

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

    gl->glBindVertexArray(0);

    // --- Manually restore the GL state to a clean default for the next pass ---
    gl->glDepthMask(GL_TRUE);      // CRITICAL: Re-enable depth writing.
    gl->glDisable(GL_BLEND);     // Disable blending.
    gl->glEnable(GL_CULL_FACE);  // Re-enable face culling for the next opaque pass.
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

void SplinePass::createPrimitivesForContext(QOpenGLContext* ctx,
    QOpenGLFunctions_4_3_Core* gl)
{
    qDebug() << "SplinePass: Creating line and cap primitives for context" << ctx;

    // LINE PRIMITIVE
    GLuint lineVAO = 0, lineVBO = 0;
    gl->glGenVertexArrays(1, &lineVAO);
    gl->glGenBuffers(1, &lineVBO);
    // this binds VAO, binds VBO, uploads a unit?length line,
    // sets up attribs, then unbinds VAO.
    buildUnitLine(gl, lineVAO, lineVBO);

    // CAP (triangle) PRIMITIVE
    GLuint capVAO = 0, capVBO = 0;
    gl->glGenVertexArrays(1, &capVAO);
    gl->glGenBuffers(1, &capVBO);
    // this binds VAO, binds VBO, uploads a little triangle,
    // sets up attribs, then unbinds VAO.
    buildCap(gl, capVAO, capVBO);

    // stash them for draw() later
    m_lineVAOs[ctx] = lineVAO;
    m_lineVBOs[ctx] = lineVBO;
    m_capVAOs[ctx] = capVAO;
    m_capVBOs[ctx] = capVBO;
}