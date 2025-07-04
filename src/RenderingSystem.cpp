#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "PrimitiveBuilders.hpp"
#include "FieldSolver.hpp" // Included for the new FieldSolver integration

#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLContext> // Required for per-context resource management
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <QDebug>
#include <stdexcept>
#include <string>
#include <QSize>
#include <QSurface>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <random>

#define CHECK_GL_ERROR()                                                       \
    do {                                                                       \
        GLenum err;                                                            \
        while ((err = m_gl->glGetError()) != GL_NO_ERROR) {                    \
            qCritical() << "!!! OpenGL Error:" << err << "at line" << __LINE__ \
                        << "in file" << __FILE__;                              \
        }                                                                      \
    } while (0)

struct GLStateSnapshot {
    GLboolean blendEnabled;
    GLboolean depthMask;
    GLboolean cullFaceEnabled;
    GLint blendSrcRGB;
    GLint blendDstRGB;
    GLint blendSrcAlpha;
    GLint blendDstAlpha;
    GLint blendEquationRGB;
    GLint blendEquationAlpha;
};

void restoreGLState(QOpenGLFunctions_4_3_Core* gl, const GLStateSnapshot& snapshot) {
    if (snapshot.blendEnabled) gl->glEnable(GL_BLEND);
    else gl->glDisable(GL_BLEND);

    gl->glDepthMask(snapshot.depthMask);

    if (snapshot.cullFaceEnabled) gl->glEnable(GL_CULL_FACE);
    else gl->glDisable(GL_CULL_FACE);

    gl->glBlendFuncSeparate(snapshot.blendSrcRGB, snapshot.blendDstRGB, snapshot.blendSrcAlpha, snapshot.blendDstAlpha);
    gl->glBlendEquationSeparate(snapshot.blendEquationRGB, snapshot.blendEquationAlpha);
}

void saveGLState(QOpenGLFunctions_4_3_Core* gl, GLStateSnapshot& snapshot) {
    gl->glGetBooleanv(GL_BLEND, &snapshot.blendEnabled);
    gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &snapshot.depthMask);
    gl->glGetBooleanv(GL_CULL_FACE, &snapshot.cullFaceEnabled);
    gl->glGetIntegerv(GL_BLEND_SRC_RGB, &snapshot.blendSrcRGB);
    gl->glGetIntegerv(GL_BLEND_DST_RGB, &snapshot.blendDstRGB);
    gl->glGetIntegerv(GL_BLEND_SRC_ALPHA, &snapshot.blendSrcAlpha);
    gl->glGetIntegerv(GL_BLEND_DST_ALPHA, &snapshot.blendDstAlpha);
    gl->glGetIntegerv(GL_BLEND_EQUATION_RGB, &snapshot.blendEquationRGB);
    gl->glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &snapshot.blendEquationAlpha);
}

static const char* fbStatusStr(GLenum s)
{
    switch (s)
    {
    case GL_FRAMEBUFFER_COMPLETE: return "COMPLETE";
    case GL_FRAMEBUFFER_UNDEFINED: return "UNDEFINED";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: return "INCOMPLETE_DRAW_BUFFER";
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: return "INCOMPLETE_READ_BUFFER";
    case GL_FRAMEBUFFER_UNSUPPORTED: return "UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "INCOMPLETE_MULTISAMPLE";
    default: return "UNKNOWN";
    }
}
static void dumpCompositeUniforms(QOpenGLFunctions_4_3_Core* gl, Shader* sh)
{
    GLint sceneLoc = sh->getLoc("sceneTexture");   // will throw if not found :contentReference[oaicite:0]{index=0}
    GLint glowLoc = sh->getLoc("glowTexture");
    qDebug().nospace() << "[Composite] sceneTexture loc = "
        << sceneLoc << " , glowTexture loc = "
        << glowLoc;
}

static void dumpCompositeAttributes(QOpenGLFunctions_4_3_Core* gl, Shader* sh)
{
    GLint posAttrib = gl->glGetAttribLocation(sh->ID, "pos");   // `ID` is public :contentReference[oaicite:1]{index=1}
    qDebug().nospace() << "[Composite] expects attribute 'pos' ? "
        << (posAttrib != -1);
}

/*  When you suspect the texture itself is empty, call this.
    It reads the very first pixel so you can see whether anything
    besides pure-black ever makes it into the render target.   */
static void debugTexturePixel(QOpenGLFunctions_4_3_Core* gl,
    GLuint tex, const char* label)
{
    GLuint fbo = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, tex, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        qWarning() << "[DebugTex] FBO incomplete for" << label;
    }
    else
    {
        float px[4] = { 0 };
        gl->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px);
        qDebug().nospace() << "[DebugTex] " << label << " first-pixel = "
            << px[0] << ", " << px[1] << ", "
            << px[2] << ", " << px[3];
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
}

//---------- HELPER FUNCTIONS (Formatted for Clarity) ------------------

// Evaluates a Catmull-Rom spline on the CPU.
static std::vector<glm::vec3> evaluateCatmullRomCPU(const std::vector<glm::vec3>& controlPoints, int segmentsPerCurve)
{
    std::vector<glm::vec3> lineVertices;
    if (controlPoints.size() < 4) { // A segment requires at least 4 control points.
        return lineVertices;
    }

    lineVertices.reserve(static_cast<size_t>(controlPoints.size() - 3) * segmentsPerCurve); // Pre-allocate memory for efficiency.

    // Iterate through each 4-point segment of the spline.
    for (size_t i = 0; i < controlPoints.size() - 3; ++i) {
        const glm::vec3& p0 = controlPoints[i];
        const glm::vec3& p1 = controlPoints[i + 1];
        const glm::vec3& p2 = controlPoints[i + 2];
        const glm::vec3& p3 = controlPoints[i + 3];

        // Generate the points for the current segment.
        for (int j = 0; j < segmentsPerCurve; ++j) {
            float t = static_cast<float>(j) / (segmentsPerCurve - 1); // Parameter t from 0 to 1.
            float t2 = t * t;
            float t3 = t2 * t;

            // Catmull-Rom interpolation formula.
            glm::vec3 point = 0.5f * (
                (2.0f * p1) +
                (-p0 + p2) * t +
                (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
                );
            lineVertices.push_back(point);
        }
    }
    return lineVertices;
}

// For linear "splines", the vertices are just the control points themselves.
static std::vector<glm::vec3> evaluateLinearCPU(const std::vector<glm::vec3>& controlPoints) {
    return controlPoints;
}

// Evaluates a Bezier curve on the CPU.
static std::vector<glm::vec3> evaluateBezierCPU(const std::vector<glm::vec3>& controlPoints, int numSegments) {
    std::vector<glm::vec3> lineVertices;
    if (controlPoints.empty()) {
        return lineVertices;
    }
    lineVertices.reserve(numSegments); // Pre-allocate memory.

    // Lambda to calculate binomial coefficients (nCk).
    auto binomialCoeff = [](int n, int k) {
        long long res = 1;
        if (k > n - k) k = n - k;
        for (int i = 0; i < k; ++i) {
            res = res * (n - i);
            res = res / (i + 1);
        }
        return static_cast<float>(res);
        };

    int n = static_cast<int>(controlPoints.size()) - 1; // Degree of the curve.
    for (int i = 0; i < numSegments; ++i) {
        float t = static_cast<float>(i) / (numSegments - 1); // Parameter t from 0 to 1.
        glm::vec3 point(0.0f);
        // Sum up the control points weighted by the Bernstein polynomials.
        for (int j = 0; j <= n; ++j) {
            float bernstein = binomialCoeff(n, j) * pow(t, j) * pow(1 - t, n - j);
            point += controlPoints[j] * bernstein;
        }
        lineVertices.push_back(point);
    }
    return lineVertices;
}

// Evaluates a user-defined parametric function on the CPU.
static std::vector<glm::vec3> evaluateParametricCPU(const std::function<glm::vec3(float)>& func, int numSegments) {
    std::vector<glm::vec3> lineVertices;
    if (!func) { // Ensure the function object is valid.
        return lineVertices;
    }
    lineVertices.reserve(numSegments);
    for (int i = 0; i < numSegments; ++i) {
        float t = static_cast<float>(i) / (numSegments - 1); // Parameter t from 0 to 1.
        lineVertices.push_back(func(t)); // Evaluate the function at t.
    }
    return lineVertices;
}

// Calculates the shortest rotation quaternion between two vectors.
static glm::quat rotationBetweenVectors(glm::vec3 from, glm::vec3 to) {
    from = glm::normalize(from);
    to = glm::normalize(to);

    float cosTheta = glm::dot(from, to);
    glm::vec3 rotationAxis;

    if (cosTheta < -1.0f + 0.001f) {
        // Special case for opposite vectors: find an arbitrary perpendicular axis.
        rotationAxis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), from);
        if (glm::length2(rotationAxis) < 0.01f) // If 'from' is parallel to Z, use X axis.
            rotationAxis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);

        rotationAxis = glm::normalize(rotationAxis);
        return glm::angleAxis(glm::radians(180.0f), rotationAxis);
    }

    rotationAxis = glm::cross(from, to);
    float s = sqrt((1.0f + cosTheta) * 2.0f);
    float invs = 1.0f / s;

    return glm::quat(s * 0.5f, rotationAxis.x * invs, rotationAxis.y * invs, rotationAxis.z * invs);
}

