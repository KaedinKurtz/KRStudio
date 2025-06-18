#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Camera.hpp"

#include <QOpenGLFunctions_4_1_Core>
#include <QDebug>
#include <stdexcept>
#include <string>
#include <glm/gtc/type_ptr.hpp>

//---------- HELPER FUNCTIONS ------------------

static std::vector<glm::vec3> evaluateCatmullRomCPU(const std::vector<glm::vec3>& controlPoints, int segmentsPerCurve)
{
    std::vector<glm::vec3> lineVertices;
    // A Catmull-Rom segment requires 4 points, so we need at least that many to draw anything.
    if (controlPoints.size() < 4) {
        return lineVertices;
    }

    // Reserve space for efficiency
    lineVertices.reserve(static_cast<size_t>(controlPoints.size() - 3) * segmentsPerCurve);

    // Iterate through each segment of the spline
    for (size_t i = 0; i < controlPoints.size() - 3; ++i) {
        const glm::vec3& p0 = controlPoints[i];
        const glm::vec3& p1 = controlPoints[i + 1];
        const glm::vec3& p2 = controlPoints[i + 2];
        const glm::vec3& p3 = controlPoints[i + 3];

        // Generate the points for this segment
        for (int j = 0; j < segmentsPerCurve; ++j) {
            float t = static_cast<float>(j) / (segmentsPerCurve - 1);
            float t2 = t * t;
            float t3 = t2 * t;

            // The Catmull-Rom equation
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

static std::vector<glm::vec3> evaluateLinearCPU(const std::vector<glm::vec3>& controlPoints) {
    return controlPoints;
}

static std::vector<glm::vec3> evaluateBezierCPU(const std::vector<glm::vec3>& controlPoints, int numSegments) {
    std::vector<glm::vec3> lineVertices;
    if (controlPoints.empty()) {
        return lineVertices;
    }
    lineVertices.reserve(numSegments);

    auto binomialCoeff = [](int n, int k) {
        long long res = 1;
        if (k > n - k) k = n - k;
        for (int i = 0; i < k; ++i) {
            res = res * (n - i);
            res = res / (i + 1);
        }
        return (float)res;
        };

    int n = static_cast<int>(controlPoints.size()) - 1;
    for (int i = 0; i < numSegments; ++i) {
        float t = static_cast<float>(i) / (numSegments - 1);
        glm::vec3 point(0.0f);
        for (int j = 0; j <= n; ++j) {
            float bernstein = binomialCoeff(n, j) * pow(t, j) * pow(1 - t, n - j);
            point += controlPoints[j] * bernstein;
        }
        lineVertices.push_back(point);
    }
    return lineVertices;
}

static std::vector<glm::vec3> evaluateParametricCPU(const std::function<glm::vec3(float)>& func, int numSegments) {
    std::vector<glm::vec3> lineVertices;
    if (!func) {
        return lineVertices;
    }
    lineVertices.reserve(numSegments);
    for (int i = 0; i < numSegments; ++i) {
        float t = static_cast<float>(i) / (numSegments - 1);
        lineVertices.push_back(func(t));
    }
    return lineVertices;
}

RenderingSystem::RenderingSystem(QOpenGLFunctions_4_1_Core* gl)
    : m_gl(gl),
    m_phongShader(nullptr),
    m_gridShader(nullptr),
    m_outlineShader(nullptr),
    m_gridQuadVAO(0),
    m_gridQuadVBO(0),
    m_intersectionVAO(0),
    m_intersectionVBO(0)
{
    if (!m_gl) {
        qWarning() << "[RenderingSystem] Error: QOpenGLFunctions_4_1_Core pointer is null!";
    }
}

RenderingSystem::~RenderingSystem() {}

void RenderingSystem::initialize() {
    if (!m_gl) {
        qWarning() << "[RenderingSystem] Cannot initialize, GL functions not set.";
        return;
    }

    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    try {
        m_phongShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/vertex_shader.glsl", "D:/RoboticsSoftware/shaders/fragment_shader.glsl");
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[RenderingSystem] FATAL: Failed to initialize Phong shader:" << e.what();
    }

    try {
        m_gridShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/grid_vert.glsl", "D:/RoboticsSoftware/shaders/grid_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Grid shader. Error:" << e.what();
    }

    try {
        m_outlineShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/outline_vert.glsl", "D:/RoboticsSoftware/shaders/outline_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Outline shader. Error:" << e.what();
    }

    try {
        m_splineShader = Shader::buildTessellatedShader(
            m_gl,
            "D:/RoboticsSoftware/shaders/spline_vert.glsl",
            "D:/RoboticsSoftware/shaders/spline_tesc.glsl",
            "D:/RoboticsSoftware/shaders/spline_tese.glsl",
            "D:/RoboticsSoftware/shaders/spline_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: spline shader load failed:"
            << e.what();
    }

    try {
        // This is our new, simple shader for drawing lines
        m_lineShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/line_vert.glsl", "D:/RoboticsSoftware/shaders/line_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Line shader:" << e.what();
    }

    try {
        m_glowShader = std::make_unique<Shader>(m_gl,
            std::vector<std::string>{
            "D:/RoboticsSoftware/shaders/glow_line_vert.glsl",
                "D:/RoboticsSoftware/shaders/glow_line_geom.glsl",
                "D:/RoboticsSoftware/shaders/glow_line_frag.glsl"
        }
        );
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Glow shader:" << e.what();
    }

    try {
        m_capShader = std::make_unique<Shader>(m_gl,
            std::vector<std::string>{
            "D:/RoboticsSoftware/shaders/cap_vert.glsl",
                "D:/RoboticsSoftware/shaders/cap_geom.glsl",
                "D:/RoboticsSoftware/shaders/cap_frag.glsl"
        }
        );
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Cap shader:" << e.what();
    }

    // Setup for the new cap renderer
    m_gl->glGenVertexArrays(1, &m_capVAO);
    m_gl->glGenBuffers(1, &m_capVBO);
    m_gl->glBindVertexArray(m_capVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_capVBO);
    // The VAO for points is identical to the line VAO
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    m_gl->glBindVertexArray(0);

    // Setup for the new line renderer
    m_gl->glGenVertexArrays(1, &m_lineVAO);
    m_gl->glGenBuffers(1, &m_lineVBO);
    m_gl->glBindVertexArray(m_lineVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    m_gl->glBindVertexArray(0);

    float gridPlaneVertices[] = {
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f,
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f, -2000.0f, 0.0f,  2000.0f
    };

    GLint bits = 24;
    m_gl->glGetIntegerv(GL_DEPTH_BITS, &bits);
    m_depthBits = static_cast<int>(bits);

    m_gl->glGenVertexArrays(1, &m_intersectionVAO);
    m_gl->glGenBuffers(1, &m_intersectionVBO);
    m_gl->glBindVertexArray(m_intersectionVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_intersectionVBO);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    m_gl->glBindVertexArray(0);

    m_gl->glGenVertexArrays(1, &m_gridQuadVAO);
    m_gl->glGenBuffers(1, &m_gridQuadVBO);
    m_gl->glBindVertexArray(m_gridQuadVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_gridQuadVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices), gridPlaneVertices, GL_STATIC_DRAW);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glBindVertexArray(0);

    m_gl->glGenVertexArrays(1, &m_tmpVAO);
    m_gl->glGenBuffers(1, &m_tmpVBO);
    m_gl->glGenBuffers(1, &m_splineCpSSBO);
    m_gl->glBindVertexArray(m_tmpVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_tmpVBO);
    // no data yet – the real upload happens every frame
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(glm::vec3), (void*)0);
    m_gl->glBindVertexArray(0);
}

void RenderingSystem::shutdown() {
    // Note: Shutdown no longer needs the registry as RenderResourceComponent has been removed
    if (m_gridQuadVAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_gridQuadVAO);
        m_gl->glDeleteBuffers(1, &m_gridQuadVBO);
    }
    if (m_intersectionVAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_intersectionVAO);
        m_gl->glDeleteBuffers(1, &m_intersectionVBO);
    }
    if (m_tmpVAO) {
        m_gl->glDeleteVertexArrays(1, &m_tmpVAO);
        m_gl->glDeleteBuffers(1, &m_tmpVBO);
    }
    if (m_splineCpSSBO) {
        m_gl->glDeleteBuffers(1, &m_splineCpSSBO);
    }
    if (m_capVAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_capVAO);
        m_gl->glDeleteBuffers(1, &m_capVBO);
    }
}


