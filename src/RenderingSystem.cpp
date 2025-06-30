#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "PrimitiveBuilders.hpp"
#include "FieldSolver.hpp" // Included for the new FieldSolver integration

#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLContext> // Required for per-context resource management
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <QDebug>
#include <stdexcept>
#include <string>
#include <QSize>
#include <QSurface>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>

#define CHECK_GL_ERROR()                                                       \
    do {                                                                       \
        GLenum err;                                                            \
        while ((err = m_gl->glGetError()) != GL_NO_ERROR) {                    \
            qCritical() << "!!! OpenGL Error:" << err << "at line" << __LINE__ \
                        << "in file" << __FILE__;                              \
        }                                                                      \
    } while (0)

static const char* fbStatusStr(GLenum s)
{
    switch (s)
    {
    case GL_FRAMEBUFFER_COMPLETE:                       return "COMPLETE";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:          return "INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:  return "MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:         return "INCOMPLETE_DRAW_BUFFER";
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:         return "INCOMPLETE_READ_BUFFER";
    case GL_FRAMEBUFFER_UNSUPPORTED:                    return "UNSUPPORTED";
    default:                                            return "UNKNOWN";
    }
}

static void dumpCompositeUniforms(QOpenGLFunctions_4_1_Core* gl, Shader* sh)
{
    GLint sceneLoc = sh->getLoc("sceneTexture");   // will throw if not found :contentReference[oaicite:0]{index=0}
    GLint glowLoc = sh->getLoc("glowTexture");
    qDebug().nospace() << "[Composite] sceneTexture loc = "
        << sceneLoc << " , glowTexture loc = "
        << glowLoc;
}

static void dumpCompositeAttributes(QOpenGLFunctions_4_1_Core* gl, Shader* sh)
{
    GLint posAttrib = gl->glGetAttribLocation(sh->ID, "pos");   // `ID` is public :contentReference[oaicite:1]{index=1}
    qDebug().nospace() << "[Composite] expects attribute 'pos' ? "
        << (posAttrib != -1);
}

/*  When you suspect the texture itself is empty, call this.
    It reads the very first pixel so you can see whether anything
    besides pure-black ever makes it into the render target.   */