// Linearly interpolates a color from a gradient based on a normalized value.
static glm::vec4 getColorFromGradient(float value, const std::vector<ColorStop>& gradient) {
    if (gradient.empty()) return glm::vec4(1.0f); // Default to white if gradient is undefined.
    if (value <= gradient.front().position) return gradient.front().color; // Clamp to start color.
    if (value >= gradient.back().position) return gradient.back().color; // Clamp to end color.

    // Find the two color stops the value lies between.
    for (size_t i = 0; i < gradient.size() - 1; ++i) {
        if (value >= gradient[i].position && value <= gradient[i + 1].position) {
            // Normalize the value within the range of the two stops.
            float t = (value - gradient[i].position) / (gradient[i + 1].position - gradient[i].position);
            // Interpolate the colors.
            return glm::mix(gradient[i].color, gradient[i + 1].color, t);
        }
    }
    return gradient.back().color; // Fallback.
}

static QOpenGLFunctions_4_3_Core* resolveGl41(QOpenGLContext* ctx)
{
    if (!ctx) return nullptr;                     // no context – nothing to do
    auto* f = QOpenGLVersionFunctionsFactory
        ::get<QOpenGLFunctions_4_3_Core>(ctx); // always available
    if (f) f->initializeOpenGLFunctions();        // one-time init
    return f;
}

void RenderingSystem::checkAndLogGlError(const char* label) {
    GLenum err;
    while ((err = m_gl->glGetError()) != GL_NO_ERROR) {
        qCritical() << "[GL ERROR] After" << label << "- Code:" << err;
    }
}

void RenderingSystem::ensureGlResolved()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();

    // 1 — Early-out: still on the same context and the function table is valid
    if (ctx == m_lastContext && m_gl)
        return;

    qDebug() << "OpenGL context changed. Re-resolving function pointers for"
        << ctx;

    // 2 — Lost the context → clear cache and bail
    if (!ctx) {
        m_gl = nullptr;
        m_lastContext = nullptr;
        return;
    }

    // 3 — Grab (or re-grab) the function table for *this* context
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(ctx);
    if (!m_gl) {
        qCritical() << "Failed to obtain GL 4.3 core functions!";
        m_lastContext = nullptr;
        return;
    }
    m_gl->initializeOpenGLFunctions();

    // 4 — Auto-invalidate the cache when this context is about to die
    //     (DirectConnection: slot runs *inside* the context-shutdown thread)
    QObject::connect(ctx, &QOpenGLContext::aboutToBeDestroyed,
        ctx,                     // <- any QObject* is OK
        [this, ctx]
        {
            if (ctx == m_lastContext) {
                m_gl = nullptr;
                m_lastContext = nullptr;
            }
        },
        Qt::DirectConnection);

    // 5 — Remember which context we’re bound to
    m_lastContext = ctx;
}

QOpenGLContext* RenderingSystem::currentCtxOrNull()
{
    // tiny utility: never throws, never logs
    return QOpenGLContext::currentContext();
}

//---------- CONSTRUCTOR / DESTRUCTOR ------------------

RenderingSystem::RenderingSystem(QOpenGLWidget* w, QOpenGLFunctions_4_3_Core* gl, QObject* parent)
    :m_viewportWidget(w), m_gl(gl)
{
    m_fieldSolver = std::make_unique<FieldSolver>();
}

RenderingSystem::~RenderingSystem() {}

//---------- PUBLIC: RENDER LOOP & LIFECYCLE MANAGEMENT ------------------

void RenderingSystem::initialize(int w, int h)
{
    ensureGlResolved();
    if (!m_gl) {
        qFatal("RenderingSystem::initialize – no current context was provided.");
    }

    // +++ ADD THIS DIAGNOSTIC CODE +++
    const GLubyte* glVersion = m_gl->glGetString(GL_VERSION);
    const GLubyte* glslVersion = m_gl->glGetString(GL_SHADING_LANGUAGE_VERSION);
    qDebug() << "========================================================";
    qDebug() << "[DRIVER_INFO] Actual GL Version:" << (const char*)glVersion;
    qDebug() << "[DRIVER_INFO] Actual GLSL Version:" << (const char*)glslVersion;
    qDebug() << "========================================================";
    // ++++++++++++++++++++++++++++++++


    initShaders();

    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_gl->glGenBuffers(1, &m_debugBuffer);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_debugBuffer);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 4096 * 16, nullptr, GL_DYNAMIC_COPY);

    // Create a buffer for the atomic counter, initialized to 0
    m_gl->glGenBuffers(1, &m_debugAtomicCounter);
    m_gl->glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_debugAtomicCounter);
    m_gl->glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    GLuint zero = 0;
    m_gl->glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    m_isInitialized = true;
}

void RenderingSystem::shutdown(entt::registry& registry) {
    if (!m_gl) return;

    qDebug() << "[LIFECYCLE] Shutting down per-context GPU resources.";

    // Per-context primitives (VAOs for grid, lines, etc.)
    for (auto const& primitives : m_contextPrimitives) {
        if (primitives.gridVAO) m_gl->glDeleteVertexArrays(1, &primitives.gridVAO);
        if (primitives.gridVBO) m_gl->glDeleteBuffers(1, &primitives.gridVBO);
        if (primitives.lineVAO) m_gl->glDeleteVertexArrays(1, &primitives.lineVAO);
        if (primitives.lineVBO) m_gl->glDeleteBuffers(1, &primitives.lineVBO);
        if (primitives.capVAO) m_gl->glDeleteVertexArrays(1, &primitives.capVAO);
        if (primitives.capVBO) m_gl->glDeleteBuffers(1, &primitives.capVBO);
        if (primitives.compositeVAO) m_gl->glDeleteVertexArrays(1, &primitives.compositeVAO);
        if (primitives.arrowVAO) m_gl->glDeleteVertexArrays(1, &primitives.arrowVAO);
        if (primitives.arrowVBO) m_gl->glDeleteBuffers(1, &primitives.arrowVBO);
        if (primitives.arrowEBO) m_gl->glDeleteBuffers(1, &primitives.arrowEBO);
        if (primitives.instanceVBO) m_gl->glDeleteBuffers(1, &primitives.instanceVBO);
    }

    if (m_debugBuffer) m_gl->glDeleteBuffers(1, &m_debugBuffer);
    if (m_debugAtomicCounter) m_gl->glDeleteBuffers(1, &m_debugAtomicCounter);

    m_contextPrimitives.clear();

    // Per-viewport FBOs
    for (auto const& [widget, target] : m_targets) {
        m_gl->glDeleteFramebuffers(1, &target.mainFBO);
        m_gl->glDeleteTextures(1, &target.mainColorTexture);
        m_gl->glDeleteTextures(1, &target.mainDepthTexture);
        m_gl->glDeleteFramebuffers(1, &target.glowFBO);
        m_gl->glDeleteTextures(1, &target.glowTexture);
        m_gl->glDeleteFramebuffers(2, target.pingpongFBO);
        m_gl->glDeleteTextures(2, target.pingpongTexture);
    }
    m_targets.clear();

    qDebug() << "[LIFECYCLE] Shutting down per-entity GPU resources.";
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view)
    {
        auto& res = view.get<RenderResourceComponent>(entity);
        for (auto const& [context, buffers] : res.perContext) {
            if (buffers.VAO) m_gl->glDeleteVertexArrays(1, &buffers.VAO);
            if (buffers.VBO) m_gl->glDeleteBuffers(1, &buffers.VBO);
            if (buffers.EBO) m_gl->glDeleteBuffers(1, &buffers.EBO);
        }
        res.perContext.clear();
    }

    auto visualizerView = registry.view<FieldVisualizerComponent>();
    for (auto entity : visualizerView) {
        auto& vis = visualizerView.get<FieldVisualizerComponent>(entity);
        // Cleanup particle buffers
        if (vis.particleVAO) m_gl->glDeleteVertexArrays(1, &vis.particleVAO);
        if (vis.particleBuffer[0]) m_gl->glDeleteBuffers(2, vis.particleBuffer);
        vis.particleVAO = 0;
        vis.particleBuffer[0] = 0;
        vis.particleBuffer[1] = 0;

        // Cleanup arrow buffers
        if (vis.gpuData.samplePointsSSBO) m_gl->glDeleteBuffers(1, &vis.gpuData.samplePointsSSBO);
        if (vis.gpuData.instanceDataSSBO) m_gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
        if (vis.gpuData.commandUBO) m_gl->glDeleteBuffers(1, &vis.gpuData.commandUBO);
    }
    // Reset all shader pointers
    m_phongShader.reset();
    m_gridShader.reset();
    m_outlineShader.reset();
    m_splineShader.reset();
    m_lineShader.reset();
    m_glowShader.reset();
    m_capShader.reset();
    m_instancedArrowShader.reset();
    m_emissiveSolidShader.reset();
    m_blurShader.reset();
    m_compositeShader.reset();

    // Delete remaining globally shared resources
    m_gl->glDeleteVertexArrays(1, &m_intersectionVAO);
    m_gl->glDeleteBuffers(1, &m_intersectionVBO);

    m_isInitialized = false;
    m_gl = nullptr;
    m_lastContext = nullptr;
}

