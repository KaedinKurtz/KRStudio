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
        // Using an absolute path for debugging consistency.
        m_phongShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/vertex_shader.glsl", "D:/RoboticsSoftware/shaders/fragment_shader.glsl");
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[RenderingSystem] FATAL: Failed to initialize Phong shader:" << e.what();
    }

    // --- Load Grid Shader ---
    try {
        qDebug() << "[RenderingSystem] Attempting to load grid shaders from absolute path...";
        // FINAL DEBUGGING STEP: Use a hardcoded, absolute path to the shader files.
        // This removes all doubt about whether the file is being found.
        m_gridShader = std::make_unique<Shader>(m_gl, "D:/RoboticsSoftware/shaders/grid_vert.glsl", "D:/RoboticsSoftware/shaders/grid_frag.glsl");
        qDebug() << "[RenderingSystem] SUCCESS: Grid shader loaded and compiled.";
    }
    catch (const std::runtime_error& e) {
        qCritical() << "[RenderingSystem] FATAL: Failed to initialize Grid shader. Error:" << e.what();
    }

    // --- Create geometry for the grid (a large plane) ---
    float gridPlaneVertices[] = {
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f,
        -2000.0f, 0.0f, -2000.0f,  2000.0f, 0.0f,  2000.0f, -2000.0f, 0.0f,  2000.0f
    };
    m_gl->glGenVertexArrays(1, &m_gridQuadVAO);
    m_gl->glGenBuffers(1, &m_gridQuadVBO);
    m_gl->glBindVertexArray(m_gridQuadVAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_gridQuadVBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(gridPlaneVertices), &gridPlaneVertices, GL_STATIC_DRAW);
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

void RenderingSystem::renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_gridShader) {
        return;
    }

    auto gridView = registry.view<GridComponent, TransformComponent>();
    if (gridView.size_hint() == 0) return;

    auto gridEntity = gridView.front();
    auto& grid = gridView.get<GridComponent>(gridEntity);
    auto& transform = gridView.get<TransformComponent>(gridEntity);

    if (!grid.masterVisible) return;

    m_gl->glDepthMask(GL_FALSE);
    m_gridShader->use();

    // Set uniforms
    m_gridShader->setMat4("u_viewMatrix", view);
    m_gridShader->setMat4("u_projectionMatrix", projection);
    m_gridShader->setMat4("u_gridModelMatrix", transform.getTransform());
    m_gridShader->setVec3("u_cameraPos", camPosition);
    m_gridShader->setFloat("u_distanceToGrid", glm::length(camPosition - transform.translation));
    m_gridShader->setInt("u_numLevels", grid.levels.size());
    for (size_t i = 0; i < grid.levels.size() && i < 5; ++i) {
        std::string base = "u_levels[" + std::to_string(i) + "].";
        m_gridShader->setFloat(base + "spacing", grid.levels[i].spacing);
        m_gridShader->setVec3(base + "color", grid.levels[i].color);
        m_gridShader->setFloat(base + "fadeInCameraDistanceEnd", grid.levels[i].fadeInCameraDistanceEnd);
        m_gridShader->setFloat(base + "fadeInCameraDistanceStart", grid.levels[i].fadeInCameraDistanceStart);
        m_gridShader->setBool("u_levelVisible[" + std::to_string(i) + "]", grid.levelVisible[i]);
    }
    m_gridShader->setBool("u_isDotted", grid.isDotted);
    m_gridShader->setFloat("u_baseLineWidthPixels", grid.baseLineWidthPixels);
    m_gridShader->setBool("u_showAxes", grid.showAxes);
    m_gridShader->setVec3("u_xAxisColor", grid.xAxisColor);
    m_gridShader->setVec3("u_zAxisColor", grid.zAxisColor);
    m_gridShader->setFloat("u_axisLineWidthPixels", grid.baseLineWidthPixels * 1.5f);
    const auto& sceneProps = registry.ctx().get<SceneProperties>();
    m_gridShader->setBool("u_useFog", sceneProps.fogEnabled);
    m_gridShader->setVec3("u_fogColor", sceneProps.fogColor);
    m_gridShader->setFloat("u_fogStartDistance", sceneProps.fogStartDistance);
    m_gridShader->setFloat("u_fogEndDistance", sceneProps.fogEndDistance);

    // Draw call
    m_gl->glBindVertexArray(m_gridQuadVAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);

    m_gl->glDepthMask(GL_TRUE);
}

void RenderingSystem::renderMeshes(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_phongShader) return;

    m_phongShader->use();
    m_phongShader->setMat4("view", view);
    m_phongShader->setMat4("projection", projection);
    m_phongShader->setVec3("viewPos", camPosition);

    auto renderableView = registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto entity : renderableView) {
        auto& mesh = renderableView.get<RenderableMeshComponent>(entity);
        auto& xf = renderableView.get<TransformComponent>(entity);
        auto& res = registry.get_or_emplace<RenderResourceComponent>(entity);

        if (res.VAO == 0) {
            m_gl->glGenVertexArrays(1, &res.VAO);
            m_gl->glGenBuffers(1, &res.VBO);
            m_gl->glGenBuffers(1, &res.EBO);
            m_gl->glBindVertexArray(res.VAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, res.VBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(glm::vec3), mesh.vertices.data(), GL_STATIC_DRAW);
            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.EBO);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
            m_gl->glBindVertexArray(0);
        }

        m_phongShader->setMat4("model", xf.getTransform());
        m_gl->glBindVertexArray(res.VAO);
        m_gl->glDrawElements(GL_TRIANGLES, static_cast<int>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
        m_gl->glBindVertexArray(0);
    }
}