static void debugTexturePixel(QOpenGLFunctions_4_1_Core* gl,
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

static QOpenGLFunctions_4_1_Core* resolveGl41(QOpenGLContext* ctx)
{
    if (!ctx) return nullptr;                     // no context – nothing to do
    auto* f = QOpenGLVersionFunctionsFactory
        ::get<QOpenGLFunctions_4_1_Core>(ctx); // always available
    if (f) f->initializeOpenGLFunctions();        // one-time init
    return f;
}

//---------- CONSTRUCTOR / DESTRUCTOR ------------------

RenderingSystem::RenderingSystem(QOpenGLWidget* viewport,
    QOpenGLFunctions_4_1_Core* gl /* = nullptr */)
    : m_viewportWidget(viewport)
    , m_gl(gl)
{
    // Grab functions from the widget’s context if the caller didn’t pass any
    if (!m_gl && m_viewportWidget)
        m_gl = resolveGl41(m_viewportWidget->context());

    if (!m_gl)
        qWarning() << "[RenderingSystem] Failed to obtain 4.1 core functions!";

    m_fieldSolver = std::make_unique<FieldSolver>();
}

RenderingSystem::~RenderingSystem() {}

//---------- PUBLIC: RENDER LOOP & LIFECYCLE MANAGEMENT ------------------

void RenderingSystem::initialize(int width, int height) {
    if (!m_gl) {
        qWarning() << "[RenderingSystem] Cannot initialize, GL functions not set.";
        return;
    }
    m_width = width;
    m_height = height;

    initShaders();
    initFramebuffers(width, height);
    initRenderPrimitives();
    initFullscreenQuad();

    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void RenderingSystem::shutdown() {
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

    m_gl->glDeleteVertexArrays(1, &m_gridQuadVAO);
    m_gl->glDeleteBuffers(1, &m_gridQuadVBO);
    m_gl->glDeleteVertexArrays(1, &m_arrowVAO);
    m_gl->glDeleteBuffers(1, &m_arrowVBO);
    m_gl->glDeleteBuffers(1, &m_arrowEBO);
    m_gl->glDeleteBuffers(1, &m_instanceVBO);
    m_gl->glDeleteVertexArrays(1, &m_lineVAO);
    m_gl->glDeleteBuffers(1, &m_lineVBO);
    m_gl->glDeleteVertexArrays(1, &m_capVAO);
    m_gl->glDeleteBuffers(1, &m_capVBO);
    m_gl->glDeleteVertexArrays(1, &m_intersectionVAO);
    m_gl->glDeleteBuffers(1, &m_intersectionVBO);
    m_gl->glDeleteVertexArrays(1, &m_quadVAO);

    m_gl->glDeleteFramebuffers(1, &m_mainFBO);
    m_gl->glDeleteTextures(1, &m_mainColorTexture);
    m_gl->glDeleteRenderbuffers(1, &m_mainDepthRenderbuffer);
    m_gl->glDeleteFramebuffers(1, &m_glowFBO);
    m_gl->glDeleteTextures(1, &m_glowTexture);
    m_gl->glDeleteFramebuffers(2, m_pingpongFBO);
    m_gl->glDeleteTextures(2, m_pingpongTexture);
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
    m_registry = &registry;

    // ------------------------------------------------------------------
    // 1. Bind the off-screen framebuffer that collects the scene.
    // ------------------------------------------------------------------
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_mainFBO);

    // ------------------------------------------------------------------
    // 2. *** OPTIONAL DEBUG CLEAR ***
    //    Turn this on to verify that the composite pass really shows
    //    whatever ends up in m_mainFBO.
    // ------------------------------------------------------------------
    
        m_gl->glClearColor(1.0f, 0.0f, 1.0f, 1.0f);          // magenta
        m_gl->glClear(GL_COLOR_BUFFER_BIT);
    

    // ------------------------------------------------------------------
    // 3. Normal per-frame state.
    // ------------------------------------------------------------------
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthFunc(GL_LESS);

    // (If you actually use the stencil buffer elsewhere, leave this on)
    m_gl->glEnable(GL_STENCIL_TEST);
    m_gl->glStencilMask(0xFF);

    // Scene-defined background colour
    const auto& props = m_registry->ctx().get<SceneProperties>();
    m_gl->glClearColor(props.backgroundColor.r,
        props.backgroundColor.g,
        props.backgroundColor.b,
        props.backgroundColor.a);

    // Full clear: colour + depth (+ stencil if attached)
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT);

    // ------------------------------------------------------------------
    // 4. Make it explicit that we render/read from attachment 0 of the FBO.
    //    This prevents the perpetual  GL_INVALID_ENUM <attachment>  spam
    //    once we later bind back to the default framebuffer.
    // ------------------------------------------------------------------
    static const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    m_gl->glDrawBuffers(1, &drawBuf);
    m_gl->glReadBuffer(drawBuf);
}

