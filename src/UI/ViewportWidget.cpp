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
#include <optional>
#include <limits>

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
struct CpuRay { glm::vec3 origin; glm::vec3 dir; };

static CpuRay makeRayFromScreen(int px, int py, int vpW, int vpH, const Camera& cam)
{
    float x = (2.0f * float(px) / float(vpW)) - 1.0f;
    float y = 1.0f - (2.0f * float(py) / float(vpH)); // NDC with Y up

    glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
    glm::mat4 V = cam.getViewMatrix();
    glm::mat4 invVP = glm::inverse(P * V);

    glm::vec4 nearW = invVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 farW = invVP * glm::vec4(x, y, 1.0f, 1.0f);
    nearW /= nearW.w; farW /= farW.w;

    CpuRay r;
    r.origin = glm::vec3(nearW);
    r.dir = glm::normalize(glm::vec3(farW - nearW));
    return r;
}

static bool intersectRayAABB(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& bmin, const glm::vec3& bmax,
    float& tEnter, float& tExit)
{
    glm::vec3 invD(1.0f / rd.x, 1.0f / rd.y, 1.0f / rd.z);

    glm::vec3 t0 = (bmin - ro) * invD;
    glm::vec3 t1 = (bmax - ro) * invD;

    glm::vec3 tminVec(std::min(t0.x, t1.x),
        std::min(t0.y, t1.y),
        std::min(t0.z, t1.z));

    glm::vec3 tmaxVec(std::max(t0.x, t1.x),
        std::max(t0.y, t1.y),
        std::max(t0.z, t1.z));

    tEnter = std::max(std::max(tminVec.x, tminVec.y), tminVec.z);
    tExit = std::min(std::min(tmaxVec.x, tmaxVec.y), tmaxVec.z);

    return (tExit >= tEnter) && (tExit >= 0.0f);
}

struct CpuPickHit {
    entt::entity entity{ entt::null };
    glm::vec3    worldPos{ 0 };
    float        worldT{ std::numeric_limits<float>::max() };
};

static std::optional<CpuPickHit>
cpuPickAABB(Scene& scene, const Camera& cam, int px, int py, int vpW, int vpH)
{
    CpuRay ray = makeRayFromScreen(px, py, vpW, vpH, cam);
    if (!std::isfinite(ray.dir.x) || !std::isfinite(ray.dir.y) || !std::isfinite(ray.dir.z)) {
        qWarning() << "[Pick] bad ray dir!";
        return std::nullopt;
    }
    auto& reg = scene.getRegistry();
    auto view = reg.view<TransformComponent, RenderableMeshComponent>();
    qDebug() << "[Pick] entities in view:" << int(view.size_hint())
        << " px,py=" << px << py << " vp=" << vpW << vpH;

    CpuPickHit best;

    for (auto [e, xform, mesh] : view.each()) {
        glm::mat4 M = xform.getTransform();
        glm::mat4 invM = glm::inverse(M);

        glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
        glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));

        float t0, t1;
        if (!intersectRayAABB(roL, rdL, mesh.aabbMin, mesh.aabbMax, t0, t1)) continue;
        if (t0 < 0.0f) t0 = t1;

        glm::vec3 localHit = roL + rdL * t0;
        glm::vec3 worldHit = glm::vec3(M * glm::vec4(localHit, 1.0f));
        float dist = glm::length(worldHit - ray.origin);

        if (dist < best.worldT) {
            best.entity = e;
            best.worldPos = worldHit;
            best.worldT = dist;
        }
    }

    if (best.entity != entt::null) return best;
    return std::nullopt;
}

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
        //glFinish();

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
        setFocus(Qt::OtherFocusReason);
        getCamera().setNavMode(Camera::NavMode::FLY);
        setCursor(Qt::BlankCursor);
    }
    if (ev->button() == Qt::LeftButton) {
        m_clickStartPos = ev->pos();   // <-- remember down position
        // DO NOT call IntersectionSystem here anymore
    }

    update();
    QOpenGLWidget::mousePressEvent(ev);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::RightButton) {
        getCamera().setNavMode(Camera::NavMode::ORBIT);
        unsetCursor();
    }

    if (ev->button() == Qt::LeftButton) {
        if (m_suppressClickThisRelease) {
            m_suppressClickThisRelease = false;
            return; // Suppress click after a double-click
        }

        const int delta = (ev->pos() - m_clickStartPos).manhattanLength();
        if (delta < m_ClickSlop) { // It's a click, not a drag
            auto& reg = m_scene->getRegistry();
            const bool isCtrlPressed = ev->modifiers() & Qt::ControlModifier;

            // Perform the raycast
            auto hit = cpuPickAABB(*m_scene, getCamera(), ev->pos().x(), ev->pos().y(), width(), height());

            if (hit) {
                // We clicked on an object
                entt::entity hitEntity = hit->entity;
                if (isCtrlPressed) {
                    // CTRL is pressed: Toggle selection
                    if (reg.all_of<SelectedComponent>(hitEntity)) {
                        reg.remove<SelectedComponent>(hitEntity);
                    }
                    else {
                        reg.emplace<SelectedComponent>(hitEntity);
                    }
                }
                else {
                    // CTRL is NOT pressed: Standard selection
                    // Clear previous selection first
                    for (auto eSel : reg.view<SelectedComponent>()) {
                        reg.remove<SelectedComponent>(eSel);
                    }
                    // Select the new entity
                    reg.emplace<SelectedComponent>(hitEntity);
                }
                emit selectionChanged(hitEntity);
            }
            else {
                // We clicked on empty space: Deselect all
                if (!isCtrlPressed) { // Only deselect all if Ctrl isn't held
                    for (auto eSel : reg.view<SelectedComponent>()) {
                        reg.remove<SelectedComponent>(eSel);
                    }
                    emit selectionChanged(entt::null);
                }
            }
            update(); // Trigger a repaint to show the change
        }
    }

    QOpenGLWidget::mouseReleaseEvent(ev);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* ev)
{
    int dx = ev->pos().x() - m_lastMousePos.x();
    int dy = ev->pos().y() - m_lastMousePos.y();
    Camera& cam = getCamera();

    // If user moved more than slop, this isn't a click
    if (m_maybeClick && (ev->pos() - m_pressPos).manhattanLength() > m_ClickSlop) {
        m_maybeClick = false;
    }

    if (cam.navMode() == Camera::NavMode::FLY)            cam.freeLook(dx, dy);
    else if (ev->buttons() & Qt::MiddleButton ||
        (ev->buttons() & Qt::LeftButton && ev->modifiers() & Qt::ShiftModifier))
        cam.pan(dx, dy, width(), height());
    else if (ev->buttons() & Qt::LeftButton)              cam.orbit(dx, dy);  // unchanged

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

    Camera& cam = getCamera();
    if (auto hit = cpuPickAABB(*m_scene, cam, ev->pos().x(), ev->pos().y(), width(), height())) {
        float keepDist = glm::length(cam.getPosition() - hit->worldPos);
        cam.focusOn(hit->worldPos, keepDist);
        update();
    }

    // Prevent the following release from being treated as a single click
    m_suppressClickThisRelease = true;
}