void RenderingSystem::renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos)
{
    if (!m_gridShader) return;
    auto viewG = registry.view<GridComponent, TransformComponent>();
    if (viewG.begin() == viewG.end()) return;
    const auto drawQuad = [&] {
        m_gl->glBindVertexArray(m_gridQuadVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
        };
    const auto sendCommonUniforms = [&](const GridComponent& g, const TransformComponent& xf) {
        const float camDist = glm::length(camPos - xf.translation);
        m_gridShader->setMat4("u_viewMatrix", view);
        m_gridShader->setMat4("u_projectionMatrix", projection);
        m_gridShader->setMat4("u_gridModelMatrix", xf.getTransform());
        m_gridShader->setVec3("u_cameraPos", camPos);
        m_gridShader->setFloat("u_distanceToGrid", camDist);
        m_gridShader->setInt("u_numLevels", int(g.levels.size()));
        for (std::size_t i = 0; i < g.levels.size() && i < 5; ++i) {
            const std::string b = "u_levels[" + std::to_string(i) + "].";
            m_gridShader->setFloat(b + "spacing", g.levels[i].spacing);
            m_gridShader->setVec3(b + "color", g.levels[i].color);
            m_gridShader->setFloat(b + "fadeInCameraDistanceStart", g.levels[i].fadeInCameraDistanceStart);
            m_gridShader->setFloat(b + "fadeInCameraDistanceEnd", g.levels[i].fadeInCameraDistanceEnd);
            m_gridShader->setBool("u_levelVisible[" + std::to_string(i) + "]", g.levelVisible[i]);
        }
        m_gridShader->setBool("u_isDotted", g.isDotted);
        m_gridShader->setFloat("u_baseLineWidthPixels", g.baseLineWidthPixels);
        m_gridShader->setBool("u_showAxes", g.showAxes);
        m_gridShader->setVec3("u_xAxisColor", g.xAxisColor);
        m_gridShader->setVec3("u_zAxisColor", g.zAxisColor);
        m_gridShader->setFloat("u_axisLineWidthPixels", g.baseLineWidthPixels * 1.5f);
        const auto& props = registry.ctx().get<SceneProperties>();
        m_gridShader->setBool("u_useFog", props.fogEnabled);
        m_gridShader->setVec3("u_fogColor", props.fogColor);
        m_gridShader->setFloat("u_fogStartDistance", props.fogStartDistance);
        m_gridShader->setFloat("u_fogEndDistance", props.fogEndDistance);
        };
    m_gl->glEnable(GL_POLYGON_OFFSET_FILL);
    constexpr float kBias = 1.0f;
    for (auto entity : viewG) {
        auto& grid = viewG.get<GridComponent>(entity);
        if (!grid.masterVisible) continue;
        auto& xf = viewG.get<TransformComponent>(entity);
        m_gridShader->use();
        sendCommonUniforms(grid, xf);
        m_gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glPolygonOffset(+kBias, +kBias);
        drawQuad();
        m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_gl->glDepthMask(GL_FALSE);
        m_gl->glPolygonOffset(-2.0f, -2.0f);
        drawQuad();
    }
    m_gl->glDisable(GL_POLYGON_OFFSET_FILL);
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glUseProgram(0); // Release shader
}

void RenderingSystem::updateCameraTransforms(entt::registry& r)
{
    auto view = r.view<CameraComponent, TransformComponent>();
    for (auto e : view) {
        auto& cam = view.get<CameraComponent>(e).camera;
        auto& xf = view.get<TransformComponent>(e);

        // position comes straight from the camera
        xf.translation = cam.getPosition();

        // build a rotation so +Z looks along -camera.forward
        glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - cam.getPosition());
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::cross(right, fwd);

        glm::mat3 rot(right, up, -fwd);           // column-major
        xf.rotation = glm::quat_cast(rot);        // if you store quaternions
    }
}

