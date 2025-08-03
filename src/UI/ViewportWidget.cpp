#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLVersionFunctionsFactory>
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
#include <QMessageBox>
#include <QLabel>

#include "ViewportWidget.hpp"
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "IntersectionSystem.hpp"
#include "DebugHelpers.hpp"
#include "LedTweakDialog.hpp"
#include "FieldSolver.hpp"
#include "BlackBox.hpp"

int ViewportWidget::s_instanceCounter = 0;

static GLuint s_testTextureId = 0;
static bool s_isFirstContextInit = true;

void ViewportWidget::propagateTransforms(entt::registry& r)
{
    auto viewParents = r.view<ParentComponent>(); // cache once

    std::function<void(entt::entity, const glm::mat4&)> dfs =
        [&](entt::entity e, const glm::mat4& parentW)
        {
            glm::mat4 local = r.get<TransformComponent>(e).getTransform();
            glm::mat4 world = parentW * local;

            r.emplace_or_replace<WorldTransformComponent>(e, world);

            // recurse through children of e
            for (auto child : viewParents) {
                if (viewParents.get<ParentComponent>(child).parent == e)
                    dfs(child, world);
            }
        };

    // Process all root entities (those without a ParentComponent)
    for (auto e : r.view<TransformComponent>(entt::exclude<ParentComponent>))
        dfs(e, glm::mat4(1.0f));
}

ViewportWidget::ViewportWidget(Scene* scene, RenderingSystem* renderingSystem, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent),
    m_scene(scene),
    m_cameraEntity(cameraEntity),
    m_renderingSystem(renderingSystem)
{
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    QSurfaceFormat format;
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);

    m_instanceId = s_instanceCounter++;
    qDebug() << "[LIFECYCLE] Constructing ViewportWidget instance:" << m_instanceId;
    setFocusPolicy(Qt::StrongFocus);

    m_statsOverlay = new QLabel(this);
    m_statsOverlay->setStyleSheet("background-color: rgba(44, 49, 58, 0.7);"
        "color: white;"
        "padding: 5px;"
        "border-radius: 3px;");
    m_statsOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_statsOverlay->show();

}

void ViewportWidget::setRenderingSystem(RenderingSystem* system)
{
    m_renderingSystem = system;
}

ViewportWidget::~ViewportWidget() {}

void ViewportWidget::initializeGL()
{
    // ...
    // ADD THIS BLOCK IF YOU DON'T HAVE IT
    m_logger = new QOpenGLDebugLogger(this);
    connect(m_logger, &QOpenGLDebugLogger::messageLogged, this, &ViewportWidget::handleLoggedMessage);
    if (m_logger->initialize()) {
        m_logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
        m_logger->enableMessages();
    }
    else {
        qWarning() << "Failed to initialize OpenGL Debug Logger.";
    }
    // ...
    if (m_renderingSystem) {
        m_renderingSystem->onViewportAdded(this);
    }
    emit glContextReady();
}

void ViewportWidget::resizeGL(int w, int h) {
    // We still need to position the overlay on resize.
    if (m_statsOverlay) {
        m_statsOverlay->move(10, 10);
    }
    // The RenderingSystem will handle FBO resizing automatically in its renderFrame() loop.
}

void ViewportWidget::shutdown()
{
    makeCurrent();

    glDeleteVertexArrays(1, &m_outlineVAO);
    glDeleteBuffers(1, &m_outlineVBO);
    doneCurrent();
}

void ViewportWidget::paintGL()
{
    // The main render call is now in MainWindow.
    // This function's only job is to update the stats overlay.
    if (m_renderingSystem) {
        QString statsText = QString("FPS: %1\nFrame: %2 ms")
            .arg(m_renderingSystem->getFPS(), 0, 'f', 1)
            .arg(m_renderingSystem->getFrameTime(), 0, 'f', 2);
        m_statsOverlay->setText(statsText);
        m_statsOverlay->adjustSize();
    }
}