void RenderingSystem::renderScene(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos, const std::vector<std::vector<glm::vec3>>& intersectionOutlines) {
    // All render passes are now active.
    renderMeshes(registry, view, projection, camPos);
    renderGrid(registry, view, projection, camPos);
    renderSplines(registry, view, projection, camPos, m_width, m_height);
    renderFieldVisualizers(registry, view, projection);
    drawIntersections(intersectionOutlines, view, projection);
    renderSelectionGlow(registry, view, projection);
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_mainFBO);
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    m_gl->glBlitFramebuffer(0, 0, m_width, m_height,
        0, 0, m_width, m_height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void RenderingSystem::endFrame()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;                       // safety – should never happen

    /* --------------------------------------------------------- */
    /*  1. back-buffer                                           */
    /* --------------------------------------------------------- */
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER,
        ctx->defaultFramebufferObject());

    m_gl->glEnable(GL_FRAMEBUFFER_SRGB);

    /* NOTE -----------------------------------------------------
     *  paintGL() has already called glViewport(0,0, fbW, fbH)
     *  for the *current* viewport widget.  Do NOT change it
     *  here or we will clip / stretch other viewports.
     * -------------------------------------------------------- */
   
     /* --------------------------------------------------------- */
     /*  2. States                                                */
     /* --------------------------------------------------------- */
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_STENCIL_TEST);
    m_gl->glDisable(GL_BLEND);
    m_gl->glDisable(GL_CULL_FACE);
    m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);      // black backdrop
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    /* --------------------------------------------------------- */
    /*  3. Composite                                             */
    /* --------------------------------------------------------- */
    m_compositeShader->use();
    m_compositeShader->setInt("sceneTex", 0);
    m_compositeShader->setInt("glowTex", 1);

    // 0 : scene colour
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_mainColorTexture);

    // 1 : final blurred glow
    m_gl->glActiveTexture(GL_TEXTURE1);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_pingpongTexture[1]);

    m_gl->glBindVertexArray(m_quadVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);          // fullscreen triangle
    m_gl->glBindVertexArray(0);

    /* --------------------------------------------------------- */
    /* 4. Tidy                                                   */
    /* --------------------------------------------------------- */
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
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
    auto viewRM = registry.view<RenderableMeshComponent, TransformComponent>();

    for (auto entity : viewRM) {
        if (entity == m_currentCamera || isDescendantOf(registry, entity, m_currentCamera)) {
            continue;
        }

        auto& mesh = viewRM.get<RenderableMeshComponent>(entity);
        auto& xf = viewRM.get<TransformComponent>(entity);

        // FIX: Use MaterialComponent. If it doesn't exist, fall back to a default gray color.
        // The reference to 'mesh.colour' is now removed.
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
    auto viewG = registry.view<GridComponent, TransformComponent>();
    if (viewG.begin() == viewG.end()) return;

    // Lambda to draw the grid's quad geometry.
    const auto drawQuad = [&] {
        m_gl->glBindVertexArray(m_gridQuadVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
        };

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
    if (!m_glowShader || !m_capShader) return;

    // Common GL state for all splines
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_FALSE); // Disable depth writing for transparency effects.
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto e : registry.view<SplineComponent>())
    {
        const auto& sp = registry.get<SplineComponent>(e);
        std::vector<glm::vec3> lineStripPoints;

        // 1. Generate vertex data on the CPU based on spline type.
        switch (sp.type) {
        case SplineType::Linear:     lineStripPoints = evaluateLinearCPU(sp.controlPoints); break;
        case SplineType::CatmullRom: lineStripPoints = evaluateCatmullRomCPU(sp.controlPoints, 64); break;
        case SplineType::Bezier:     lineStripPoints = evaluateBezierCPU(sp.controlPoints, 64); break;
        case SplineType::Parametric: lineStripPoints = evaluateParametricCPU(sp.parametric.func, 128); break;
        }

        if (lineStripPoints.size() < 2) continue;

        // 2. Draw the main line segments using the glow shader.
        m_glowShader->use();
        m_glowShader->setMat4("u_view", view);
        m_glowShader->setMat4("u_proj", proj);
        m_glowShader->setFloat("u_thickness", sp.thickness);
        m_glowShader->setVec2("u_viewport_size", glm::vec2(viewportWidth, viewportHeight));
        m_glowShader->setVec4("u_glowColour", sp.glowColour);
        m_glowShader->setVec4("u_coreColour", sp.coreColour);

        // Convert line strip to line segments for GL_LINES topology.
        std::vector<glm::vec3> lineSegments;
        lineSegments.reserve((lineStripPoints.size() - 1) * 2);
        for (size_t i = 0; i < lineStripPoints.size() - 1; ++i) {
            lineSegments.push_back(lineStripPoints[i]);
            lineSegments.push_back(lineStripPoints[i + 1]);
        }

        m_gl->glBindVertexArray(m_lineVAO);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        m_gl->glBufferData(GL_ARRAY_BUFFER, lineSegments.size() * sizeof(glm::vec3), lineSegments.data(), GL_DYNAMIC_DRAW);
        m_gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineSegments.size()));

        // 3. Draw caps on line joints to make them appear rounded.
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
            capPoints = { lineStripPoints.front(), lineStripPoints.back() };
        }

        if (!capPoints.empty()) {
            m_gl->glBindVertexArray(m_capVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_capVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, capPoints.size() * sizeof(glm::vec3), capPoints.data(), GL_DYNAMIC_DRAW);
            m_gl->glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(capPoints.size()));
        }
    }

    // Cleanup OpenGL state.
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glBindVertexArray(0);
}

