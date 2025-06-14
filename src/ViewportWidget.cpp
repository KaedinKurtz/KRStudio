#include "ViewportWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"
#include "IntersectionSystem.hpp"

#include <QOpenGLContext>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDebug>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

ViewportWidget::ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent),
    m_scene(scene),
    m_cameraEntity(cameraEntity),
    m_animationTimer(new QTimer(this)),
    m_outlineVAO(0),
    m_outlineVBO(0)
{
    Q_ASSERT(m_scene != nullptr);
    setFocusPolicy(Qt::StrongFocus);
}

ViewportWidget::~ViewportWidget()
{
    if (isValid()) {
        makeCurrent();
        if (m_outlineVAO != 0) glDeleteVertexArrays(1, &m_outlineVAO);
        if (m_outlineVBO != 0) glDeleteBuffers(1, &m_outlineVBO);
    }
}

Camera& ViewportWidget::getCamera()
{
    return m_scene->getRegistry().get<CameraComponent>(m_cameraEntity).camera;
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Grid geometry (positions only)
    const float halfSize = 1000.0f;
    const std::vector<float> grid_vertices = {
        -halfSize, 0.0f, -halfSize,  halfSize, 0.0f, -halfSize,  halfSize, 0.0f,  halfSize,
         halfSize, 0.0f,  halfSize, -halfSize, 0.0f,  halfSize, -halfSize, 0.0f, -halfSize
    };

    // A complete, properly lit cube definition with interleaved positions and normals.
    // Layout: [pos.x, pos.y, pos.z, norm.x, norm.y, norm.z]
    const std::vector<float> lit_cube_vertices = {
        // positions           // normals (pointing outwards from each face)
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // Back face
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // Front face
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // Left face
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // Right face
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, // Bottom face
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // Top face
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f
    };

    const std::vector<unsigned int> lit_cube_indices = {
         0,  1,  2,  2,  3,  0, // Back
         4,  5,  6,  6,  7,  4, // Front
         8,  9, 10, 10, 11,  8, // Left
        12, 13, 14, 14, 15, 12, // Right
        16, 17, 18, 18, 19, 16, // Bottom
        20, 21, 22, 22, 23, 20  // Top
    };

    try {
        m_gridShader = std::make_unique<Shader>(this, "shaders/grid_vert.glsl", "shaders/grid_frag.glsl");
        m_phongShader = std::make_unique<Shader>(this, "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
        m_outlineShader = std::make_unique<Shader>(this, "shaders/outline_vert.glsl", "shaders/outline_frag.glsl");

        m_gridMesh = std::make_unique<Mesh>(this, grid_vertices);
        m_cubeMesh = std::make_unique<Mesh>(this, lit_cube_vertices, lit_cube_indices, true);
    }
    catch (const std::exception& e) {
        qCritical() << "Failed to initialize resources in ViewportWidget: " << e.what();
    }

    glGenVertexArrays(1, &m_outlineVAO);
    glGenBuffers(1, &m_outlineVBO);
    glBindVertexArray(m_outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_outlineVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);

    connect(m_animationTimer, &QTimer::timeout, this, QOverload<>::of(&ViewportWidget::update));
    m_animationTimer->start(16);
}

void ViewportWidget::paintGL()
{
    qDebug() << "[MainViewport" << this->objectName() << "] Painting scene with" << m_scene->getRegistry().view<entt::entity>().size() << "total entities.";


    IntersectionSystem::update(m_scene);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_scene) return;
    auto& registry = m_scene->getRegistry();
    auto& camera = getCamera();
    const float aspectRatio = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const glm::mat4 viewMatrix = camera.getViewMatrix();
    const glm::mat4 projectionMatrix = camera.getProjectionMatrix(aspectRatio);

    if (m_gridShader && m_gridMesh) {
        m_gridShader->use();
        m_gridShader->setMat4("u_viewMatrix", viewMatrix);
        m_gridShader->setMat4("u_projectionMatrix", projectionMatrix);
        m_gridShader->setVec3("u_cameraPos", camera.getPosition());
        auto gridView = registry.view<const TransformComponent, const GridComponent>();
        gridView.each([this, &camera](const auto& transform, const auto& grid) {
            if (!grid.masterVisible) return;
            m_gridShader->setMat4("u_gridModelMatrix", transform.getTransform());
            float distanceToGrid = glm::length(camera.getPosition() - transform.translation);
            m_gridShader->setFloat("u_distanceToGrid", distanceToGrid);
            for (int i = 0; i < 5; ++i) { m_gridShader->setBool(("u_levelVisible[" + std::to_string(i) + "]").c_str(), grid.levelVisible[i]); }
            const int numLevelsToSend = static_cast<int>(grid.levels.size());
            m_gridShader->setInt("u_numLevels", numLevelsToSend);
            for (int i = 0; i < numLevelsToSend; ++i) {
                std::string base = "u_levels[" + std::to_string(i) + "].";
                m_gridShader->setFloat((base + "spacing").c_str(), grid.levels[i].spacing);
                m_gridShader->setVec3((base + "color").c_str(), grid.levels[i].color);
                m_gridShader->setFloat((base + "fadeInCameraDistanceEnd").c_str(), grid.levels[i].fadeInCameraDistanceEnd);
                m_gridShader->setFloat((base + "fadeInCameraDistanceStart").c_str(), grid.levels[i].fadeInCameraDistanceStart);
            }
            m_gridShader->setBool("u_isDotted", grid.isDotted);
            m_gridShader->setBool("u_showAxes", grid.showAxes);
            m_gridShader->setVec3("u_xAxisColor", grid.xAxisColor);
            m_gridShader->setVec3("u_zAxisColor", grid.zAxisColor);
            m_gridShader->setFloat("u_axisLineWidthPixels", grid.axisLineWidthPixels);
            m_gridShader->setFloat("u_baseLineWidthPixels", grid.baseLineWidthPixels);
            m_gridMesh->draw();
            });
    }

    if (m_phongShader && m_cubeMesh) {
        m_phongShader->use();
        m_phongShader->setMat4("view", viewMatrix);
        m_phongShader->setMat4("projection", projectionMatrix);
        m_phongShader->setVec3("lightPos", camera.getPosition());
        m_phongShader->setVec3("viewPos", camera.getPosition());
        m_phongShader->setVec3("objectColor", glm::vec3(0.8f, 0.8f, 0.8f));
        m_phongShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        auto meshView = registry.view<const TransformComponent, const RenderableMeshComponent>();
        meshView.each([this](const auto& transform, const auto& renderable) {
            m_phongShader->setMat4("model", transform.getTransform());
            m_cubeMesh->draw();
            });
    }

    if (m_outlineShader && m_outlineVAO != 0) {
        // --- THIS IS THE FIX ---
        // Disable the depth test so the outline always draws on top of the grid.
        // Also increase the line width to make it more visible.
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.5f);

        m_outlineShader->use();
        m_outlineShader->setMat4("u_view", viewMatrix);
        m_outlineShader->setMat4("u_projection", projectionMatrix);
        auto sliceableView = registry.view<const IntersectionComponent>();
        sliceableView.each([this](const auto& intersection) {
            const auto& result = intersection.result;
            if (result.isIntersecting && !result.worldOutlinePoints3D.empty()) {
                glBindVertexArray(m_outlineVAO);
                glBindBuffer(GL_ARRAY_BUFFER, m_outlineVBO);

                // Draw the main outline (yellow)
                m_outlineShader->setVec3("u_color", glm::vec3(1.0f, 1.0f, 0.0f));
                glBufferData(GL_ARRAY_BUFFER, result.worldOutlinePoints3D.size() * sizeof(glm::vec3), result.worldOutlinePoints3D.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_LINE_LOOP, 0, result.worldOutlinePoints3D.size());

                // (Caliper drawing logic would go here)

                glBindVertexArray(0);
            }
            });

        // Re-enable depth testing for the rest of the scene.
        glEnable(GL_DEPTH_TEST);
        glLineWidth(1.0f); // Reset line width
    }
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    bool isPanning = (event->buttons() & Qt::MiddleButton);
    bool isOrbiting = (event->buttons() & Qt::LeftButton);

    if (isOrbiting || isPanning) {
        getCamera().processMouseMovement(dx, -dy, isPanning);
    }
    m_lastMousePos = event->pos();
    QOpenGLWidget::mouseMoveEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    getCamera().processMouseScroll(event->angleDelta().y() / 120.0f);
    QOpenGLWidget::wheelEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_P) {
        getCamera().toggleProjection();
    }
    else if (event->key() == Qt::Key_R) {
        getCamera().setToKnownGoodView();
    }
    QOpenGLWidget::keyPressEvent(event);
}