void RenderingSystem::resize(int reqW, int reqH)
{
    if (reqW <= 0 || reqH <= 0) return;                 // guard

    const bool needsRealloc = (reqW > m_width) || (reqH > m_height);
    if (!needsRealloc) return;                          // already big enough

    /* grow to the *maximum* requested so far */
    m_width = std::max(reqW, m_width);
    m_height = std::max(reqH, m_height);

    /* recreate all size-dependent GPU resources ----------------------- */
    initFramebuffers(m_width, m_height);                // tears down & rebuilds
    qInfo() << "[RenderingSystem] grew FBOs to"
        << m_width << "x" << m_height;
}

inline void RenderingSystem::ensureSize(int w, int h)
{
    if (w != m_width || h != m_height) resize(w, h);
}

void RenderingSystem::beginFrame(entt::registry& registry)
{
    //
    // [DEPRECATED] This function was part of the old single-render pipeline. 
    // Its logic (binding and clearing the main FBO) is now handled at the
    // start of the new renderView() function for each viewport individually.
    //
}

void RenderingSystem::renderSceneToFBOs(entt::registry& registry, const Camera& primaryCamera)
{
    //
    // [DEPRECATED] This function was the core of the old single-render pipeline.
    // All the render passes it used to call (renderMeshes, renderGrid, etc.)
    // are now called directly from within the new renderView() function.
    //
}

bool RenderingSystem::isRenderCtx(const QOpenGLContext* ctx) const noexcept
{
    return ctx && (ctx == m_ownerCtx || ctx->shareGroup() == m_ownerCtx->shareGroup());
}

// This is the cheap function, run ONCE PER VIEWPORT.
// <<< ADDED "RenderingSystem::" SCOPE >>>
void RenderingSystem::compositeToScreen(int vpW, int vpH)
{
    //
    // [DEPRECATED] This function's logic has been merged into the end of the
    // new renderView() function. That function now handles the final composite
    // pass to draw the result to the screen for each viewport.
    //
}
//---------- RENDER PASS IMPLEMENTATIONS ------------------

void RenderingSystem::renderMeshes(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos) {
    if (!m_phongShader) return;

    m_phongShader->use();
    m_phongShader->setMat4("view", view);
    m_phongShader->setMat4("projection", projection);
    m_phongShader->setVec3("lightColor", glm::vec3(1.0f));
    m_phongShader->setVec3("lightPos", glm::vec3(5.0f, 10.0f, 5.0f));
    m_phongShader->setVec3("viewPos", camPos);

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto viewRM = registry.view<RenderableMeshComponent, TransformComponent>();

    for (auto entity : viewRM) {
        if (entity == m_currentCamera || isDescendantOf(registry, entity, m_currentCamera)) {
            continue;
        }

        auto& mesh = viewRM.get<RenderableMeshComponent>(entity);
        auto& xf = viewRM.get<TransformComponent>(entity);

        auto* mat = registry.try_get<MaterialComponent>(entity);
        m_phongShader->setVec3("objectColor", mat ? mat->albedo : glm::vec3(0.8f));

        auto& res = registry.get_or_emplace<RenderResourceComponent>(entity);
        auto& buf = res.perContext[ctx];

        if (buf.VAO == 0) {
            m_gl->glGenVertexArrays(1, &buf.VAO);
            m_gl->glGenBuffers(1, &buf.VBO);
            m_gl->glGenBuffers(1, &buf.EBO);
            m_gl->glBindVertexArray(buf.VAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, buf.VBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);
            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.EBO);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned), mesh.indices.data(), GL_STATIC_DRAW);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
            m_gl->glEnableVertexAttribArray(1);
            m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
            m_gl->glBindVertexArray(0);
        }

        glm::mat4 modelMatrix = registry.all_of<WorldTransformComponent>(entity)
            ? registry.get<WorldTransformComponent>(entity).matrix
            : xf.getTransform();

        m_phongShader->setMat4("model", modelMatrix);
        m_gl->glBindVertexArray(buf.VAO);
        m_gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    m_gl->glBindVertexArray(0);
}

void RenderingSystem::renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos)
{
    if (!m_gridShader) return;

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto& primitives = m_contextPrimitives[ctx];

    if (primitives.gridVAO == 0) {
        qDebug() << "Creating grid primitives for context" << ctx;
        float gridPlaneVertices[] = { -2000.f,0,-2000.f, 2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,2000.f };
        m_gl->glGenVertexArrays(1, &primitives.gridVAO);
        m_gl->glGenBuffers(1, &primitives.gridVBO);
        m_gl->glBindVertexArray(primitives.gridVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, primitives.gridVBO);
        m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices), gridPlaneVertices, GL_STATIC_DRAW);
        m_gl->glEnableVertexAttribArray(0);
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        // Connect this context's destruction signal to our cleanup slot.
         
    }

    const auto drawQuad = [&] {
        m_gl->glBindVertexArray(primitives.gridVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        };

    auto viewG = registry.view<GridComponent, TransformComponent>();
    if (viewG.begin() == viewG.end()) return;

    m_gridShader->use();
    m_gl->glEnable(GL_POLYGON_OFFSET_FILL);

    for (auto entity : viewG) {
        auto& grid = viewG.get<GridComponent>(entity);
        if (!grid.masterVisible) continue;

        auto& xf = viewG.get<TransformComponent>(entity);
        const float camDist = glm::length(camPos - xf.translation);

        // Send all grid uniforms to the shader.
        m_gridShader->setMat4("u_viewMatrix", view);
        m_gridShader->setMat4("u_projectionMatrix", projection);
        m_gridShader->setMat4("u_gridModelMatrix", xf.getTransform());
        m_gridShader->setVec3("u_cameraPos", camPos);
        m_gridShader->setFloat("u_distanceToGrid", camDist);
        m_gridShader->setBool("u_isDotted", grid.isDotted);
        m_gridShader->setFloat("u_baseLineWidthPixels", grid.baseLineWidthPixels);
        m_gridShader->setBool("u_showAxes", grid.showAxes);
        m_gridShader->setVec3("u_xAxisColor", grid.xAxisColor);
        m_gridShader->setVec3("u_zAxisColor", grid.zAxisColor);

        // Send grid level data.
        m_gridShader->setInt("u_numLevels", int(grid.levels.size()));
        for (std::size_t i = 0; i < grid.levels.size() && i < 5; ++i) { // Shader supports max 5 levels.
            const std::string b = "u_levels[" + std::to_string(i) + "].";
            m_gridShader->setFloat(b + "spacing", grid.levels[i].spacing);
            m_gridShader->setVec3(b + "color", grid.levels[i].color);
            m_gridShader->setFloat(b + "fadeInCameraDistanceStart", grid.levels[i].fadeInCameraDistanceStart);
            m_gridShader->setFloat(b + "fadeInCameraDistanceEnd", grid.levels[i].fadeInCameraDistanceEnd);
            m_gridShader->setBool("u_levelVisible[" + std::to_string(i) + "]", grid.levelVisible[i]);
        }

        // Use scene-wide fog properties.
        const auto& props = registry.ctx().get<SceneProperties>();
        m_gridShader->setBool("u_useFog", props.fogEnabled);
        m_gridShader->setVec3("u_fogColor", props.fogColor);
        m_gridShader->setFloat("u_fogStartDistance", props.fogStartDistance);
        m_gridShader->setFloat("u_fogEndDistance", props.fogEndDistance);

        // Z-fighting mitigation: First pass writes to depth buffer only.
        m_gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glPolygonOffset(1.0f, 1.0f);
        drawQuad();

        // Second pass writes color, but not depth, using the depth from the first pass.
        m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_gl->glDepthMask(GL_FALSE);
        m_gl->glPolygonOffset(-1.0f, -1.0f); // Bias back to draw lines on top.
        drawQuad();
    }
    m_gl->glDisable(GL_POLYGON_OFFSET_FILL);
    m_gl->glDepthMask(GL_TRUE); // Reset depth mask.
}