void RenderingSystem::renderFieldVisualizers(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection) {
    if (!m_instancedArrowShader || m_arrowVAO == 0) return;

    auto visualizerView = registry.view<const FieldVisualizerComponent, const TransformComponent>();

    for (auto entity : visualizerView) {
        const auto& vis = visualizerView.get<const FieldVisualizerComponent>(entity);
        if (!vis.isEnabled) continue;

        const auto& xf = visualizerView.get<const TransformComponent>(entity);

        struct InstanceData { glm::mat4 modelMatrix; glm::vec3 color; };
        std::vector<InstanceData> instanceData;
        glm::vec3 minBound = -vis.bounds / 2.0f;

        // Generate instance data for each arrow.
        for (int x = 0; x < vis.density.x; ++x) {
            for (int y = 0; y < vis.density.y; ++y) {
                for (int z = 0; z < vis.density.z; ++z) {
                    // Calculate sample position in world space.
                    glm::vec3 t = {
                        (vis.density.x > 1) ? static_cast<float>(x) / (vis.density.x - 1) : 0.5f,
                        (vis.density.y > 1) ? static_cast<float>(y) / (vis.density.y - 1) : 0.5f,
                        (vis.density.z > 1) ? static_cast<float>(z) / (vis.density.z - 1) : 0.5f,
                    };
                    glm::vec3 localPos = minBound + t * vis.bounds;
                    glm::vec3 worldPos = xf.getTransform() * glm::vec4(localPos, 1.0f);

                    // Sample the field and check magnitude.
                    glm::vec3 fieldValue = m_fieldSolver->getVectorAt(registry, worldPos, vis.sourceEntities);
                    float magnitude = glm::length(fieldValue);
                    if (magnitude < vis.cullingThreshold) continue;

                    // Build the transformation matrix for this arrow instance.
                    glm::mat4 trans = glm::translate(glm::mat4(1.0f), worldPos);
                    glm::mat4 rot = glm::mat4_cast(rotationBetweenVectors(glm::vec3(0, 0, 1), fieldValue));
                    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(vis.arrowHeadScale, vis.arrowHeadScale, magnitude * vis.vectorScale));
                    glm::mat4 model = trans * rot * scale;

                    // Determine the color based on magnitude or potential (polarity).
                    glm::vec3 color;
                    if (vis.coloringMode == FieldColoringMode::Polarity) {
                        float potential = m_fieldSolver->getPotentialAt(registry, worldPos, vis.sourceEntities);
                        float normPotential = (potential - vis.minPotential) / (vis.maxPotential - vis.minPotential);
                        color = glm::vec3(getColorFromGradient(glm::clamp(normPotential, 0.0f, 1.0f), vis.colorGradient));
                    }
                    else { // Default to Magnitude
                        float normMag = glm::clamp((magnitude - vis.minMagnitude) / (vis.maxMagnitude - vis.minMagnitude), 0.0f, 1.0f);
                        color = glm::vec3(getColorFromGradient(normMag, vis.colorGradient));
                    }

                    instanceData.push_back({ model, color });
                }
            }
        }
        if (instanceData.empty()) continue;

        // Upload and draw all instances for this visualizer in one batch.
        m_instancedArrowShader->use();
        m_instancedArrowShader->setMat4("view", view);
        m_instancedArrowShader->setMat4("projection", projection);

        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        m_gl->glBufferSubData(GL_ARRAY_BUFFER, 0, instanceData.size() * sizeof(InstanceData), instanceData.data());

        m_gl->glBindVertexArray(m_arrowVAO);
        m_gl->glDrawElementsInstanced(GL_TRIANGLES, m_arrowIndexCount, GL_UNSIGNED_INT, 0, instanceData.size());
    }
    m_gl->glBindVertexArray(0);
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

