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
    const glm::vec3& eye)
{
    if (!m_splineShader) return;          // shader failed to load – bail

    /* ---------- program-wide uniforms ---------------------------------- */
    m_splineShader->use();
    m_splineShader->setMat4("u_view", view);
    m_splineShader->setMat4("u_proj", proj);
    m_splineShader->setVec3("u_eye", eye);

    m_gl->glLineWidth(3.0f);                       // fat line
    m_gl->glPatchParameteri(GL_PATCH_VERTICES, 1);      // one vertex per patch

for (auto e : r.view<SplineComponent>())
{
    const SplineComponent& sp = r.get<SplineComponent>(e);

    /* colour ----------------------------------------------------------------*/
    m_splineShader->setVec4("u_colour", sp.colour);

    /* ------------ Catmull–Rom ---------------------------------------------*/
    if (sp.type == SplineType::CatmullRom)
    {
        const auto& pts = sp.catmullRom;          // ▼  NO “.pts”

        if (pts.size() < 4)                       // ▼  guard
            continue;

        /* upload the control-points once per frame --------------------------- */
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_tmpVBO);
        m_gl->glBufferData(GL_ARRAY_BUFFER,                // ▼  four params
            static_cast<GLsizeiptr>(pts.size() * sizeof(glm::vec3)),
            pts.data(),
            GL_DYNAMIC_DRAW);

        m_gl->glBindVertexArray(m_tmpVAO);
        m_gl->glDrawArrays(GL_PATCHES, 0,                  // ▼  three params
            static_cast<GLsizei>(pts.size()));
    }
    /* ------------ Parametric ----------------------------------------------*/
    else            /* SplineType::Parametric */
    {
        constexpr int segs = 64;             // samples per curve
        m_splineShader->setInt("u_paramSegs", segs);
        m_gl->glBindVertexArray(0);          // we do all math in the shader
        m_gl->glDrawArrays(GL_PATCHES, 0, segs);
    }
}

m_gl->glBindVertexArray(0);
m_gl->glUseProgram(0);
}