void RenderingSystem::renderSplines(entt::registry& registry, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye, int viewportWidth, int viewportHeight)
{
    qDebug() << "[DEBUG] SplinePass: Starting...";

    if (!m_glowShader || !m_capShader) return;

    // Save the current OpenGL state to ensure this pass is isolated.
    GLStateSnapshot stateBeforeSplines;
    saveGLState(m_gl, stateBeforeSplines);

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        restoreGLState(m_gl, stateBeforeSplines); // Always restore state on early exit
        return;
    }
    auto& primitives = m_contextPrimitives[ctx];

    // --- On-Demand Creation for Primitives (remains the same) ---
    if (primitives.lineVAO == 0)
    {
        qDebug() << "Creating spline LINE primitives for context" << ctx;
        m_gl->glGenVertexArrays(1, &primitives.lineVAO);
        m_gl->glGenBuffers(1, &primitives.lineVBO);
        m_gl->glBindVertexArray(primitives.lineVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, primitives.lineVBO);
        m_gl->glEnableVertexAttribArray(0);
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    }
    if (primitives.capVAO == 0)
    {
        qDebug() << "Creating spline CAP primitives for context" << ctx;
        m_gl->glGenVertexArrays(1, &primitives.capVAO);
        m_gl->glGenBuffers(1, &primitives.capVBO);
        m_gl->glBindVertexArray(primitives.capVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, primitives.capVBO);
        m_gl->glEnableVertexAttribArray(0);
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    }

    // --- Set Specific State for Spline Rendering ---
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDepthMask(GL_FALSE);
    m_gl->glDisable(GL_CULL_FACE);

    // --- Iterate and Draw Splines ---
    auto splineView = registry.view<SplineComponent>();
    for (auto e : splineView)
    {
        auto& sp = splineView.get<const SplineComponent>(e);

        if (sp.cachedVertices.size() < 2) {
            continue;
        }

        // --- Caching Logic ---
        // Only perform the expensive CPU calculation if the spline has changed.
        if (sp.isDirty) {
            qDebug() << "[Spline Cache] Recalculating vertices for dirty spline entity:" << (int)e;

            // Perform the expensive, one-time calculation and store it.
            switch (sp.type) {
            case SplineType::Linear:     sp.cachedVertices = evaluateLinearCPU(sp.controlPoints); break;
            case SplineType::CatmullRom: sp.cachedVertices = evaluateCatmullRomCPU(sp.controlPoints, 64); break;
            case SplineType::Bezier:     sp.cachedVertices = evaluateBezierCPU(sp.controlPoints, 64); break;
            case SplineType::Parametric: sp.cachedVertices = evaluateParametricCPU(sp.parametric.func, 128); break;
            }
            // Mark the spline as clean until its control points are modified again.
            sp.isDirty = false;
        }

        // On every frame, we now use the fast, cached data.
        if (sp.cachedVertices.size() < 2) {
            continue;
        }

        // --- Draw Glow (using cached vertices) ---
        m_glowShader->use();
        m_glowShader->setMat4("u_view", view);
        m_glowShader->setMat4("u_proj", proj);
        m_glowShader->setFloat("u_thickness", sp.thickness);
        m_glowShader->setVec2("u_viewport_size", glm::vec2(viewportWidth, viewportHeight));
        m_glowShader->setVec4("u_glowColour", sp.glowColour);
        m_glowShader->setVec4("u_coreColour", sp.coreColour);

        // Convert cached line strip to line segments for GL_LINES topology.
        std::vector<glm::vec3> lineSegments;
        lineSegments.reserve((sp.cachedVertices.size() - 1) * 2);
        for (size_t i = 0; i < sp.cachedVertices.size() - 1; ++i) {
            lineSegments.push_back(sp.cachedVertices[i]);
            lineSegments.push_back(sp.cachedVertices[i + 1]);
        }

        m_gl->glBindVertexArray(primitives.lineVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, primitives.lineVBO);
        m_gl->glBufferData(GL_ARRAY_BUFFER, lineSegments.size() * sizeof(glm::vec3), lineSegments.data(), GL_DYNAMIC_DRAW);
        m_gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineSegments.size()));

        // --- Draw Caps (using cached vertices) ---
        m_capShader->use();
        m_capShader->setMat4("u_view", view);
        m_capShader->setMat4("u_proj", proj);
        m_capShader->setVec2("u_viewport_size", glm::vec2(viewportWidth, viewportHeight));
        m_capShader->setFloat("u_thickness", sp.thickness);
        m_capShader->setVec4("u_glowColour", sp.glowColour);
        m_capShader->setVec4("u_coreColour", sp.coreColour);

        // Decide which points need a cap.
        std::vector<glm::vec3> capPoints;
        if (sp.type == SplineType::Linear) {
            // For linear splines, cap every control point to round the corners.
            capPoints = sp.controlPoints;
        }
        else {
            // For smooth splines, only cap the absolute start and end points.
            capPoints = { sp.cachedVertices.front(), sp.cachedVertices.back() };
        }

        if (!capPoints.empty()) {
            m_gl->glBindVertexArray(primitives.capVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, primitives.capVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, capPoints.size() * sizeof(glm::vec3), capPoints.data(), GL_DYNAMIC_DRAW);
            m_gl->glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(capPoints.size()));
        }
    }

    // --- Restore State ---
    qDebug() << "[DEBUG] SplinePass: Finished. Resetting state...";
    restoreGLState(m_gl, stateBeforeSplines);
    m_gl->glBindVertexArray(0);
    qDebug() << "[DEBUG] SplinePass: State perfectly restored.";
}


void RenderingSystem::updateAnimations(entt::registry& registry, float frameDt)
{
    // A persistent timer to drive all animations
    static float timer = 0.f;
    timer += frameDt;

    // --- Pulsing Spline Logic ---
    auto splineView = registry.view<PulsingSplineTag, SplineComponent>();

    //! CORRECTED: Use size_hint() to check if the view has any entities.
    if (splineView.size_hint() > 0) {
        float pulseSpeed = 3.0f;
        float brightness = (sin(timer * pulseSpeed) + 1.0f) / 2.0f; // Varies between 0.0 and 1.0
        float minBrightness = 0.1f;
        float finalBrightness = minBrightness + (1.0f - minBrightness) * brightness;

        for (auto entity : splineView) {
            auto& spline = splineView.get<SplineComponent>(entity);
            spline.glowColour.a = 1.0f * finalBrightness;
        }
    }

    // --- Pulsing Light Logic ---
    auto lightView = registry.view<PulsingLightComponent, MaterialComponent>();
    for (auto entity : lightView) {
        auto& pulse = lightView.get<PulsingLightComponent>(entity);
        auto& material = lightView.get<MaterialComponent>(entity);

        // A sine wave gives a smooth pulse between 0.0 and 1.0
        float blendFactor = (sin(timer * pulse.speed) + 1.0f) / 2.0f;

        // Interpolate the material's color between the on and off colors
        material.albedo = glm::mix(pulse.offColor, pulse.onColor, blendFactor);
    }
}

void RenderingSystem::resetGLState()
{
    if (!m_gl) return;
    m_gl->glDisable(GL_FRAMEBUFFER_SRGB);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_TRUE);

    m_gl->glDisable(GL_BLEND);
    m_gl->glDisable(GL_STENCIL_TEST);
    m_gl->glUseProgram(0);
}