void RenderingSystem::renderSelectionGlow(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection) {
    auto viewSelected = registry.view<SelectedComponent, RenderableMeshComponent, TransformComponent>();
    if (viewSelected.size_hint() == 0) {
        // If nothing is selected, we still need to clear the glow texture to avoid stale glows from previous frames.
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_glowFBO);
        m_gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        m_gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // --- PASS 1: Render solid emissive color of selected objects to the glow FBO.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_glowFBO);
    m_gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT); // No depth clear needed, we render on a clean slate.

    m_emissiveSolidShader->use();
    m_emissiveSolidShader->setMat4("view", view);
    m_emissiveSolidShader->setMat4("projection", projection);
    m_emissiveSolidShader->setVec3("emissiveColor", glm::vec3(1.0f, 0.75f, 0.1f)); // Bright yellow/orange glow.

    for (auto entity : viewSelected) {
        auto [mesh, transform] = viewSelected.get<RenderableMeshComponent, TransformComponent>(entity);
        m_emissiveSolidShader->setMat4("model", transform.getTransform());

        // We must use the same per-context resource logic as in renderMeshes.
        auto& res = registry.get_or_emplace<RenderResourceComponent>(entity);
        auto& buf = res.perContext[QOpenGLContext::currentContext()];
        if (buf.VAO != 0) {
            m_gl->glBindVertexArray(buf.VAO);
            m_gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
    }

    // --- PASS 2: Apply Gaussian blur to the glow texture using ping-pong FBOs.
    bool horizontal = true, first_iteration = true;
    unsigned int amount = 10; // Number of blur passes (5 horizontal, 5 vertical).
    m_blurShader->use();
    m_blurShader->setInt("screenTexture", 0);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glDisable(GL_DEPTH_TEST); // Blur is a 2D effect.

    for (unsigned int i = 0; i < amount; i++) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[horizontal]);
        m_gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        m_blurShader->setBool("horizontal", horizontal);

        // On the first pass, the input is the original glow texture.
        // After that, the input is the output of the previous blur pass.
        m_gl->glBindTexture(GL_TEXTURE_2D, first_iteration ? m_glowTexture : m_pingpongTexture[!horizontal]);

        m_gl->glBindVertexArray(m_quadVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);

        horizontal = !horizontal;
        if (first_iteration) first_iteration = false;
    }

    m_gl->glEnable(GL_DEPTH_TEST); // Re-enable depth testing for the next frame's main passes.
    // The final, fully blurred texture is now in m_pingpongTexture[0].
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

        xf.translation = cam.getPosition(); // Position comes directly from camera.

        // Build a rotation matrix to align the object's Z-axis with the camera's view direction.
        glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - cam.getPosition());
        glm::vec3 up = glm::vec3(0, 1, 0); // Assuming world up is +Y.
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::cross(right, fwd);

        xf.rotation = glm::quat_cast(glm::mat3(right, up, -fwd));
    }
}

