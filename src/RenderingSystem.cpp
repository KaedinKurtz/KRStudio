#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Camera.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QDebug>
#include <stdexcept>
#include <string>

RenderingSystem::RenderingSystem(QOpenGLFunctions_3_3_Core* gl)
    : m_gl(gl), m_phongShader(nullptr), m_gridShader(nullptr)
{
    if (!m_gl) {
        qWarning() << "[RenderingSystem] Error: QOpenGLFunctions_3_3_Core pointer is null!";
    }
}

RenderingSystem::~RenderingSystem() {}

float RenderingSystem::depthOffsetOnePx(const glm::mat4& proj,
    float            camDepth) const
{
    // 1.  Convert the camera-space depth of the fragment we’re looking at
    //     to Normalised-Device-Coordinates (range −1 … +1).
    //
    float ndcZ = (proj[2][2] * camDepth + proj[3][2]) /
        (proj[2][3] * camDepth + proj[3][3]);

    // 2.  Move that NDC value by exactly one pixel in Z.
    //
    //     OpenGL’s Z buffer after projection is [0 … 1] where
    //     zBuffer = ndcZ * 0.5 + 0.5
    //
    //     One pixel in Z is therefore 1.0f / (float)depthBits.  
    //     We ask OpenGL once at start-up and cache it (m_depthBits).
    //
    const float onePixel = 1.0f / static_cast<float>(m_depthBits);
    float ndcZoffset = ndcZ + onePixel;

    // 3.  Convert the offset NDC value back to camera-space depth so that
    //     we know how far to translate.
    //
    //     Solving the projection equation for camDepth:
    //       ndcZ = (A·z + B)/(C·z + D)
    //
    //     → z = (B - D·ndcZ)/(C·ndcZ - A)
    //
    float A = proj[2][2], B = proj[3][2];
    float C = proj[2][3], D = proj[3][3];

    float camDepthOffset = (B - D * ndcZoffset) /
        (C * ndcZoffset - A);

    return camDepthOffset - camDepth;      // the Δz we need
}

void RenderingSystem::initialize() {
    if (!m_gl) {
        qWarning() << "[RenderingSystem] Cannot initialize, GL functions not set.";
        return;
    }

    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Load Phong Shader (for meshes) ---
    try {
        m_phongShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/vertex_shader.glsl", "D:/RoboticsSoftware/shaders/fragment_shader.glsl");
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[RenderingSystem] FATAL: Failed to initialize Phong shader:" << e.what();
    }

    // --- Load Grid Shader ---
    try {
        m_gridShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/grid_vert.glsl", "D:/RoboticsSoftware/shaders/grid_frag.glsl");
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Grid shader. Error:" << e.what();
    }

    // --- Create geometry for the grid (a large plane) ---
    float gridPlaneVertices[] = {
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f,
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f, -2000.0f, 0.0f,  2000.0f
    };

    GLint bits = 24;
    m_gl->glGetIntegerv(GL_DEPTH_BITS, &bits);
    m_depthBits = static_cast<int>(bits);

    m_gl->glGenVertexArrays(1, &m_gridQuadVAO);
    m_gl->glGenBuffers(1, &m_gridQuadVBO);
    m_gl->glBindVertexArray(m_gridQuadVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_gridQuadVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices),
        gridPlaneVertices, GL_STATIC_DRAW);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glBindVertexArray(0);
}

void RenderingSystem::shutdown(entt::registry& registry) {
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view) {
        auto& res = view.get<RenderResourceComponent>(entity);
        m_gl->glDeleteVertexArrays(1, &res.VAO);
        m_gl->glDeleteBuffers(1, &res.VBO);
        m_gl->glDeleteBuffers(1, &res.EBO);
    }
    if (m_gridQuadVAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_gridQuadVAO);
        m_gl->glDeleteBuffers(1, &m_gridQuadVBO);
    }
}

void RenderingSystem::render(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_gl) return;

    m_gl->glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderMeshes(registry, view, projection, camPosition);
    renderGrid(registry, view, projection, camPosition);
}