void RenderingSystem::renderFieldVisualizers(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, float deltaTime)
{
    if (!m_gl) {
        qWarning() << "[FieldViz] Render call skipped: m_gl is null.";
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning() << "[FieldViz] Render call skipped: No active OpenGL context.";
        return;
    }

    // --- 1. GATHER ALL EFFECTOR DATA (CPU Side) ---
    std::vector<PointEffectorGpu> pointEffectors;
    std::vector<TriangleGpu> triangleEffectors;
    std::vector<DirectionalEffectorGpu> directionalEffectors;
    // ... (Effector gathering logic is correct and remains the same) ...
    auto pointView = registry.view<PointEffectorComponent, TransformComponent>();
    for (auto entity : pointView) {
        auto& comp = pointView.get<PointEffectorComponent>(entity);
        auto& xf = pointView.get<TransformComponent>(entity);
        PointEffectorGpu effector;
        effector.position = glm::vec4(xf.translation, 1.0f);
        effector.normal = glm::vec4(0.0f);
        effector.strength = comp.strength;
        effector.radius = comp.radius;
        effector.falloffType = static_cast<int>(comp.falloff);
        pointEffectors.push_back(effector);
    }
    auto splineView = registry.view<SplineEffectorComponent, SplineComponent>();
    for (auto entity : splineView) {
        auto& comp = splineView.get<SplineEffectorComponent>(entity);
        auto& spline = splineView.get<SplineComponent>(entity);
        if (spline.cachedVertices.empty()) continue;
        for (size_t i = 0; i < spline.cachedVertices.size() - 1; ++i) {
            glm::vec3 tangent = glm::normalize(spline.cachedVertices[i + 1] - spline.cachedVertices[i]);
            glm::vec3 normal = glm::normalize(glm::cross(tangent, glm::vec3(0, 1, 0)));
            if (comp.direction == SplineEffectorComponent::ForceDirection::Tangent) {
                normal = tangent;
            }
            PointEffectorGpu effector;
            effector.position = glm::vec4(spline.cachedVertices[i], 1.0f);
            effector.normal = glm::vec4(normal, 0.0f);
            effector.strength = comp.strength;
            effector.radius = comp.radius;
            effector.falloffType = 1;
            pointEffectors.push_back(effector);
        }
    }
    auto meshView = registry.view<MeshEffectorComponent, RenderableMeshComponent, TransformComponent>();
    for (auto entity : meshView) {
        auto& comp = meshView.get<MeshEffectorComponent>(entity);
        auto& mesh = meshView.get<RenderableMeshComponent>(entity);
        auto& xf = meshView.get<TransformComponent>(entity);
        glm::mat4 model = xf.getTransform();
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            TriangleGpu tri;
            tri.v0 = model * glm::vec4(mesh.vertices[mesh.indices[i]].position, 1.0f);
            tri.v1 = model * glm::vec4(mesh.vertices[mesh.indices[i + 1]].position, 1.0f);
            tri.v2 = model * glm::vec4(mesh.vertices[mesh.indices[i + 2]].position, 1.0f);
            tri.v0.w = comp.strength;
            tri.normal.w = comp.distance;
            triangleEffectors.push_back(tri);
        }
    }
    auto dirView = registry.view<DirectionalEffectorComponent>();
    for (auto entity : dirView) {
        auto& comp = dirView.get<DirectionalEffectorComponent>(entity);
        DirectionalEffectorGpu effector;
        effector.direction = glm::vec4(glm::normalize(comp.direction), 0.0f);
        effector.strength = comp.strength;
        directionalEffectors.push_back(effector);
    }

    // --- 2. UPLOAD ALL EFFECTOR DATA TO GPU ---
    if (m_effectorDataUBO == 0) m_gl->glGenBuffers(1, &m_effectorDataUBO);
    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, m_effectorDataUBO);
    m_gl->glBufferData(GL_UNIFORM_BUFFER, 256 * sizeof(PointEffectorGpu) + 16 * sizeof(DirectionalEffectorGpu), nullptr, GL_DYNAMIC_DRAW);
    if (!pointEffectors.empty()) {
        m_gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, std::min(size_t(256), pointEffectors.size()) * sizeof(PointEffectorGpu), pointEffectors.data());
    }
    if (!directionalEffectors.empty()) {
        size_t directionalOffset = 256 * sizeof(PointEffectorGpu);
        m_gl->glBufferSubData(GL_UNIFORM_BUFFER, directionalOffset, std::min(size_t(16), directionalEffectors.size()) * sizeof(DirectionalEffectorGpu), directionalEffectors.data());
    }
    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
    if (m_triangleDataSSBO == 0) m_gl->glGenBuffers(1, &m_triangleDataSSBO);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleDataSSBO);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, triangleEffectors.size() * sizeof(TriangleGpu), triangleEffectors.empty() ? nullptr : triangleEffectors.data(), GL_DYNAMIC_DRAW);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    auto& arrowPrimitives = m_contextPrimitives[ctx];
    if (arrowPrimitives.arrowVAO == 0) {
        std::vector<Vertex> arrowVertices;
        std::vector<unsigned int> arrowIndices;
        createArrowPrimitive(arrowVertices, arrowIndices);
        arrowPrimitives.arrowIndexCount = arrowIndices.size();
        m_gl->glGenVertexArrays(1, &arrowPrimitives.arrowVAO);
        m_gl->glGenBuffers(1, &arrowPrimitives.arrowVBO);
        m_gl->glGenBuffers(1, &arrowPrimitives.arrowEBO);
        m_gl->glBindVertexArray(arrowPrimitives.arrowVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, arrowPrimitives.arrowVBO);
        m_gl->glBufferData(GL_ARRAY_BUFFER, arrowVertices.size() * sizeof(Vertex), arrowVertices.data(), GL_STATIC_DRAW);
        m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, arrowPrimitives.arrowEBO);
        m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, arrowIndices.size() * sizeof(unsigned int), arrowIndices.data(), GL_STATIC_DRAW);
        m_gl->glEnableVertexAttribArray(0);
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        m_gl->glEnableVertexAttribArray(1);
        m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        m_gl->glBindVertexArray(0);
    }


    GLuint zero = 0;
    m_gl->glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_debugAtomicCounter);
    m_gl->glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // Bind the debug buffers to the binding points we used in the shaders
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_debugBuffer);
    m_gl->glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 11, m_debugAtomicCounter);


    // --- 3. MAIN LOOP FOR EACH VISUALIZER ---
    auto visualizerView = registry.view<FieldVisualizerComponent, TransformComponent>();
    for (auto entity : visualizerView)
    {
        auto& vis = visualizerView.get<FieldVisualizerComponent>(entity);
        if (!vis.isEnabled) continue;
        const auto& xf = visualizerView.get<const TransformComponent>(entity);

        if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Particles)
        {
            if (!m_particleUpdateComputeShader || !m_particleRenderShader) continue;

            auto& settings = vis.particleSettings;

            if (vis.particleBuffer[0] == 0 || vis.isGpuDataDirty) {
                if (vis.particleBuffer[0] != 0) {
                    m_gl->glDeleteBuffers(2, vis.particleBuffer);
                    m_gl->glDeleteVertexArrays(1, &vis.particleVAO);
                }
                std::vector<Particle> particles(settings.particleCount);
                std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distrib(0.0f, 1.0f);
                glm::vec3 boundsSize = vis.bounds.max - vis.bounds.min;
                for (int i = 0; i < settings.particleCount; ++i) {
                    particles[i].position = glm::vec4(vis.bounds.min + distrib(rng) * boundsSize, 1.0f);
                    particles[i].velocity = glm::vec4(0.0f);
                    particles[i].color = glm::vec4(1.0f); // Will be set by shader
                    particles[i].age = distrib(rng) * settings.lifetime;
                    particles[i].lifetime = settings.lifetime;
                    particles[i].size = settings.baseSize;
                }
                m_gl->glGenBuffers(2, vis.particleBuffer);
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);
                m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);
                m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);
                m_gl->glGenVertexArrays(1, &vis.particleVAO);
            }

            m_particleUpdateComputeShader->use();
            m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_effectorDataUBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_triangleDataSSBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]);
            m_particleUpdateComputeShader->setMat4("u_visualizerModelMatrix", xf.getTransform());
            m_particleUpdateComputeShader->setFloat("u_deltaTime", deltaTime);
            m_particleUpdateComputeShader->setFloat("u_time", m_elapsedTime);
            m_particleUpdateComputeShader->setVec3("u_boundsMin", vis.bounds.min);
            m_particleUpdateComputeShader->setVec3("u_boundsMax", vis.bounds.max);
            m_particleUpdateComputeShader->setInt("u_pointEffectorCount", static_cast<int>(pointEffectors.size()));
            m_particleUpdateComputeShader->setInt("u_directionalEffectorCount", static_cast<int>(directionalEffectors.size()));
            m_particleUpdateComputeShader->setInt("u_triangleEffectorCount", static_cast<int>(triangleEffectors.size()));

            m_gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
            m_gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            m_gl->glEnable(GL_PROGRAM_POINT_SIZE);
            m_gl->glEnable(GL_BLEND);
            m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            m_gl->glDepthMask(GL_FALSE);
            m_particleRenderShader->use();
            m_particleRenderShader->setMat4("u_view", view);
            m_particleRenderShader->setMat4("u_projection", projection);
            m_gl->glBindVertexArray(vis.particleVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, vis.particleBuffer[1 - vis.currentReadBuffer]);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
            m_gl->glEnableVertexAttribArray(1);
            m_gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, color));
            m_gl->glEnableVertexAttribArray(2);
            m_gl->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, size));
            m_gl->glDrawArrays(GL_POINTS, 0, settings.particleCount);
            m_gl->glBindVertexArray(0);
            m_gl->glDepthMask(GL_TRUE);
            m_gl->glDisable(GL_BLEND);
            m_gl->glDisable(GL_PROGRAM_POINT_SIZE);
            vis.currentReadBuffer = 1 - vis.currentReadBuffer;
        }
        else if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Flow)
        {
            if (!m_flowVectorComputeShader || !m_instancedArrowShader) continue;

            auto& settings = vis.flowSettings;

            if (vis.particleBuffer[0] == 0 || vis.isGpuDataDirty) {
                if (vis.particleBuffer[0] != 0) {
                    m_gl->glDeleteBuffers(2, vis.particleBuffer);
                    if (vis.gpuData.instanceDataSSBO) m_gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
                }
                std::vector<Particle> particles(settings.particleCount);
                std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distrib(0.0f, 1.0f);
                glm::vec3 boundsCenter = (vis.bounds.min + vis.bounds.max) / 2.0f;
                glm::vec3 boundsHalfSize = (vis.bounds.max - vis.bounds.min) / 2.0f;

                for (int i = 0; i < settings.particleCount; ++i) {
                    // TODO: Implement different spawn distributions
                    particles[i].position = glm::vec4(vis.bounds.min + distrib(rng) * boundsHalfSize * 2.0f, 1.0f);
                    particles[i].velocity = glm::vec4(0.0f);
                    particles[i].color = glm::vec4(1.0f); // Default color
                    particles[i].age = distrib(rng) * settings.lifetime;
                    particles[i].lifetime = settings.lifetime;
                    particles[i].size = settings.baseSize;
                }
                m_gl->glGenBuffers(2, vis.particleBuffer);
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);
                m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);
                m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);

                if (vis.gpuData.instanceDataSSBO == 0) m_gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
                m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
                m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, settings.particleCount * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);
            }

            m_flowVectorComputeShader->use();
            m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_effectorDataUBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_triangleDataSSBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, vis.gpuData.instanceDataSSBO);

            m_flowVectorComputeShader->setMat4("u_visualizerModelMatrix", xf.getTransform());
            m_flowVectorComputeShader->setFloat("u_deltaTime", deltaTime);
            m_flowVectorComputeShader->setFloat("u_time", m_elapsedTime);
            m_flowVectorComputeShader->setVec3("u_boundsMin", vis.bounds.min);
            m_flowVectorComputeShader->setVec3("u_boundsMax", vis.bounds.max);
            m_flowVectorComputeShader->setFloat("u_baseSpeed", settings.baseSpeed);
            m_flowVectorComputeShader->setFloat("u_velocityMultiplier", settings.speedIntensityMultiplier);
            m_flowVectorComputeShader->setFloat("u_flowScale", settings.baseSize);
            m_flowVectorComputeShader->setFloat("u_fadeInPercent", settings.growthPercentage);
            m_flowVectorComputeShader->setFloat("u_fadeOutPercent", settings.shrinkPercentage);
            // TODO: Pass gradient data to shader
            m_flowVectorComputeShader->setInt("u_pointEffectorCount", static_cast<int>(pointEffectors.size()));
            m_flowVectorComputeShader->setInt("u_directionalEffectorCount", static_cast<int>(directionalEffectors.size()));
            m_flowVectorComputeShader->setInt("u_triangleEffectorCount", static_cast<int>(triangleEffectors.size()));
            m_flowVectorComputeShader->setFloat("u_seedOffset", static_cast<float>(rand()) / RAND_MAX);

            m_gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
            m_gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            m_instancedArrowShader->use();
            m_instancedArrowShader->setMat4("view", view);
            m_instancedArrowShader->setMat4("projection", projection);

            m_gl->glBindVertexArray(arrowPrimitives.arrowVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);

            GLsizei vec4Size = sizeof(glm::vec4);
            m_gl->glEnableVertexAttribArray(2); m_gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, modelMatrix));
            m_gl->glEnableVertexAttribArray(3); m_gl->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + vec4Size));
            m_gl->glEnableVertexAttribArray(4); m_gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 2 * vec4Size));
            m_gl->glEnableVertexAttribArray(5); m_gl->glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 3 * vec4Size));
            m_gl->glEnableVertexAttribArray(6); m_gl->glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, color)));
            m_gl->glVertexAttribDivisor(2, 1); m_gl->glVertexAttribDivisor(3, 1); m_gl->glVertexAttribDivisor(4, 1); m_gl->glVertexAttribDivisor(5, 1); m_gl->glVertexAttribDivisor(6, 1);

            m_gl->glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(arrowPrimitives.arrowIndexCount), GL_UNSIGNED_INT, 0, settings.particleCount);

            m_gl->glBindVertexArray(0);
            vis.currentReadBuffer = 1 - vis.currentReadBuffer;
        }
        if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Arrows)
        {
            if (!m_arrowFieldComputeShader || !m_instancedArrowShader) {
                qWarning() << "[FieldViz] Arrow render skipped: Shaders not loaded.";
                continue;
            }

            // FIX: Access settings from the correct nested struct
            auto& settings = vis.arrowSettings;

            if (vis.isGpuDataDirty) {
                qDebug() << "[FieldViz] isGpuDataDirty is true. Recreating arrow buffers with density:" << settings.density.x << "x" << settings.density.y << "x" << settings.density.z;

                if (vis.gpuData.samplePointsSSBO) m_gl->glDeleteBuffers(1, &vis.gpuData.samplePointsSSBO);
                if (vis.gpuData.instanceDataSSBO) m_gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
                if (vis.gpuData.commandUBO) m_gl->glDeleteBuffers(1, &vis.gpuData.commandUBO);

                std::vector<glm::vec4> samplePoints;
                vis.gpuData.numSamplePoints = settings.density.x * settings.density.y * settings.density.z;

                qDebug() << "[FieldViz] Calculated numSamplePoints:" << vis.gpuData.numSamplePoints;
                /*
                if (glm::length2(vis.bounds.max - vis.bounds.min) < 1e-6f) {
                    vis.bounds.min = glm::vec3(-5.0f);
                    vis.bounds.max = glm::vec3(5.0f);
                }
                */

                if (vis.gpuData.numSamplePoints == 0) {
                    qWarning() << "[FieldViz] Arrow density is zero, skipping buffer creation.";
                }
                else {
                    samplePoints.reserve(vis.gpuData.numSamplePoints);
                    glm::vec3 boundsSize = vis.bounds.max - vis.bounds.min;
                    for (int x = 0; x < settings.density.x; ++x) {
                        for (int y = 0; y < settings.density.y; ++y) {
                            for (int z = 0; z < settings.density.z; ++z) {
                                glm::vec3 t = {
                                    (settings.density.x > 1) ? static_cast<float>(x) / (settings.density.x - 1) : 0.5f,
                                    (settings.density.y > 1) ? static_cast<float>(y) / (settings.density.y - 1) : 0.5f,
                                    (settings.density.z > 1) ? static_cast<float>(z) / (settings.density.z - 1) : 0.5f,
                                };
                                samplePoints.emplace_back(glm::vec4(vis.bounds.min + t * boundsSize, 1.0f));
                            }
                        }
                    }
                    m_gl->glGenBuffers(1, &vis.gpuData.samplePointsSSBO);
                    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.samplePointsSSBO);
                    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, samplePoints.size() * sizeof(glm::vec4), samplePoints.data(), GL_STATIC_DRAW);

                    m_gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
                    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
                    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, vis.gpuData.numSamplePoints * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

                    struct DrawElementsIndirectCommand { GLuint count; GLuint instanceCount; GLuint firstIndex; GLuint baseVertex; GLuint baseInstance; };
                    DrawElementsIndirectCommand cmd = { (GLuint)arrowPrimitives.arrowIndexCount, 0, 0, 0, 0 };
                    m_gl->glGenBuffers(1, &vis.gpuData.commandUBO);
                    m_gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
                    m_gl->glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), &cmd, GL_DYNAMIC_DRAW);
                }
            }

            if (vis.gpuData.numSamplePoints == 0) continue;

            qDebug() << "[FieldViz] Dispatching compute shader for arrows. Scale:" << settings.vectorScale
                << "Head Scale:" << settings.headScale << "Cull Thresh:" << settings.cullingThreshold;

            m_gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
            GLuint zero = 0;
            // The instanceCount is the second integer in the struct, so its offset is sizeof(GLuint).
            m_gl->glBufferSubData(GL_DRAW_INDIRECT_BUFFER, sizeof(GLuint), sizeof(GLuint), &zero);

            m_arrowFieldComputeShader->use();
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vis.gpuData.samplePointsSSBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vis.gpuData.instanceDataSSBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, vis.gpuData.commandUBO);
            m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_effectorDataUBO);
            m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_triangleDataSSBO);
            m_arrowFieldComputeShader->setMat4("u_visualizerModelMatrix", xf.getTransform());
            m_arrowFieldComputeShader->setFloat("u_vectorScale", settings.vectorScale);
            m_arrowFieldComputeShader->setFloat("u_arrowHeadScale", settings.headScale);
            m_arrowFieldComputeShader->setFloat("u_cullingThreshold", settings.cullingThreshold);
            m_arrowFieldComputeShader->setInt("u_pointEffectorCount", static_cast<int>(pointEffectors.size()));
            m_arrowFieldComputeShader->setInt("u_directionalEffectorCount", static_cast<int>(directionalEffectors.size()));
            m_arrowFieldComputeShader->setInt("u_triangleEffectorCount", static_cast<int>(triangleEffectors.size()));

            m_gl->glDispatchCompute((GLuint)vis.gpuData.numSamplePoints / 256 + 1, 1, 1);
            m_gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

            struct DrawElementsIndirectCommand {
                GLuint count;
                GLuint instanceCount;
                GLuint firstIndex;
                GLuint baseVertex;
                GLuint baseInstance;
            };
            DrawElementsIndirectCommand cmd;
            m_gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
            m_gl->glGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &cmd);
            m_gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

            qDebug() << "[INDIRECT_DRAW_DEBUG] Instance Count:" << cmd.instanceCount
                << " | Index Count:" << cmd.count;
            // --- END DEBUG ---


            /* ---------- DEBUG: dump first few InstanceData records ---------- */
            {
                constexpr GLuint kDumpCount = 4;                    // how many structs to print
                InstanceData dump[kDumpCount]{};                    // CPU-side mirror

                // 1) read back the SSBO that the compute shader just filled
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
            m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    sizeof(dump),    // bytes to copy
                    dump);           // destination

                // 2) spit them out in a human-readable way
                qDebug().nospace() << "[INSTANCE_DUMP] sizeof(InstanceData) = "
                    << sizeof(InstanceData) << "  (expect 96)";
                for (GLuint i = 0; i < kDumpCount; ++i) {
                    const auto& d = dump[i];
                    qDebug().nospace()
                        << "  [" << i << "] pos=("
                        << d.modelMatrix[3].x << ", "
                        << d.modelMatrix[3].y << ", "
                        << d.modelMatrix[3].z << ")  "
                        << "scaleZ=" << d.modelMatrix[2][2] << "  "
                        << "colour=(" << d.color.r << ", "
                        << d.color.g << ", "
                        << d.color.b << ")";
                }
            }
            /* ---------------------------------------------------------------- */


            m_instancedArrowShader->use();
            m_instancedArrowShader->setMat4("view", view);
            m_instancedArrowShader->setMat4("projection", projection);

            m_gl->glBindVertexArray(arrowPrimitives.arrowVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);
            GLsizei vec4Size = sizeof(glm::vec4);
            m_gl->glEnableVertexAttribArray(2);
            m_gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, modelMatrix));
            m_gl->glEnableVertexAttribArray(3);
            m_gl->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + vec4Size));
            m_gl->glEnableVertexAttribArray(4);
            m_gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 2 * vec4Size));
            m_gl->glEnableVertexAttribArray(5);
            m_gl->glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 3 * vec4Size));
            m_gl->glEnableVertexAttribArray(6);
            m_gl->glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, color)));
            m_gl->glVertexAttribDivisor(2, 1);
            m_gl->glVertexAttribDivisor(3, 1);
            m_gl->glVertexAttribDivisor(4, 1);
            m_gl->glVertexAttribDivisor(5, 1);
            m_gl->glVertexAttribDivisor(6, 1);

            //m_gl->glFrontFace(GL_CW);

            m_gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
            m_gl->glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0);

           // m_gl->glFrontFace(GL_CCW);

            m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

            struct InstanceData {
                glm::mat4 modelMatrix;
                glm::vec4 color;
                glm::vec4 _padding;
            };



            InstanceData firstInstance;
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
            m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(InstanceData), &firstInstance);

            qDebug() << "========================================================";
            qDebug() << "[CANARY_TEST] Color of first instance: " << glm::to_string(firstInstance.color).c_str();
            qDebug() << "========================================================";

            m_gl->glBindVertexArray(0);
        }
        vis.isGpuDataDirty = false; // Reset dirty flag after processing
    }
}