bool RenderingSystem::isDescendantOf(entt::registry& r,
    entt::entity    e,
    entt::entity    ancestor)
{
    while (r.any_of<ParentComponent>(e)) {
        e = r.get<ParentComponent>(e).parent;
        if (e == ancestor) return true;
    }
    return false;
}

void RenderingSystem::renderMeshes(entt::registry& registry,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& camPos)
{
    if (!m_phongShader) return;

    /* ---- shader globals (unchanged) -------------------------------------- */
    m_phongShader->use();
    m_phongShader->setMat4("view", view);
    m_phongShader->setMat4("projection", projection);
    m_phongShader->setVec3("lightColor", glm::vec3(1.0f));
    m_phongShader->setVec3("lightPos", glm::vec3(5.0f, 10.0f, 5.0f));
    m_phongShader->setVec3("viewPos", camPos);

    QOpenGLContext* ctx = QOpenGLContext::currentContext();   // <- **this** context

    auto viewRM = registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto entity : viewRM)
    {
        if (entity == m_currentCamera) continue;          // the camera itself
        if (RenderingSystem::isDescendantOf(registry, entity, m_currentCamera))
            continue;

        auto& mesh = viewRM.get<RenderableMeshComponent>(entity);
        auto& xf = viewRM.get<TransformComponent>(entity);

        m_phongShader->setVec3("objectColor", glm::vec3(mesh.colour));

        /* ---------- obtain / create buffers for *this* context ----------- */
        auto& res = registry.get_or_emplace<RenderResourceComponent>(entity);
        auto& buf = res.perContext[ctx];        // inserts empty Buffers if absent

        if (buf.VAO == 0)                        // first time in this context
        {
            m_gl->glGenVertexArrays(1, &buf.VAO);
            m_gl->glGenBuffers(1, &buf.VBO);
            m_gl->glGenBuffers(1, &buf.EBO);

            m_gl->glBindVertexArray(buf.VAO);

            m_gl->glBindBuffer(GL_ARRAY_BUFFER, buf.VBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER,
                mesh.vertices.size() * sizeof(Vertex),
                mesh.vertices.data(),
                GL_STATIC_DRAW);

            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.EBO);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                mesh.indices.size() * sizeof(unsigned),
                mesh.indices.data(),
                GL_STATIC_DRAW);

            /* ----- vertex attribute layout -------------------------------- */
            m_gl->glEnableVertexAttribArray(0);        // position
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                sizeof(Vertex),
                (void*)offsetof(Vertex, position));

            m_gl->glEnableVertexAttribArray(1);        // normal
            m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                sizeof(Vertex),
                (void*)offsetof(Vertex, normal));

            m_gl->glBindVertexArray(0);
        }

        /* ---------- draw -------------------------------------------------- */
        glm::mat4 modelMatrix =
            registry.all_of<WorldTransformComponent>(entity)
            ? registry.get<WorldTransformComponent>(entity).matrix   // world
            : xf.getTransform();                                    // local fallback

        m_phongShader->setMat4("model", modelMatrix);
        m_gl->glBindVertexArray(buf.VAO);
        m_gl->glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(mesh.indices.size()),
            GL_UNSIGNED_INT, nullptr);
        m_gl->glBindVertexArray(0);
    }

    m_gl->glUseProgram(0);
}