// ============================================================================
void RenderingSystem::renderGrid(entt::registry& registry,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& camPos)
{
    if (!m_gridShader) return;

    auto viewG = registry.view<GridComponent, TransformComponent>();
    if (viewG.begin() == viewG.end()) return;

    //------------------------------------------------------------------
    // Lambda helpers shared by *every* grid draw in the loop
    //------------------------------------------------------------------
    const auto drawQuad = [&] {
        m_gl->glBindVertexArray(m_gridQuadVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
        };

    const auto sendCommonUniforms =
        [&](const GridComponent& g, const TransformComponent& xf)
        {
            const float camDist = glm::length(camPos - xf.translation);

            m_gridShader->setMat4("u_viewMatrix", view);
            m_gridShader->setMat4("u_projectionMatrix", projection);
            m_gridShader->setMat4("u_gridModelMatrix", xf.getTransform());
            m_gridShader->setVec3("u_cameraPos", camPos);
            m_gridShader->setFloat("u_distanceToGrid", camDist);

            m_gridShader->setInt("u_numLevels", int(g.levels.size()));
            for (std::size_t i = 0; i < g.levels.size() && i < 5; ++i)
            {
                const std::string b = "u_levels[" + std::to_string(i) + "].";
                m_gridShader->setFloat(b + "spacing", g.levels[i].spacing);
                m_gridShader->setVec3(b + "color", g.levels[i].color);
                m_gridShader->setFloat(b + "fadeInCameraDistanceStart",
                    g.levels[i].fadeInCameraDistanceStart);
                m_gridShader->setFloat(b + "fadeInCameraDistanceEnd",
                    g.levels[i].fadeInCameraDistanceEnd);
                m_gridShader->setBool("u_levelVisible[" + std::to_string(i) + "]",
                    g.levelVisible[i]);
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

    //------------------------------------------------------------------
    // Global GL state (depth test already enabled elsewhere)
    //------------------------------------------------------------------
    m_gl->glEnable(GL_POLYGON_OFFSET_FILL);
    constexpr float kBias = 1.0f;

    //------------------------------------------------------------------
    // Loop over every grid entity
    //------------------------------------------------------------------
    for (auto entity : viewG)
    {
        auto& grid = viewG.get<GridComponent>(entity);
        if (!grid.masterVisible) continue;

        auto& xf = viewG.get<TransformComponent>(entity);

        m_gridShader->use();
        sendCommonUniforms(grid, xf);

        //---------------- Pass A – depth only, push behind ------------
        m_gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glPolygonOffset(+kBias, +kBias);
        drawQuad();

        //---------------- Pass B – colour, pull forward ---------------
        m_gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_gl->glDepthMask(GL_FALSE);
        m_gl->glPolygonOffset(-kBias, -kBias);
        drawQuad();
    }

    //------------------------------------------------------------------
    // Restore GL defaults
    //------------------------------------------------------------------
    m_gl->glDisable(GL_POLYGON_OFFSET_FILL);
    m_gl->glDepthMask(GL_TRUE);
}


void RenderingSystem::renderMeshes(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_phongShader) return;

    m_phongShader->use();
    m_phongShader->setMat4("view", view);
    m_phongShader->setMat4("projection", projection);
    m_phongShader->setVec3("objectColor", glm::vec3(0.8f, 0.2f, 0.2f));   // any colour you like
    m_phongShader->setVec3("lightColor", glm::vec3(1.0f));               // white directional light
    m_phongShader->setVec3("lightPos", glm::vec3(5.0f, 10.0f, 5.0f));  // a bit above the scene
    m_phongShader->setVec3("viewPos", camPosition);

    auto renderableView = registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto entity : renderableView) {
        auto& mesh = renderableView.get<RenderableMeshComponent>(entity);
        auto& xf = renderableView.get<TransformComponent>(entity);
        auto& res = registry.emplace_or_replace<RenderResourceComponent>(entity,
            0u, 0u, 0u);

   //     assert((res.VAO == 0 && res.VBO == 0 && res.EBO == 0) &&
    //        "RenderResourceComponent must be zero-initialised");

        if (res.VAO == 0) {
            m_gl->glGenVertexArrays(1, &res.VAO);
            m_gl->glGenBuffers(1, &res.VBO);
            m_gl->glGenBuffers(1, &res.EBO);
            qDebug() << "VAO =" << res.VAO;
            m_gl->glBindVertexArray(res.VAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, res.VBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER,
                mesh.vertices.size() * sizeof(Vertex),
                mesh.vertices.data(),
                GL_STATIC_DRAW);
            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.EBO);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                mesh.indices.size() * sizeof(unsigned),
                mesh.indices.data(),
                GL_STATIC_DRAW);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                sizeof(Vertex), (void*)0);
            m_gl->glEnableVertexAttribArray(1);
            m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                sizeof(Vertex),
                (void*)offsetof(Vertex, normal));
            m_gl->glBindVertexArray(0);
        }

        m_phongShader->setMat4("model", xf.getTransform());
        m_gl->glBindVertexArray(res.VAO);
        m_gl->glDrawElements(GL_TRIANGLES, static_cast<int>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
        qDebug() << "[MESH] VAO =" << res.VAO;
        m_gl->glBindVertexArray(0);
    }
}
