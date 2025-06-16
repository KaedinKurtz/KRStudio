#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Camera.hpp" // We need the Camera for its position.

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
    m_gl->glEnable(GL_BLEND); // Enable blending for the grid's transparency.
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Load Phong Shader (for meshes) ---
    try {
        m_phongShader = std::make_unique<Shader>(m_gl, ":/shaders/phong.vert", ":/shaders/phong.frag");
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[RenderingSystem] Failed to initialize Phong shader:" << e.what();
    }

    // --- Load Grid Shader ---
    try {
        // Use the shader files you provided. Ensure they are in your .qrc resource file.
        m_gridShader = std::make_unique<Shader>(m_gl, ":/shaders/grid.vert", ":/shaders/grid.frag");
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[RenderingSystem] Failed to initialize Grid shader:" << e.what();
    }

    // --- Create geometry for the grid (a large plane) ---
    float gridPlaneVertices[] = {
        // positions          
        -2000.0f, 0.0f, -2000.0f,
         2000.0f, 0.0f, -2000.0f,
         2000.0f, 0.0f,  2000.0f,

        -2000.0f, 0.0f, -2000.0f,
         2000.0f, 0.0f,  2000.0f,
        -2000.0f, 0.0f,  2000.0f
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
    // Clean up the grid geometry.
    if (m_gridQuadVAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_gridQuadVAO);
        m_gl->glDeleteBuffers(1, &m_gridQuadVBO);
    }
}

void RenderingSystem::render(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_gl) return;

    // Clear the back buffer.
    m_gl->glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderMeshes(registry, view, projection, camPosition); // Render all the solid objects first.
    renderGrid(registry, view, projection, camPosition);   // Then, render the transparent grid over them.
}

void RenderingSystem::renderGrid(entt::registry& registry, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPosition) {
    if (!m_gridShader) return; // Don't try to render if the shader failed to compile.

    // Get the first available grid component. In the future, this could support multiple grids.
    auto gridView = registry.view<GridComponent, TransformComponent>();
    // FIX: entt::view does not have .empty(). The correct method is .size_hint()
    if (gridView.size_hint() == 0) return; // No grid entity found.

    auto gridEntity = gridView.front();
    auto& grid = gridView.get<GridComponent>(gridEntity);
    auto& transform = gridView.get<TransformComponent>(gridEntity);

    if (!grid.masterVisible) return; // Skip rendering if the grid is hidden.

    m_gridShader->use(); // Activate the grid shader program.

    // --- Set Camera and Transform Uniforms ---
    m_gridShader->setMat4("u_viewMatrix", view); // Sets the view matrix uniform.
    m_gridShader->setMat4("u_projectionMatrix", projection); // Sets the projection matrix uniform.
    m_gridShader->setMat4("u_gridModelMatrix", transform.getTransform()); // Sets the grid's world position and orientation.
    m_gridShader->setVec3("u_cameraPos", camPosition); // Sets the camera's world position.
    m_gridShader->setFloat("u_distanceToGrid", glm::length(camPosition - transform.translation)); // Approximates distance for fading effects.

    // --- Set Grid Level Uniforms ---
    m_gridShader->setInt("u_numLevels", grid.levels.size()); // Tells the shader how many levels to loop through.
    for (size_t i = 0; i < grid.levels.size() && i < 5; ++i) { // Transfer data for each grid level to the shader.
        std::string base = "u_levels[" + std::to_string(i) + "].";
        m_gridShader->setFloat(base + "spacing", grid.levels[i].spacing);
        m_gridShader->setVec3(base + "color", grid.levels[i].color);
        m_gridShader->setFloat(base + "fadeInCameraDistanceEnd", grid.levels[i].fadeInCameraDistanceEnd);
        m_gridShader->setFloat(base + "fadeInCameraDistanceStart", grid.levels[i].fadeInCameraDistanceStart);
        m_gridShader->setBool("u_levelVisible[" + std::to_string(i) + "]", grid.levelVisible[i]);
    }

    // --- Set Feature and Style Uniforms ---
    m_gridShader->setBool("u_isDotted", grid.isDotted); // Sets whether the grid is solid or dotted.
    m_gridShader->setFloat("u_baseLineWidthPixels", grid.baseLineWidthPixels); // Sets the base thickness of grid lines.
    m_gridShader->setBool("u_showAxes", grid.showAxes); // Sets whether the X and Z axes are visible.
    m_gridShader->setVec3("u_xAxisColor", grid.xAxisColor); // Sets the color for the X axis.
    m_gridShader->setVec3("u_zAxisColor", grid.zAxisColor); // Sets the color for the Z axis.
    m_gridShader->setFloat("u_axisLineWidthPixels", grid.baseLineWidthPixels * 1.5f); // Makes axes slightly thicker than regular lines.

    // --- Set Fog Uniforms ---
    const auto& sceneProps = registry.ctx().get<SceneProperties>();
    m_gridShader->setBool("u_useFog", sceneProps.fogEnabled); // Sets whether fog is active.
    m_gridShader->setVec3("u_fogColor", sceneProps.fogColor); // Sets the color of the fog.
    m_gridShader->setFloat("u_fogStartDistance", sceneProps.fogStartDistance); // Sets the distance where fog begins.
    m_gridShader->setFloat("u_fogEndDistance", sceneProps.fogEndDistance); // Sets the distance where fog is at full density.

    // --- Draw the Grid ---
    m_gl->glBindVertexArray(m_gridQuadVAO); // Bind the quad's VAO.
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6); // Draw the six vertices that make up the quad.
    m_gl->glBindVertexArray(0); // Unbind the VAO.
}

// NOTE: The renderMeshes function is included for completeness but is unchanged.
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
