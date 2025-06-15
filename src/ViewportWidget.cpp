#include "ViewportWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"
#include "IntersectionSystem.hpp"
#include "DebugHelpers.hpp" // <-- INCLUDE THE NEW HEADER

#include <QOpenGLContext>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDebug>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <QMatrix4x4>
#include <utility> // For std::move
#include <iomanip>

// All helper functions are now removed from here and are in DebugHelpers.hpp

// Recursive helper function for Forward Kinematics
glm::mat4 calculateMainWorldTransform(entt::entity entity, entt::registry& registry, int depth = 0)
{
    QString indent = QString(depth * 4, ' ');
    auto* tagPtr = registry.try_get<TagComponent>(entity);
    QString tag = tagPtr ? QString::fromStdString(tagPtr->tag) : "NO_TAG";
    qDebug().noquote() << indent << "[MainFK] Calculating for" << entity << "tagged as" << tag;

    auto& transformComp = registry.get<TransformComponent>(entity);
    glm::mat4 localTransform = transformComp.getTransform();

    glm::mat4 finalTransform = localTransform;

    if (registry.all_of<ParentComponent>(entity)) {
        auto& parentComp = registry.get<ParentComponent>(entity);
        if (registry.valid(parentComp.parent)) {
            qDebug().noquote() << indent << "  -> Found parent" << parentComp.parent << ". Recursing...";
            glm::mat4 parentWorldTransform = calculateMainWorldTransform(parentComp.parent, registry, depth + 1);
            finalTransform = parentWorldTransform * localTransform;
        }
    }
    return finalTransform;
}

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
        if (m_gridVAO) glDeleteVertexArrays(1, &m_gridVAO);
        if (m_gridVBO) glDeleteBuffers(1, &m_gridVBO);
        if (m_cubeVAO) glDeleteVertexArrays(1, &m_cubeVAO);
        if (m_cubeVBO) glDeleteBuffers(1, &m_cubeVBO);
        if (m_cubeEBO) glDeleteBuffers(1, &m_cubeEBO);
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

    const float halfSize = 1000.0f;
    const std::vector<float> grid_vertices = { -halfSize, 0.0f, -halfSize,  halfSize, 0.0f, -halfSize,  halfSize, 0.0f,  halfSize, halfSize, 0.0f,  halfSize, -halfSize, 0.0f,  halfSize, -halfSize, 0.0f, -halfSize };

    try {
        m_gridShader = std::make_unique<Shader>(this, "shaders/grid_vert.glsl", "shaders/grid_frag.glsl");
        m_phongShader = std::make_unique<Shader>(this, "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
        m_outlineShader = std::make_unique<Shader>(this, "shaders/outline_vert.glsl", "shaders/outline_frag.glsl");

        m_gridMesh = std::make_unique<Mesh>(grid_vertices);
        m_cubeMesh = std::make_unique<Mesh>(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());

        glGenVertexArrays(1, &m_gridVAO);
        glGenBuffers(1, &m_gridVBO);
        glBindVertexArray(m_gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
        glBufferData(GL_ARRAY_BUFFER, grid_vertices.size() * sizeof(float), grid_vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        glGenVertexArrays(1, &m_cubeVAO);
        glGenBuffers(1, &m_cubeVBO);
        glGenBuffers(1, &m_cubeEBO);
        glBindVertexArray(m_cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
        const auto& cubeVerts = m_cubeMesh->vertices();
        glBufferData(GL_ARRAY_BUFFER, cubeVerts.size() * sizeof(float), cubeVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
        const auto& cubeInd = m_cubeMesh->indices();
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cubeInd.size() * sizeof(unsigned int), cubeInd.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    catch (const std::exception& e) {
        qCritical() << "[MainViewport] CRITICAL: Failed to initialize resources:" << e.what();
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
    IntersectionSystem::update(m_scene);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_scene) return;
    auto& registry = m_scene->getRegistry();
    auto& camera = getCamera();
    const float aspectRatio = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const glm::mat4 viewMatrix = camera.getViewMatrix();
    const glm::mat4 projectionMatrix = camera.getProjectionMatrix(aspectRatio);

    // --- Draw Grid ---
    if (m_gridShader && m_gridMesh) {
        auto gridView = registry.view<const GridComponent>();
        int gridCount = 0;
        for (auto entity : gridView) { (void)entity; gridCount++; }
        qDebug() << "[MainViewport] Found" << gridCount << "grid entities.";

        m_gridShader->use();
        m_gridShader->setMat4("u_viewMatrix", viewMatrix);
        m_gridShader->setMat4("u_projectionMatrix", projectionMatrix);
        m_gridShader->setVec3("u_cameraPos", camera.getPosition());
        gridView.each([this, &camera, &registry](const auto entity, const auto& grid) {
            if (!grid.masterVisible) return;
            auto& transform = registry.get<TransformComponent>(entity);
            m_gridShader->setMat4("u_gridModelMatrix", transform.getTransform());
            // ... (rest of uniform setting is correct) ...
            glBindVertexArray(m_gridVAO);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(m_gridMesh->vertices().size() / 3));
            glBindVertexArray(0);
            qDebug() << "[MainViewport] Drew grid entity" << entity;
            });
    }

    // --- Draw Renderable Meshes (Robot, etc.) ---
    if (m_phongShader && m_cubeMesh) {
        m_phongShader->use();
        m_phongShader->setMat4("view", viewMatrix);
        m_phongShader->setMat4("projection", projectionMatrix);
        m_phongShader->setVec3("lightPos", camera.getPosition());
        m_phongShader->setVec3("viewPos", camera.getPosition());
        m_phongShader->setVec3("objectColor", glm::vec3(0.8f, 0.8f, 0.8f));
        m_phongShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

        auto meshView = registry.view<const RenderableMeshComponent>();
        int meshCount = 0;
        for (auto entity : meshView) { (void)entity; meshCount++; }
        qDebug() << "[MainViewport] Found" << meshCount << "renderable mesh entities to draw.";

        for (auto entity : meshView) {
            auto* tagPtr = registry.try_get<TagComponent>(entity);
            QString tag = tagPtr ? QString::fromStdString(tagPtr->tag) : "NO_TAG";
            qDebug() << "  [MainViewport] --- Processing entity" << entity << "tagged as" << tag << "---";

            glm::mat4 worldTransform = calculateMainWorldTransform(entity, registry);
            // **FIX**: Corrected function name from printMainMatrix to printMatrix
            printMatrix(worldTransform, "    [MainViewport] Final World Transform for " + tag + ":");
            m_phongShader->setMat4("model", worldTransform);

            glBindVertexArray(m_cubeVAO);
            glDrawElements(GL_TRIANGLES, static_cast<int>(m_cubeMesh->indices().size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }

    // --- Draw Intersection Outlines ---
    if (m_outlineShader && m_outlineVAO != 0) {
        // ... (this logic was correct)
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