void RenderingSystem::drawIntersections(const std::vector<std::vector<glm::vec3>>& allOutlines, const glm::mat4& view, const glm::mat4& proj)
{
    if (!m_outlineShader || m_intersectionVAO == 0 || allOutlines.empty()) {
        return;
    }

    m_gl->glDisable(GL_DEPTH_TEST);
    m_outlineShader->use();
    m_outlineShader->setMat4("u_view", view);
    m_outlineShader->setMat4("u_projection", proj);
    m_outlineShader->setVec3("u_outlineColor", glm::vec3(1.0f, 0.5f, 0.0f));

    m_gl->glBindVertexArray(m_intersectionVAO);

    for (const auto& outlinePoints : allOutlines)
    {
        if (outlinePoints.size() > 1)
        {
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_intersectionVBO);
            m_gl->glBufferData(
                GL_ARRAY_BUFFER,
                outlinePoints.size() * sizeof(glm::vec3),
                outlinePoints.data(),
                GL_DYNAMIC_DRAW
            );
            m_gl->glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)outlinePoints.size());
        }
    }

    m_gl->glBindVertexArray(0);
    m_gl->glEnable(GL_DEPTH_TEST);
}

void RenderingSystem::renderSplines(entt::registry& r,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& eye,
    int viewportWidth,
    int viewportHeight)
{
    if (!m_glowShader || !m_capShader) return;

    // Common GL state for all splines
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_FALSE);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto e : r.view<SplineComponent>())
    {
        const SplineComponent& sp = r.get<SplineComponent>(e);
        std::vector<glm::vec3> lineStripPoints;

        // --- 1. Generate Vertex Data (Unchanged) ---
        switch (sp.type) {
        case SplineType::Linear: lineStripPoints = evaluateLinearCPU(sp.controlPoints); break;
        case SplineType::CatmullRom: lineStripPoints = evaluateCatmullRomCPU(sp.controlPoints, 64); break;
        case SplineType::Bezier: lineStripPoints = evaluateBezierCPU(sp.controlPoints, 64); break;
        case SplineType::Parametric: lineStripPoints = evaluateParametricCPU(sp.parametric.func, 128); break;
        }

        if (lineStripPoints.size() < 2) continue;

        // --- 2. Draw the Line Segments (Unchanged) ---
        m_glowShader->use();
        m_glowShader->setMat4("u_view", view);
        m_glowShader->setMat4("u_proj", proj);
        m_glowShader->setFloat("u_thickness", sp.thickness);
        m_glowShader->setVec2("u_viewport_size", glm::vec2(viewportWidth, viewportHeight));
        m_glowShader->setVec4("u_glowColour", sp.glowColour);
        m_glowShader->setVec4("u_coreColour", sp.coreColour);

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

        // --- 3. Draw the Caps (with new conditional logic) ---
        m_capShader->use();
        m_capShader->setMat4("u_view", view);
        m_capShader->setMat4("u_proj", proj);
        m_capShader->setVec2("u_viewport_size", glm::vec2(viewportWidth, viewportHeight));
        m_capShader->setFloat("u_thickness", sp.thickness);
        m_capShader->setVec4("u_glowColour", sp.glowColour);
        m_capShader->setVec4("u_coreColour", sp.coreColour);

        // --- NEW LOGIC: Decide which points get a cap ---
        std::vector<glm::vec3> capPoints;
        if (sp.type == SplineType::Linear)
        {
            // For a Linear spline, every control point is a "corner"
            // that needs a cap to look smooth.
            capPoints = sp.controlPoints;
        }
        else
        {
            // For smooth, curved splines, we only need caps at the
            // absolute beginning and end of the entire line.
            capPoints = { lineStripPoints.front(), lineStripPoints.back() };
        }
        // --- END NEW LOGIC ---

        if (!capPoints.empty())
        {
            m_gl->glBindVertexArray(m_capVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_capVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, capPoints.size() * sizeof(glm::vec3), capPoints.data(), GL_DYNAMIC_DRAW);
            m_gl->glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(capPoints.size()));
        }
    }

    // --- Cleanup OpenGL State (Unchanged) ---
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glBindVertexArray(0);
    m_gl->glUseProgram(0);
}

