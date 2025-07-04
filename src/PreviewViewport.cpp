#include "PreviewViewport.hpp"
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "components.hpp"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>

PreviewViewport::PreviewViewport(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);
    setFocusPolicy(Qt::StrongFocus);
}

// --- FIX: Destructor is now empty ---
// All GPU resource cleanup is handled by RenderingSystem::shutdown(),
// so this destructor should do nothing. This prevents the crash on exit.
PreviewViewport::~PreviewViewport()
{
}

void PreviewViewport::setRenderingSystem(RenderingSystem* system, Scene* scene, entt::entity camera)
{
    m_renderingSystem = system;
    m_scene = scene;
    m_cameraEntity = camera;
}

void PreviewViewport::updateRobot(const RobotDescription& description)
{
    qWarning() << "PreviewViewport::updateRobot is deprecated and has no effect.";
}

void PreviewViewport::setAnimationSpeed(int sliderValue)
{
    qWarning() << "PreviewViewport::setAnimationSpeed is deprecated and has no effect.";
}

void PreviewViewport::initializeGL()
{
    initializeOpenGLFunctions();
}

void PreviewViewport::paintGL()
{
    if (!m_renderingSystem || !m_scene || !m_renderingSystem->isInitialized() || !m_scene->getRegistry().valid(m_cameraEntity)) {
        glClearColor(0.15f, 0.16f, 0.18f, 1.0f); // A slightly different color to confirm it's this path
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const int fbW = static_cast<int>(width() * devicePixelRatioF());
    const int fbH = static_cast<int>(height() * devicePixelRatioF());

    m_renderingSystem->renderView(this, m_scene->getRegistry(), m_cameraEntity, fbW, fbH);
}

void PreviewViewport::resizeGL(int w, int h)
{
}

void PreviewViewport::mousePressEvent(QMouseEvent* ev)
{
    m_lastMousePos = ev->pos();
    setFocus();
}

void PreviewViewport::mouseReleaseEvent(QMouseEvent* ev)
{
}

void PreviewViewport::mouseMoveEvent(QMouseEvent* ev)
{
    if (!m_scene || !m_scene->getRegistry().valid(m_cameraEntity)) return;

    int dx = ev->pos().x() - m_lastMousePos.x();
    int dy = ev->pos().y() - m_lastMousePos.y();

    auto& camera = m_scene->getRegistry().get<CameraComponent>(m_cameraEntity).camera;

    if (ev->buttons() & Qt::LeftButton) {
        camera.orbit(dx, dy);
    }
    else if (ev->buttons() & Qt::MiddleButton) {
        camera.pan(dx, dy, width(), height());
    }

    m_lastMousePos = ev->pos();
    update();
}

void PreviewViewport::wheelEvent(QWheelEvent* ev)
{
    if (!m_scene || !m_scene->getRegistry().valid(m_cameraEntity)) return;

    auto& camera = m_scene->getRegistry().get<CameraComponent>(m_cameraEntity).camera;
    camera.dolly(ev->angleDelta().y());
    update();
}