void ViewportWidget::renderNow()
{
    if (isVisible() && context()) {
        makeCurrent();
        paintGL();

        //! DIAGNOSTIC: Force the GPU to finish all drawing for this viewport
        //! before the CPU is allowed to continue to the next one.
        glFinish();

        context()->swapBuffers(context()->surface());
        doneCurrent();
    }
}

void ViewportWidget::handleLoggedMessage(const QOpenGLDebugMessage& debugMessage)
{
    // Print any message from the OpenGL driver to the console
    qWarning() << "[OpenGL Debug]" << debugMessage.source() << debugMessage.type() << debugMessage.id() << debugMessage.severity() << debugMessage.message();
}

Camera& ViewportWidget::getCamera()
{
    if (!m_scene) {
        qFatal("FATAL: ViewportWidget trying to get camera before Scene is set!");
    }
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


void ViewportWidget::mousePressEvent(QMouseEvent* ev)
{
    m_lastMousePos = ev->pos();
    if (ev->button() == Qt::RightButton) {
        setFocus(Qt::OtherFocusReason);          // grab kb focus
        getCamera().setNavMode(Camera::NavMode::FLY);
        setCursor(Qt::BlankCursor);
    }
    if (ev->button() == Qt::LeftButton)
        IntersectionSystem::selectObjectAt(*m_scene, *this, ev->pos().x(), ev->pos().y());

    update();
    QOpenGLWidget::mousePressEvent(ev);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::RightButton) {
        getCamera().setNavMode(Camera::NavMode::ORBIT);
        unsetCursor();
    }
    QOpenGLWidget::mouseReleaseEvent(ev);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* ev)
{
    int dx = ev->pos().x() - m_lastMousePos.x();
    int dy = ev->pos().y() - m_lastMousePos.y();
    Camera& cam = getCamera();

    if (cam.navMode() == Camera::NavMode::FLY)            cam.freeLook(dx, dy);
    else if (ev->buttons() & Qt::MiddleButton ||
        (ev->buttons() & Qt::LeftButton && ev->modifiers() & Qt::ShiftModifier))
        cam.pan(dx, dy, width(), height());
    else if (ev->buttons() & Qt::LeftButton)              cam.orbit(dx, dy);

    m_lastMousePos = ev->pos();
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    getCamera().dolly(event->angleDelta().y());
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* ev)
{
    float dt = 1.0f / 60.0f;                      // per-frame step
    Camera& cam = getCamera();

    if (cam.navMode() == Camera::NavMode::FLY) {
        switch (ev->key()) {
        case Qt::Key_W: cam.flyMove(Camera::FORWARD, dt); break;
        case Qt::Key_S: cam.flyMove(Camera::BACKWARD, dt); break;
        case Qt::Key_A: cam.flyMove(Camera::LEFT, dt); break;
        case Qt::Key_D: cam.flyMove(Camera::RIGHT, dt); break;
        case Qt::Key_E: cam.flyMove(Camera::UP, dt); break;
        case Qt::Key_Q: cam.flyMove(Camera::DOWN, dt); break;
        }
    }
    else {                                         // orbit mode (old keys)
        float s = 0.05f;
        switch (ev->key()) {
        case Qt::Key_W: cam.move(Camera::FORWARD, s); break;
        case Qt::Key_S: cam.move(Camera::BACKWARD, s); break;
        case Qt::Key_A: cam.move(Camera::LEFT, s); break;
        case Qt::Key_D: cam.move(Camera::RIGHT, s); break;
        case Qt::Key_E: cam.move(Camera::UP, s); break;
        case Qt::Key_Q: cam.move(Camera::DOWN, s); break;
        case Qt::Key_P: cam.toggleProjection();       break;
        }
    }
    update();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;

    if (auto hit = IntersectionSystem::pickPoint(*m_scene, *this,
        ev->pos().x(), ev->pos().y()))
    {
        getCamera().focusOn(*hit,                    // new target
            glm::length(getCamera().getPosition() - *hit));
        update();
    }
}