void RenderingSystem::renderFieldVisualizers(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection)
{
    // Get all entities that have a visualizer component and a transform
    auto view = registry.view<const FieldVisualizerComponent, const TransformComponent>();

    for (auto entity : view)
    {
        const auto& visualizer = view.get<const FieldVisualizerComponent>(entity);
        const auto& transform = view.get<const TransformComponent>(entity);

        if (!visualizer.isEnabled) {
            continue;
        }

        // --- Instance Data Generation ---
        // 1. Create a std::vector to hold per-instance data (matrices, colors, etc.).
        // 2. Loop through the volume based on the visualizer's `bounds` and `density`.
        //    for (int x = 0; x < visualizer.density.x; ++x) {
        //        for (int y = 0; y < visualizer.density.y; ++y) {
        //            for (int z = 0; z < visualizer.density.z; ++z) {
        //
        // 3. For each point in the volume, sample the field using the FieldSolver:
        //    `glm::vec3 fieldValue = m_fieldSolver.getVectorAt(registry, point, visualizer.sourceEntities);`
        //
        // 4. Based on the `fieldValue` and the `visualizer.mode`, create the transformation
        //    matrix and color for this instance (this one arrow or potential line).
        //
        // 5. Add this instance data to your vector.
        //            }
        //        }
        //    }
        //
        // --- Drawing ---
        // 6. Upload the entire vector of instance data to the GPU (to an instance VBO).
        // 7. Issue a single instanced draw call (glDrawElementsInstanced) to render all
        //    the arrows/indicators at once.
    }
}