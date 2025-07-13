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
    m_instanceId = s_instanceCounter++;
    qDebug() << "[LIFECYCLE] Constructing ViewportWidget instance:" << m_instanceId;
    setFocusPolicy(Qt::StrongFocus);

}

void ViewportWidget::setRenderingSystem(RenderingSystem* system)
{
    m_renderingSystem = system;
}

ViewportWidget::~ViewportWidget() {}

void ViewportWidget::initializeGL() {
    // This function will now print a very distinct block of text to the log.
    qDebug() << "\n\n=====================================================================";
    qDebug() << "============== VIEWPORT INITIALIZEGL() TRIGGERED ==============";
    qDebug() << "=====================================================================";
    qDebug() << " > Widget Address:          " << this;
    qDebug() << " > Context Address:         " << context();
    qDebug() << " > RenderingSystem Address: " << m_renderingSystem;

    if (m_renderingSystem) {
        // We are using the isContextInitialized helper we added previously
        qDebug() << " > Context already known?:    " << m_renderingSystem->isContextInitialized(context());

        initializeOpenGLFunctions();
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(context());

        if (gl) {
            m_renderingSystem->ensureContextIsTracked(this);
            m_renderingSystem->initializeResourcesForContext(gl, m_scene);
        }
        else {
            qCritical() << " > CRITICAL: Failed to get GL functions in initializeGL!";
        }

    }
    else {
        qCritical() << " > CRITICAL: RenderingSystem pointer is NULL!";
    }
    qDebug() << "=====================================================================";
    qDebug() << "============== VIEWPORT INITIALIZEGL() END ==================\n\n";
}

void ViewportWidget::resizeGL(int w, int h) {
    if (!m_renderingSystem) return;

    const int fbW = int(w * devicePixelRatioF());
    const int fbH = int(h * devicePixelRatioF());

    // Call the new, correctly named function, passing 'this' which is a ViewportWidget*.
    m_renderingSystem->onViewportResized(this, this, fbW, fbH);

#ifdef RS_DEBUG_DUMP
    dbg::BlackBox::instance().dumpState("Resize", *m_renderingSystem,
        this, this);
#endif
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
    if (!m_renderingSystem || !m_scene) return;

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(context());
    if (!gl) {
        qCritical("paintGL: Could not get GL functions for current context!");
        return;
    }

    m_renderingSystem->initializeResourcesForContext(gl, m_scene);

    const int fbW = static_cast<int>(width() * devicePixelRatioF());
    const int fbH = static_cast<int>(height() * devicePixelRatioF());

    // Get the single, authoritative deltaTime for this frame from the scene.
    const float frameDeltaTime = m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;

    // Pass it to the renderView function.
    m_renderingSystem->renderView(this, gl, fbW, fbH, frameDeltaTime);
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
/*

void ViewportWidget::updateAnimations(entt::registry& registry, float frameDt)
{
    // A persistent timer, just like in your example
    static float timer = 0.f;
    timer += frameDt;

    // --- Pulsing Spline Logic ---
    float pulseSpeed = 3.0f; // Controls how fast the pulse is. Higher is faster.

    // A sine wave oscillates smoothly between -1.0 and 1.0.
    // We map it to a 0.0 to 1.0 range to use as a brightness multiplier.
    float brightness = (sin(timer * pulseSpeed) + 1.0f) / 2.0f;

    // To prevent the glow from disappearing completely, we'll set a minimum brightness.
    float minBrightness = 0.1f;
    float finalBrightness = minBrightness + (1.0f - minBrightness) * brightness;

    // Find all entities that have BOTH a PulsingSplineTag and a SplineComponent
    auto view = registry.view<PulsingSplineTag, SplineComponent>();
    for (auto entity : view)
    {
        // Get the spline component for this entity
        auto& spline = view.get<SplineComponent>(entity);

        // Modulate the alpha of the glowColour to make it pulse.
        // We set the base alpha to 1.0 and multiply by our pulsing brightness.
        spline.glowColour.a = 1.0f * finalBrightness;
    }
}
*/

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