void RenderingSystem::initShaders() {
    try {
        m_phongShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/vertex_shader.glsl", "D:/RoboticsSoftware/shaders/fragment_shader.glsl");
        m_gridShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/grid_vert.glsl", "D:/RoboticsSoftware/shaders/grid_frag.glsl");
        m_instancedArrowShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/instanced_arrow_vert.glsl", "D:/RoboticsSoftware/shaders/instanced_arrow_frag.glsl");
        m_outlineShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/outline_vert.glsl", "D:/RoboticsSoftware/shaders/outline_frag.glsl");

        m_emissiveSolidShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/vertex_shader.glsl", "D:/RoboticsSoftware/shaders/emissive_solid_frag.glsl");
        m_blurShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/post_process_vert.glsl", "D:/RoboticsSoftware/shaders/gaussian_blur_frag.glsl");
        m_compositeShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/post_process_vert.glsl", "D:/RoboticsSoftware/shaders/composite_frag.glsl");

        // NOTE: The following lines will still fail until you modify Shader.hpp
        // See instructions below on how to add the 'buildGeometryShader' function.
        m_glowShader = Shader::buildGeometryShader(m_gl, "D:/RoboticsSoftware/shaders/glow_line_vert.glsl", "D:/RoboticsSoftware/shaders/glow_line_geom.glsl", "D:/RoboticsSoftware/shaders/glow_line_frag.glsl");
        m_capShader = Shader::buildGeometryShader(m_gl, "D:/RoboticsSoftware/shaders/cap_vert.glsl", "D:/RoboticsSoftware/shaders/cap_geom.glsl", "D:/RoboticsSoftware/shaders/cap_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qFatal("[RenderingSystem] FATAL: Shader initialization failed: %s", e.what());
    }
}

void RenderingSystem::initFullscreenQuad() {
    // A single, empty VAO is sufficient for drawing a fullscreen triangle.
    // The vertex positions are generated directly in the vertex shader (a common trick).
    m_gl->glGenVertexArrays(1, &m_quadVAO);
}

void RenderingSystem::initRenderPrimitives() {
    // --- Grid Plane ---
    float gridPlaneVertices[] = { -2000.f,0,-2000.f, 2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,-2000.f, 2000.f,0,2000.f, -2000.f,0,2000.f };
    m_gl->glGenVertexArrays(1, &m_gridQuadVAO);
    m_gl->glGenBuffers(1, &m_gridQuadVBO);
    m_gl->glBindVertexArray(m_gridQuadVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_gridQuadVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices), gridPlaneVertices, GL_STATIC_DRAW);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // --- Instanced Arrow for Vector Fields ---
    std::vector<Vertex> arrowVertices;
    std::vector<unsigned int> arrowIndices;
    createArrowPrimitive(arrowVertices, arrowIndices);
    m_arrowIndexCount = arrowIndices.size();

    m_gl->glGenVertexArrays(1, &m_arrowVAO);
    m_gl->glGenBuffers(1, &m_arrowVBO);
    m_gl->glGenBuffers(1, &m_arrowEBO);
    m_gl->glGenBuffers(1, &m_instanceVBO);
    m_gl->glBindVertexArray(m_arrowVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_arrowVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, arrowVertices.size() * sizeof(Vertex), arrowVertices.data(), GL_STATIC_DRAW);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_arrowEBO);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, arrowIndices.size() * sizeof(unsigned int), arrowIndices.data(), GL_STATIC_DRAW);

    // Base mesh attributes (aPos, aNormal)
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    // Per-instance attributes (model matrix, color)
    struct InstanceData { glm::mat4 modelMatrix; glm::vec3 color; };
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, 20 * 20 * 20 * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

    GLsizei vec4Size = sizeof(glm::vec4);
    // FIX: Cast integer offsets to uintptr_t before casting to void* to prevent 64-bit warnings.
    m_gl->glEnableVertexAttribArray(2); m_gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)0);
    m_gl->glEnableVertexAttribArray(3); m_gl->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(uintptr_t)(vec4Size));
    m_gl->glEnableVertexAttribArray(4); m_gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(uintptr_t)(2 * vec4Size));
    m_gl->glEnableVertexAttribArray(5); m_gl->glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(uintptr_t)(3 * vec4Size));
    m_gl->glVertexAttribDivisor(2, 1); m_gl->glVertexAttribDivisor(3, 1); m_gl->glVertexAttribDivisor(4, 1); m_gl->glVertexAttribDivisor(5, 1);

    m_gl->glEnableVertexAttribArray(6);
    // FIX: Cast integer offsets to uintptr_t before casting to void* to prevent 64-bit warnings.
    m_gl->glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(uintptr_t)(sizeof(glm::mat4)));
    m_gl->glVertexAttribDivisor(6, 1);

    // --- Spline Primitives (Line & Cap) ---
    m_gl->glGenVertexArrays(1, &m_lineVAO);
    m_gl->glGenBuffers(1, &m_lineVBO);
    m_gl->glBindVertexArray(m_lineVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    m_gl->glGenVertexArrays(1, &m_capVAO);
    m_gl->glGenBuffers(1, &m_capVBO);
    m_gl->glBindVertexArray(m_capVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_capVBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    // --- Intersection Outline Primitive ---
    m_gl->glGenVertexArrays(1, &m_intersectionVAO);
    m_gl->glGenBuffers(1, &m_intersectionVBO);
    m_gl->glBindVertexArray(m_intersectionVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_intersectionVBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    m_gl->glBindVertexArray(0); // Unbind VAO.
}

void RenderingSystem::initFramebuffers(int width, int height) {
    // Main Scene Framebuffer
    m_gl->glGenFramebuffers(1, &m_mainFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_mainFBO);
    m_gl->glGenTextures(1, &m_mainColorTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_mainColorTexture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_mainColorTexture, 0);
    m_gl->glGenRenderbuffers(1, &m_mainDepthRenderbuffer);
    m_gl->glBindRenderbuffer(GL_RENDERBUFFER, m_mainDepthRenderbuffer);
    m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    m_gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_mainDepthRenderbuffer);
    if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Main FBO not complete!";

    // Glow Framebuffer
    m_gl->glGenFramebuffers(1, &m_glowFBO);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_glowFBO);
    m_gl->glGenTextures(1, &m_glowTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_glowTexture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_glowTexture, 0);
    if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Glow FBO not complete!";

    // Ping-Pong Framebuffers for Blurring
    m_gl->glGenFramebuffers(2, m_pingpongFBO);
    m_gl->glGenTextures(2, m_pingpongTexture);
    for (unsigned int i = 0; i < 2; i++) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[i]);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_pingpongTexture[i]);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pingpongTexture[i], 0);
        if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "Pingpong FBO " << i << " not complete!";
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}