#include "ViewportWidget.hpp"
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "components.hpp"
#include "IntersectionSystem.hpp"
#include "DebugHelpers.hpp"

#include <QOpenGLContext>
#include <QOpenGLDebugLogger>
#include <QSurfaceFormat>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QDebug>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

ViewportWidget::ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent),
    m_scene(scene),
    m_cameraEntity(cameraEntity)
{
    QSurfaceFormat format;
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);
    setFocusPolicy(Qt::StrongFocus);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&ViewportWidget::update));
    timer->start(16);
}

ViewportWidget::~ViewportWidget()
{
    // All cleanup is now handled by the explicit shutdown() call from MainWindow.
    //qDebug() << "[LIFETIME] ViewportWidget Destructor ~ViewportWidget() called. Cleanup should already be complete.";
}

void ViewportWidget::shutdown()
{
    makeCurrent();

    if (m_renderingSystem) {
        m_renderingSystem->shutdown(m_scene->getRegistry());
    }

    if (m_outlineVAO != 0) {
        glDeleteVertexArrays(1, &m_outlineVAO);
        glDeleteBuffers(1, &m_outlineVBO);
    }

    doneCurrent();
}


void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    m_debugLogger = std::make_unique<QOpenGLDebugLogger>(this);
    if (m_debugLogger->initialize()) {
        connect(m_debugLogger.get(), &QOpenGLDebugLogger::messageLogged, this, [](const QOpenGLDebugMessage& debugMessage) {
            //qDebug() << debugMessage;
            });
        m_debugLogger->startLogging();
    }
    else {
        qWarning("Could not initialize OpenGL debug logger.");
    }
    m_renderingSystem = std::make_unique<RenderingSystem>(this);
    m_renderingSystem->initialize();

    try {
        m_outlineShader = std::make_unique<Shader>(this, "D:/RoboticsSoftware/shaders/outline_vert.glsl", "D:/RoboticsSoftware/shaders/outline_frag.glsl");
        // CLEANUP: Removed the success message to reduce console spam.
    }
    catch (const std::runtime_error& e) {
        qWarning() << "[ViewportWidget] FAILED to initialize outline shader:" << e.what();
    }

    glGenVertexArrays(1, &m_outlineVAO);
    glGenBuffers(1, &m_outlineVBO);
    glBindVertexArray(m_outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 8, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void ViewportWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_scene || !m_renderingSystem) return;

    auto& registry = m_scene->getRegistry();
    auto& camera = getCamera();
    float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspect);

    m_renderingSystem->render(registry, view, proj, camera.getPosition());

    if (m_outlineShader)
    {
        auto selectedView = registry.view<SelectedComponent>();
        if (!selectedView.empty())
        {
            for (auto entity : selectedView)
            {
                if (auto* box = registry.try_get<BoundingBoxComponent>(entity))
                {
                    DebugHelpers::drawAABBOutline(*this, *m_outlineShader, *box, m_outlineVAO, m_outlineVBO, view, proj);
                }
            }
        }
    }
}

Camera& ViewportWidget::getCamera() {
    auto& registry = m_scene->getRegistry();

    if (!registry.valid(m_cameraEntity)) {
        qFatal("FATAL: ViewportWidget camera entity handle: %u is INVALID.", static_cast<uint32_t>(m_cameraEntity));
    }

    auto* cameraComp = registry.try_get<CameraComponent>(m_cameraEntity);
    if (!cameraComp) {
        qFatal("FATAL: Entity %u IS VALID, but does NOT have a CameraComponent.", static_cast<uint32_t>(m_cameraEntity));
    }

    return cameraComp->camera;
}

void ViewportWidget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    if (event->button() == Qt::LeftButton) {
        IntersectionSystem::selectObjectAt(*m_scene, *this, event->pos().x(), event->pos().y());
    }
    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    bool pan = (event->buttons() & Qt::MiddleButton) || (event->buttons() & Qt::LeftButton && event->modifiers() & Qt::ShiftModifier);
    bool orbit = (event->buttons() & Qt::LeftButton) && !(event->modifiers() & Qt::ShiftModifier);
    if (pan || orbit) getCamera().processMouseMovement(dx, -dy, pan);
    m_lastMousePos = event->pos();
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    getCamera().processMouseScroll(event->angleDelta().y() / 120.0f);
    update();
}