void RenderingSystem::updateSceneLogic(entt::registry& registry, float deltaTime)
{
    // --- Update Spline Caches ---
    // This ensures that any "dirty" splines have their vertex data
    // recalculated before any render pass needs to use it.
    auto splineView = registry.view<SplineComponent>();
    for (auto e : splineView)
    {
        auto& sp = splineView.get<SplineComponent>(e);
        if (sp.isDirty) {
            qDebug() << "[Scene Logic] Recalculating vertices for dirty spline entity:" << (int)e;
            switch (sp.type) {
            case SplineType::Linear:     sp.cachedVertices = evaluateLinearCPU(sp.controlPoints); break;
            case SplineType::CatmullRom: sp.cachedVertices = evaluateCatmullRomCPU(sp.controlPoints, 64); break;
            case SplineType::Bezier:     sp.cachedVertices = evaluateBezierCPU(sp.controlPoints, 64); break;
            case SplineType::Parametric: sp.cachedVertices = evaluateParametricCPU(sp.parametric.func, 128); break;
            }
            sp.isDirty = false;
        }
    }
}

void RenderingSystem::drawIntersections(const std::vector<std::vector<glm::vec3>>& allOutlines, const glm::mat4& view, const glm::mat4& proj)
{
    if (!m_outlineShader || m_intersectionVAO == 0 || allOutlines.empty()) return;

    m_gl->glDisable(GL_DEPTH_TEST); // Draw on top of everything.
    m_outlineShader->use();
    m_outlineShader->setMat4("u_view", view);
    m_outlineShader->setMat4("u_projection", proj);
    m_outlineShader->setVec3("u_outlineColor", glm::vec3(1.0f, 0.5f, 0.0f)); // Orange color for outlines.

    m_gl->glBindVertexArray(m_intersectionVAO);
    for (const auto& outlinePoints : allOutlines) {
        if (outlinePoints.size() > 1) {
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_intersectionVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, outlinePoints.size() * sizeof(glm::vec3), outlinePoints.data(), GL_DYNAMIC_DRAW);
            m_gl->glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(outlinePoints.size()));
        }
    }
    m_gl->glBindVertexArray(0);
    m_gl->glEnable(GL_DEPTH_TEST); // Re-enable depth testing for subsequent passes.
}

void RenderingSystem::renderSelectionGlow(QOpenGLWidget* viewport, entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, TargetFBOs& target) {
    
    auto viewSelected = registry.view<SelectedComponent, RenderableMeshComponent, TransformComponent>();

    qDebug() << "[GLOW PASS] Starting for ViewportWidget:" << viewport;

    //! Bind the dedicated glow FBO for this viewport.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.glowFBO);
    m_gl->glViewport(0, 0, target.w, target.h);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    if (viewSelected.size_hint() == 0) {
        return; // Early exit if nothing is selected
    }

    // --- PASS 1: Render solid emissive color to the glow FBO ---
    m_emissiveSolidShader->use();
    m_emissiveSolidShader->setMat4("view", view);
    m_emissiveSolidShader->setMat4("projection", projection);
    m_emissiveSolidShader->setVec3("emissiveColor", glm::vec3(1.0f, 0.75f, 0.1f));

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    for (auto entity : viewSelected) {
        auto [mesh, transform] = viewSelected.get<RenderableMeshComponent, TransformComponent>(entity);
        m_emissiveSolidShader->setMat4("model", transform.getTransform());

        auto& res = registry.get_or_emplace<RenderResourceComponent>(entity);
        auto& buf = res.perContext[ctx];
        if (buf.VAO != 0) {
            m_gl->glBindVertexArray(buf.VAO);
            m_gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
    }

    // --- PASS 2: Apply Gaussian blur ---
    bool horizontal = true, first_iteration = true;
    unsigned int amount = 9;
    m_blurShader->use();
    m_blurShader->setInt("screenTexture", 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glDisable(GL_DEPTH_TEST);

    auto& primitives = m_contextPrimitives[QOpenGLContext::currentContext()];
    if (primitives.compositeVAO == 0) {
        m_gl->glGenVertexArrays(1, &primitives.compositeVAO);
    }

    for (unsigned int i = 0; i < amount; i++) {
        //! Bind the correct ping-pong FBO for this target.
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.pingpongFBO[horizontal]);
        m_gl->glViewport(0, 0, target.w, target.h);

        //! DEBUG: Log the clear call to be 100% sure it's happening.
        qDebug() << "[GLOW PASS] Clearing pingpongFBO[" << horizontal << "] (" << target.pingpongFBO[horizontal] << ")";


        m_gl->glClear(GL_COLOR_BUFFER_BIT); // Clear is essential to prevent accumulation.

        m_blurShader->setBool("horizontal", horizontal);

        //! Bind the correct texture: either the initial glow or the other ping-pong texture.
        GLuint textureToBlur = first_iteration ? target.glowTexture : target.pingpongTexture[!horizontal];
        m_gl->glBindTexture(GL_TEXTURE_2D, textureToBlur);

        m_gl->glBindVertexArray(primitives.compositeVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);

        horizontal = !horizontal;
        if (first_iteration) first_iteration = false;
    }

    m_gl->glEnable(GL_DEPTH_TEST);
}


//---------- UTILITY & HELPER IMPLEMENTATIONS ------------------

bool RenderingSystem::isDescendantOf(entt::registry& r, entt::entity e, entt::entity ancestor) {
    // Traverse up the entity hierarchy to check for an ancestor-descendant relationship.
    while (r.any_of<ParentComponent>(e)) {
        e = r.get<ParentComponent>(e).parent;
        if (e == ancestor) return true;
    }
    return false;
}

void RenderingSystem::updateCameraTransforms(entt::registry& r)
{
    auto view = r.view<CameraComponent, TransformComponent>();
    for (auto e : view) {
        auto& cam = view.get<CameraComponent>(e).camera;
        auto& xf = view.get<TransformComponent>(e);
        xf.translation = cam.getPosition();
        glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - cam.getPosition());
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::cross(right, fwd);
        xf.rotation = glm::quat_cast(glm::mat3(right, up, -fwd));
    }
}

void RenderingSystem::initShaders() {
    try {
        // --- Use the (const char*, const char*) constructor for simple shaders ---
        // This is the most direct and clear way for simple vertex/fragment pairs.
        m_phongShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/vertex_shader.glsl",
            "D:/RoboticsSoftware/shaders/fragment_shader.glsl"
        );
        m_gridShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/grid_vert.glsl",
            "D:/RoboticsSoftware/shaders/grid_frag.glsl"
        );
        m_instancedArrowShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/instanced_arrow_vert.glsl",
            "D:/RoboticsSoftware/shaders/instanced_arrow_frag.glsl"
        );
        m_outlineShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/outline_vert.glsl",
            "D:/RoboticsSoftware/shaders/outline_frag.glsl"
        );
        m_emissiveSolidShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/vertex_shader.glsl",
            "D:/RoboticsSoftware/shaders/emissive_solid_frag.glsl"
        );
        m_blurShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/post_process_vert.glsl",
            "D:/RoboticsSoftware/shaders/gaussian_blur_frag.glsl"
        );
        m_compositeShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/post_process_vert.glsl",
            "D:/RoboticsSoftware/shaders/composite_frag.glsl"
        );

        // --- Use the static factory methods for complex shaders ---
        // This is the correct pattern for shaders with more than two stages or special types.
        m_glowShader = Shader::buildGeometryShader(m_gl,
            "D:/RoboticsSoftware/shaders/glow_line_vert.glsl",
            "D:/RoboticsSoftware/shaders/glow_line_geom.glsl",
            "D:/RoboticsSoftware/shaders/glow_line_frag.glsl"
        );
        m_capShader = Shader::buildGeometryShader(m_gl,
            "D:/RoboticsSoftware/shaders/cap_vert.glsl",
            "D:/RoboticsSoftware/shaders/cap_geom.glsl",
            "D:/RoboticsSoftware/shaders/cap_frag.glsl"
        );

        // This was the line causing the error. It should use a factory method,
        // not a constructor call via make_unique.
        m_arrowFieldComputeShader = Shader::buildComputeShader(m_gl,
            "D:/RoboticsSoftware/shaders/field_visualizer_comp.glsl"
        );

        m_particleUpdateComputeShader = Shader::buildComputeShader(m_gl,
            "D:/RoboticsSoftware/shaders/particle_update_comp.glsl"
        );

        m_particleRenderShader = std::make_unique<Shader>(m_gl,
            "D:/RoboticsSoftware/shaders/particle_render_vert.glsl",
            "D:/RoboticsSoftware/shaders/particle_render_frag.glsl"
        );

        m_flowVectorComputeShader = Shader::buildComputeShader(m_gl,
            "D:/RoboticsSoftware/shaders/flow_vector_update_comp.glsl"
        );
    }
    catch (const std::runtime_error& e) {
        qFatal("[RenderingSystem] FATAL: Shader initialization failed: %s", e.what());
    }
    {
        GLint ok = 0, len = 0;
        m_gl->glGetProgramiv(m_compositeShader->ID, GL_LINK_STATUS, &ok);
        if (!ok) {
            m_gl->glGetProgramiv(m_compositeShader->ID, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0');
            m_gl->glGetProgramInfoLog(m_compositeShader->ID, len, nullptr, log.data());
            qCritical() << "[Composite-LINK-FAIL]\n" << log.c_str();
        }
        else {
            GLint vUVSeen = m_gl->glGetAttribLocation(m_compositeShader->ID, "vUV"); // returns −1 if not a real attribute/varying
            qDebug() << "[Composite] link OK ;  glGetAttribLocation(\"vUV\") =" << vUVSeen;
        }
    }
}

void RenderingSystem::dumpRenderTargets() const
{
    // This function now finds the viewport widget for the current OpenGL context
    // and dumps its specific render target textures for debugging.

    if (!m_gl || !m_viewportWidget) return;

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning() << "[dumpRenderTargets] No active OpenGL context.";
        return;
    }

    // Since a widget's context is made current before painting, we can use the
    // stored m_viewportWidget as a key, assuming it's the one painting.
    // Note: A more complex setup might need to map context back to a widget.
    if (m_targets.count(m_viewportWidget)) {
        const TargetFBOs& target = m_targets.at(m_viewportWidget);
        debugTexturePixel(m_gl, target.mainColorTexture, "MainColour");
        debugTexturePixel(m_gl, target.pingpongTexture[0], "BlurredGlow");
    }
    else {
        qWarning() << "[dumpRenderTargets] No render targets found for the current viewport.";
    }
}

void RenderingSystem::initFramebuffers(int width, int height) {
    //
    // [DEPRECATED] This function is no longer used.
    // Per-viewport framebuffers are now created and managed on-demand 
    // by the initOrResizeFBOsForTarget() helper function, which is
    // called from within renderView().
    //
}

void RenderingSystem::renderView(QOpenGLWidget* viewport, entt::registry& registry, entt::entity cameraEntity, int vpW, int vpH)
{
    ensureGlResolved();
    if (!m_gl) return;

    m_currentCamera = cameraEntity;
    const auto& camera = registry.get<CameraComponent>(cameraEntity).camera;

    // CORRECTED: Calculate deltaTime once per frame or get from a timer.
    // For now, we'll assume a steady 60 FPS.
    const float deltaTime = 1.0f / 60.0f;
    m_elapsedTime += deltaTime;


    //! Get or create the dedicated framebuffer set for the currently rendering viewport.
    TargetFBOs& target = m_targets[viewport];

    //! Check if FBOs need to be created or resized for this viewport.
    if (target.mainFBO == 0 || vpW > target.w || vpH > target.h) {
        initOrResizeFBOsForTarget(target, vpW, vpH);
    }

    // --- 1. Bind and Clear this Viewport's Framebuffer ---
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.mainFBO);
    m_gl->glViewport(0, 0, target.w, target.h); // Use the FBO's allocated size

    resetGLState();

    const auto& props = registry.ctx().get<SceneProperties>();
    m_gl->glClearColor(props.backgroundColor.r, props.backgroundColor.g, props.backgroundColor.b, props.backgroundColor.a);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // --- 2. Main Scene Pass ---
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_TRUE);

    float aspect = (vpH > 0) ? static_cast<float>(vpW) / vpH : 1.0f;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix(aspect);
    glm::vec3 camPos = camera.getPosition();

    // Perform all render passes into the dedicated FBO
    renderMeshes(registry, view, projection, camPos);
    renderGrid(registry, view, projection, camPos);
    renderSplines(registry, view, projection, camPos, target.w, target.h);

    // Pass deltaTime to the visualizer
    renderFieldVisualizers(registry, view, projection, deltaTime);

    //! The glow pass now needs to know which FBO set to use.
    renderSelectionGlow(viewport, registry, view, projection, target);

    // --- 3. Final Composite to Screen ---
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto& primitives = m_contextPrimitives[ctx];

    if (primitives.compositeVAO == 0) {
        m_gl->glGenVertexArrays(1, &primitives.compositeVAO);
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());
    m_gl->glViewport(0, 0, vpW, vpH); // Set viewport to the actual window size

    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_FALSE);
    m_gl->glDisable(GL_BLEND);
    m_gl->glEnable(GL_FRAMEBUFFER_SRGB);

    m_compositeShader->use();
    m_compositeShader->setInt("sceneTexture", 0);
    m_compositeShader->setInt("glowTexture", 1);

    m_gl->glActiveTexture(GL_TEXTURE0);
    //! Use the color texture from this viewport's dedicated FBO
    m_gl->glBindTexture(GL_TEXTURE_2D, target.mainColorTexture);

    m_gl->glActiveTexture(GL_TEXTURE1);
    //! Use the blurred glow texture from this viewport's dedicated FBO
    m_gl->glBindTexture(GL_TEXTURE_2D, target.pingpongTexture[0]);

    m_gl->glBindVertexArray(primitives.compositeVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- 4. Restore State for Next Viewport ---
    m_gl->glBindVertexArray(0);
    m_gl->glDisable(GL_FRAMEBUFFER_SRGB);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glActiveTexture(GL_TEXTURE0);
}

void RenderingSystem::initOrResizeFBOsForTarget(TargetFBOs& target, int width, int height) {
    ensureGlResolved();

    // First, delete old resources if they exist
    if (target.mainFBO != 0) {
        m_gl->glDeleteFramebuffers(1, &target.mainFBO);
        m_gl->glDeleteTextures(1, &target.mainColorTexture);
        m_gl->glDeleteTextures(1, &target.mainDepthTexture);
        m_gl->glDeleteFramebuffers(1, &target.glowFBO);
        m_gl->glDeleteTextures(1, &target.glowTexture);
        m_gl->glDeleteFramebuffers(2, target.pingpongFBO);
        m_gl->glDeleteTextures(2, target.pingpongTexture);
    }

    target.w = width;
    target.h = height;

    // Main Scene FBO
    m_gl->glGenFramebuffers(1, &target.mainFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.mainFBO);
    m_gl->glGenTextures(1, &target.mainColorTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, target.mainColorTexture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.mainColorTexture, 0);

    m_gl->glGenTextures(1, &target.mainDepthTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, target.mainDepthTexture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, target.mainDepthTexture, 0);

    if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Main FBO not complete!";

    // Glow FBO
    m_gl->glGenFramebuffers(1, &target.glowFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.glowFBO);
    m_gl->glGenTextures(1, &target.glowTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, target.glowTexture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.glowTexture, 0);
    if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Glow FBO not complete!";

    // Ping-Pong FBOs for Blurring
    m_gl->glGenFramebuffers(2, target.pingpongFBO);
    m_gl->glGenTextures(2, target.pingpongTexture);
    for (unsigned int i = 0; i < 2; i++) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.pingpongFBO[i]);
        m_gl->glBindTexture(GL_TEXTURE_2D, target.pingpongTexture[i]);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.pingpongTexture[i], 0);
        if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "Pingpong FBO " << i << " not complete!";
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind FBO
}

