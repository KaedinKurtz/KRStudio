#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLDebugLogger>
#include <QSurfaceFormat>
#include <QTimer>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGuiApplication>
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
#include "RayPick.hpp"   // GATE 3.1: hardened ray-triangle pick (krs::pick)
#include "SelectionService.hpp"   // SELECTION-HIGHLIGHTS: face-level hover/commit (krs::sel)
#include "Shader.hpp"
#include "components.hpp"
#include "IntersectionSystem.hpp"
#include "DebugHelpers.hpp"
#include "LedTweakDialog.hpp"
#include "FieldSolver.hpp"
#include "BlackBox.hpp"
#include "GizmoSystem.hpp"
#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

int ViewportWidget::s_instanceCounter = 0;

static GLuint s_testTextureId = 0;
static bool s_isFirstContextInit = true;

// ---- Local ray builder (no dependency on IntersectionSystem internals) ----
static CpuRay makeCpuRay(const Camera& cam, int px, int py, int vpW, int vpH) {
    float x = (2.0f * float(px) / float(vpW)) - 1.0f;
    float y = 1.0f - (2.0f * float(py) / float(vpH)); // flip Y
    glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
    glm::mat4 V = cam.getViewMatrix();
    glm::mat4 invVP = glm::inverse(P * V);
    glm::vec4 nW = invVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 fW = invVP * glm::vec4(x, y, 1.0f, 1.0f);
    nW /= nW.w; fW /= fW.w;
    CpuRay r; r.origin = glm::vec3(nW); r.dir = glm::normalize(glm::vec3(fW - nW)); return r;
}

// ---- Minimal math helpers (dup of GizmoSystem�s internal ones) ----
static bool rayPlane(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& O, const glm::vec3& N,
    float& tOut, glm::vec3& H) {
    float denom = glm::dot(rd, N);
    if (std::abs(denom) < 1e-8f) return false;
    float t = glm::dot(O - ro, N) / denom;
    if (t < 0.0f) return false;
    tOut = t; H = ro + rd * t; return true;
}

static void closestRayLine(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& lo, const glm::vec3& ld,
    float& tRayOut, float& tLineOut) {
    glm::vec3 r = rd; glm::vec3 l = glm::normalize(ld);
    glm::vec3 w0 = ro - lo;
    float a = glm::dot(r, r);
    float b = glm::dot(r, l);
    float c = 1.0f;
    float d = glm::dot(r, w0);
    float e = glm::dot(l, w0);
    float denom = a * c - b * b;
    float u = 0.0f, v = 0.0f;
    if (std::abs(denom) > 1e-8f) { u = (b * e - c * d) / denom; v = (a * e - b * d) / denom; }
    else { u = 0.0f; v = e; }
    if (u < 0.0f) { u = 0.0f; v = e; }
    tRayOut = u;
    tLineOut = v;
}

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
    // Hardened pick (GATE 3.1): exact ray-TRIANGLE over each mesh via the krs::pick SSOT, instead of
    // the coarse ray-AABB that over-selects whenever the ray merely clips a bounding box (and CAD
    // AABBs are frequently degenerate/zero). Returns the nearest TRUE surface hit along the ray.
    const glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
    const glm::mat4 V = cam.getViewMatrix();
    const krs::pick::Ray ray = krs::pick::makeRayFromScreen(P, V, px, py, vpW, vpH);
    if (!std::isfinite(ray.dir.x) || !std::isfinite(ray.dir.y) || !std::isfinite(ray.dir.z)) {
        qWarning() << "[Pick] bad ray dir!";
        return std::nullopt;
    }
    const auto hit = krs::pick::pickMesh(scene.getRegistry(), ray);
    if (!hit) return std::nullopt;
    CpuPickHit best;
    best.entity = hit->entity; best.worldPos = hit->worldPos; best.worldT = hit->t;
    return best;
}

// SELECTION-HIGHLIGHTS: resolve the pixel to a sub-feature (krs::sel::pick, the gated
// backend) and update the ctx SelectionState the SelectionHighlightPass renders from.
// HOVER on free mouse-move; COMMIT (accumulating set) on a left-click. The on-screen
// highlight is OPERATOR-VISUAL-CONFIRM; the resolved identity is gated.
static void featureHover(Scene& scene, const Camera& cam, int px, int py, int vpW, int vpH)
{
    auto& reg = scene.getRegistry();
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st || !st->enabled) return;
    const glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
    const glm::mat4 V = cam.getViewMatrix();
    const krs::pick::Ray ray = krs::pick::makeRayFromScreen(P, V, px, py, vpW, vpH);
    if (!std::isfinite(ray.dir.x)) return;
    krs::sel::updateHover(*st, reg, ray);
}

static void featureCommit(Scene& scene, const Camera& cam, int px, int py, int vpW, int vpH, bool additive)
{
    auto& reg = scene.getRegistry();
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st || !st->enabled) return;
    const glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
    const glm::mat4 V = cam.getViewMatrix();
    const krs::pick::Ray ray = krs::pick::makeRayFromScreen(P, V, px, py, vpW, vpH);
    if (!std::isfinite(ray.dir.x)) return;
    krs::sel::commitSelection(*st, reg, ray, additive);
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
#ifdef KRS_GL_DEBUG
    // Debug contexts serialize the driver and tank performance — opt-in only.
    QSurfaceFormat format;
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);
#endif

    m_instanceId = s_instanceCounter++;
    qDebug() << "[LIFECYCLE] Constructing ViewportWidget instance:" << m_instanceId;
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true); // asset-browser drag-and-drop spawning
    // Deliver mouseMoveEvent on FREE movement (no button held) -- required for the
    // sub-feature hover highlight (and gizmo hover) to track the cursor live.
    setMouseTracking(true);

    // Smooth fly-camera integration (60 Hz, eased velocity).
    m_flyTimer = new QTimer(this);
    m_flyTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flyTimer, &QTimer::timeout, this, [this]() { tickFlyCamera(); });
    m_flyTimer->start(16);
    m_flyClock.start();

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
#ifdef KRS_GL_DEBUG
    // Synchronous GL debug logging stalls the pipeline on every GL call — opt-in only.
    m_logger = new QOpenGLDebugLogger(this);
    connect(m_logger, &QOpenGLDebugLogger::messageLogged, this, &ViewportWidget::handleLoggedMessage);
    if (m_logger->initialize()) {
        m_logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
        m_logger->enableMessages();
    }
    else {
        qWarning() << "Failed to initialize OpenGL Debug Logger.";
    }
#endif
    // Engine registration is deferred out of the paint cycle entirely: it
    // runs heavy GL on the engine's own context between events, where no Qt
    // context is current and nothing the RHI compositor relies on is touched.
    QTimer::singleShot(0, this, [this]() {
        if (m_renderingSystem) m_renderingSystem->onViewportAdded(this);
    });
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
    if (!m_renderingSystem) return;

    // Keep the gizmo a constant screen size as the camera moves.
    if (m_gizmo) m_gizmo->updateScreenScale(getCamera());

    // Blit the engine's latest finished frame (shared texture) into our
    // backbuffer. The engine renders on its own context via renderAllViewports.
    m_renderingSystem->presentViewport(this);

    QString statsText = QString("FPS: %1\nFrame: %2 ms")
        .arg(m_renderingSystem->getFPS(), 0, 'f', 1)
        .arg(m_renderingSystem->getFrameTime(), 0, 'f', 2);
    m_statsOverlay->setText(statsText);
    m_statsOverlay->adjustSize();
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
        m_rightPressPos = ev->pos();
        setFocus(Qt::OtherFocusReason);
        getCamera().setNavMode(Camera::NavMode::FLY);
        setCursor(Qt::BlankCursor);
    }

    // Middle-click: re-centre the orbit around whatever is under the cursor
    // (keeps the current viewing distance).
    if (ev->button() == Qt::MiddleButton && m_scene) {
        Camera& cam = getCamera();
        if (auto hit = cpuPickAABB(*m_scene, cam, ev->pos().x(), ev->pos().y(),
                                   width(), height())) {
            cam.focusOn(hit->worldPos, glm::length(cam.getPosition() - hit->worldPos));
            update();
        }
    }

    if (ev->button() == Qt::LeftButton) {
        m_clickStartPos = ev->pos();
        m_maybeClick = true;
        setFocus();                                 // was setFocus(Qt::ClickFocus)

        if (m_gizmo && m_scene) {
            Camera& cam = getCamera();
            CpuRay ray = makeCpuRay(cam, ev->pos().x(), ev->pos().y(), width(), height());

            entt::entity h = m_gizmo->pickHandle(ray);
            if (h != entt::null) {
                auto& reg = m_scene->getRegistry();
                entt::entity target = entt::null;
                for (auto eSel : reg.view<SelectedComponent>()) { target = eSel; break; }
                if (target != entt::null) {
                    glm::vec3 hitPoint = glm::vec3(0);

                    // get gizmo origin/rotation
                    const entt::entity gizRoot = m_gizmo->getRootEntity();
                    const auto& rxf = reg.get<TransformComponent>(gizRoot);
                    const glm::vec3 O = rxf.translation;

                    const auto& gh = reg.get<GizmoHandleComponent>(h);
                    if (gh.mode == GizmoMode::Rotate) {
                        glm::vec3 axis = (gh.axis == GizmoAxis::X) ? glm::vec3(1, 0, 0)
                            : (gh.axis == GizmoAxis::Y) ? glm::vec3(0, 1, 0)
                            : glm::vec3(0, 0, 1);
                        glm::vec3 N = glm::normalize(glm::mat3_cast(rxf.rotation) * axis);
                        float t; glm::vec3 H;
                        if (rayPlane(ray.origin, ray.dir, O, N, t, H)) hitPoint = H;
                        else hitPoint = O;
                    }
                    else if (gh.mode == GizmoMode::Translate || gh.mode == GizmoMode::Scale) {
                        if (gh.axis == GizmoAxis::X || gh.axis == GizmoAxis::Y || gh.axis == GizmoAxis::Z) {
                            glm::vec3 axis = (gh.axis == GizmoAxis::X) ? glm::vec3(1, 0, 0)
                                : (gh.axis == GizmoAxis::Y) ? glm::vec3(0, 1, 0)
                                : glm::vec3(0, 0, 1);
                            glm::vec3 A = glm::normalize(glm::mat3_cast(rxf.rotation) * axis);
                            float tRay, tLine; closestRayLine(ray.origin, ray.dir, O, A, tRay, tLine);
                            hitPoint = O + A * tLine;
                        }
                        else {
                            glm::vec3 N = (gh.axis == GizmoAxis::XY) ? glm::vec3(0, 0, 1)
                                : (gh.axis == GizmoAxis::YZ) ? glm::vec3(1, 0, 0)
                                : glm::vec3(0, 1, 0);
                            N = glm::normalize(glm::mat3_cast(rxf.rotation) * N);
                            float t; glm::vec3 H;
                            if (rayPlane(ray.origin, ray.dir, O, N, t, H)) hitPoint = H;
                            else hitPoint = O;
                        }
                    }

                    m_activeGizmo = h;
                    m_gizmoDragging = true;
                    m_suppressClickThisRelease = true;
                    m_gizmo->startDrag(m_activeGizmo, target, hitPoint, ray.dir);
                }
            }
        }
    }

    update();
    QOpenGLWidget::mousePressEvent(ev);
}


void ViewportWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::RightButton) {
        getCamera().setNavMode(Camera::NavMode::ORBIT);
        unsetCursor();

        // Right-CLICK (no fly-drag): scene context menu at the cursor.
        if ((ev->pos() - m_rightPressPos).manhattanLength() < m_ClickSlop && m_scene) {
            glm::vec3 worldPos(0.0f);
            entt::entity hitEntity = entt::null;
            if (auto hit = cpuPickAABB(*m_scene, getCamera(), ev->pos().x(), ev->pos().y(),
                                       width(), height())) {
                worldPos = hit->worldPos;
                hitEntity = hit->entity;
            } else {
                CpuRay ray = makeCpuRay(getCamera(), ev->pos().x(), ev->pos().y(),
                                        width(), height());
                float t;
                glm::vec3 H;
                if (rayPlane(ray.origin, ray.dir, glm::vec3(0), glm::vec3(0, 1, 0), t, H))
                    worldPos = H;
            }
            emit contextMenuRequested(ev->globalPosition().toPoint(), worldPos, hitEntity);
        }
    }

    if (ev->button() == Qt::LeftButton) {

        if (m_gizmoDragging) {
            m_gizmoDragging = false;
            if (m_gizmo) m_gizmo->endDrag();       // clears Active tags
            unsetCursor();
            update();
            // Don't early-return here; we still allow "click vs drag" logic below
        }

        if (m_gizmoDragging && m_gizmo) {
            m_gizmo->endDrag();
            m_gizmoDragging = false;
            m_activeGizmo = entt::null;
            m_suppressClickThisRelease = true;
            update();
            QOpenGLWidget::mouseReleaseEvent(ev);
            return;
        }



        if (m_suppressClickThisRelease) {
            m_suppressClickThisRelease = false;
            QOpenGLWidget::mouseReleaseEvent(ev);
            return;
        }

        // --- existing selection click ---
        const int delta = (ev->pos() - m_clickStartPos).manhattanLength();
        if (delta < m_ClickSlop) {
            auto& reg = m_scene->getRegistry();
            const bool isShiftPressed = ev->modifiers() & Qt::ShiftModifier;

            auto hit = cpuPickAABB(*m_scene, getCamera(), ev->pos().x(), ev->pos().y(), width(), height());

            if (!isShiftPressed) {
                for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
            }
            if (hit) {
                entt::entity e = hit->entity;
                if (reg.all_of<SelectedComponent>(e)) {
                    if (isShiftPressed) reg.remove<SelectedComponent>(e);
                }
                else {
                    reg.emplace<SelectedComponent>(e);
                }
            }

            // SUB-FEATURE SELECT: commit the clicked face into the accumulating set
            // (Shift = additive multi-select; plain click replaces). The highlight is
            // OPERATOR-VISUAL-CONFIRM; the resolved identity is gated.
            featureCommit(*m_scene, getCamera(), ev->pos().x(), ev->pos().y(),
                          width(), height(), isShiftPressed);

            QVector<entt::entity> currentSelection;
            for (auto eSel : reg.view<SelectedComponent>()) currentSelection.push_back(eSel);
            emit selectionChanged(currentSelection, getCamera());
            update();
        }
    }

    QOpenGLWidget::mouseReleaseEvent(ev);
}


void ViewportWidget::mouseMoveEvent(QMouseEvent* ev)
{
    int dx = ev->pos().x() - m_lastMousePos.x();
    int dy = ev->pos().y() - m_lastMousePos.y();
    Camera& cam = getCamera();

    if (m_maybeClick && (ev->pos() - m_clickStartPos).manhattanLength() > m_ClickSlop)
        m_maybeClick = false;

    // ----- DRAG GIZMO -----
    if (m_gizmoDragging && m_gizmo) {
        CpuRay ray = makeCpuRay(cam, ev->pos().x(), ev->pos().y(), width(), height());
        m_gizmo->updateDrag(ray, cam);

        auto& reg = m_scene->getRegistry();
        QVector<entt::entity> currentSelection;
        for (auto eSel : reg.view<SelectedComponent>()) currentSelection.push_back(eSel);
        emit selectionChanged(currentSelection, cam);

        // Suppress camera motion while dragging
        m_lastMousePos = ev->pos();
        update();
        return;
    }

    // ----- HOVER (only when not dragging) -----
    if (m_gizmo) {
        CpuRay ray = makeCpuRay(cam, ev->pos().x(), ev->pos().y(), width(), height());
        entt::entity newHover = m_gizmo->pickHandle(ray);
        if (newHover != m_hoverGizmo) {
            m_hoverGizmo = newHover;
            m_gizmo->setHoveredHandle(m_hoverGizmo);
        }
    }

    // ----- SUB-FEATURE HOVER HIGHLIGHT (free mouse-move only) -----
    if (m_scene && ev->buttons() == Qt::NoButton)
        featureHover(*m_scene, cam, ev->pos().x(), ev->pos().y(), width(), height());

    // ----- CAMERA NAV -----
    if (cam.navMode() == Camera::NavMode::FLY)            cam.freeLook(dx, dy);
    else if ((ev->buttons() & Qt::MiddleButton) ||
        ((ev->buttons() & Qt::LeftButton) && (ev->modifiers() & Qt::ShiftModifier)))
        cam.pan(dx, dy, width(), height());
    else if ((ev->buttons() & Qt::LeftButton) && !m_gizmoDragging)
        cam.orbit(dx, dy);

    m_lastMousePos = ev->pos();
    update();
}


void ViewportWidget::wheelEvent(QWheelEvent* event) {
    Camera& cam = getCamera();
    if (cam.navMode() == Camera::NavMode::FLY) {
        // While flying the wheel tunes the fly speed instead of dollying.
        const float steps = float(event->angleDelta().y()) / 120.0f;
        m_flySpeed = glm::clamp(m_flySpeed * std::pow(1.2f, steps), 0.2f, 60.0f);
        return;
    }
    cam.dolly(event->angleDelta().y());
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* ev)
{
    Camera& cam = getCamera();

    if (cam.navMode() == Camera::NavMode::FLY) {
        // Movement keys just record state; tickFlyCamera() integrates with
        // eased velocity every frame (autorepeat-driven steps teleport).
        if (!ev->isAutoRepeat()) m_keysDown.insert(ev->key());
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
        case Qt::Key_T: emit gizmoModeRequested(0); break; // Translate
        case Qt::Key_R: emit gizmoModeRequested(1); break; // Rotate
        case Qt::Key_Y: emit gizmoModeRequested(2); break; // Scale
        case Qt::Key_Space:
        {
            static int m = 0;           // local cycle
            m = (m + 1) % 3;
            emit gizmoModeRequested(m);
            break;
        }
        }
    }
    update();
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* ev)
{
    if (!ev->isAutoRepeat()) m_keysDown.remove(ev->key());
    QOpenGLWidget::keyReleaseEvent(ev);
}

void ViewportWidget::tickFlyCamera()
{
    const float dt = glm::clamp(float(m_flyClock.restart()) * 0.001f, 0.0f, 0.05f);
    if (!m_scene || dt <= 0.0f) return;
    Camera& cam = getCamera();

    glm::vec3 target(0.0f);
    if (cam.navMode() == Camera::NavMode::FLY) {
        const glm::vec3 pos = cam.getPosition();
        const glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - pos);
        glm::vec3 right = glm::cross(fwd, glm::vec3(0, 1, 0));
        right = glm::dot(right, right) > 1e-8f ? glm::normalize(right) : glm::vec3(1, 0, 0);
        const glm::vec3 up = glm::cross(right, fwd);

        if (m_keysDown.contains(Qt::Key_W)) target += fwd;
        if (m_keysDown.contains(Qt::Key_S)) target -= fwd;
        if (m_keysDown.contains(Qt::Key_D)) target += right;
        if (m_keysDown.contains(Qt::Key_A)) target -= right;
        if (m_keysDown.contains(Qt::Key_E)) target += up;
        if (m_keysDown.contains(Qt::Key_Q)) target -= up;
        if (glm::dot(target, target) > 1e-8f) {
            float speed = m_flySpeed;
            if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) speed *= 3.0f;
            target = glm::normalize(target) * speed;
        }
    }

    // Critically-damped ease toward the target velocity: ~100 ms ramp.
    const float blend = 1.0f - std::exp(-12.0f * dt);
    m_flyVel = glm::mix(m_flyVel, target, blend);

    if (glm::dot(m_flyVel, m_flyVel) > 1e-6f) {
        const glm::vec3 delta = m_flyVel * dt;
        cam.forceRecalculateView(cam.getPosition() + delta, cam.getFocalPoint() + delta,
                                 cam.getDistance());
        update();
    }
}

void ViewportWidget::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasFormat(QStringLiteral("application/x-krstudio-asset")))
        ev->acceptProposedAction();
}

void ViewportWidget::dragMoveEvent(QDragMoveEvent* ev)
{
    if (ev->mimeData()->hasFormat(QStringLiteral("application/x-krstudio-asset")))
        ev->acceptProposedAction();
}

void ViewportWidget::dropEvent(QDropEvent* ev)
{
    const QMimeData* mime = ev->mimeData();
    if (!mime->hasFormat(QStringLiteral("application/x-krstudio-asset")) || !m_scene) return;
    const QString path = QString::fromUtf8(
        mime->data(QStringLiteral("application/x-krstudio-asset")));
    if (path.isEmpty()) return;

    const QPoint pos = ev->position().toPoint();
    Camera& cam = getCamera();

    // Land on whatever the cursor points at; ground plane as the fallback.
    glm::vec3 worldPos(0.0f, 0.5f, 0.0f);
    if (auto hit = cpuPickAABB(*m_scene, cam, pos.x(), pos.y(), width(), height())) {
        worldPos = hit->worldPos;
    } else {
        CpuRay ray = makeCpuRay(cam, pos.x(), pos.y(), width(), height());
        float t;
        glm::vec3 H;
        if (rayPlane(ray.origin, ray.dir, glm::vec3(0), glm::vec3(0, 1, 0), t, H))
            worldPos = H;
    }

    emit assetDropped(path, worldPos);
    ev->acceptProposedAction();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;

    Camera& cam = getCamera();
    if (auto hit = cpuPickAABB(*m_scene, cam, ev->pos().x(), ev->pos().y(), width(), height())) {

        auto& reg = m_scene->getRegistry();
        if (reg.all_of<GizmoHandleComponent>(hit->entity)) {
            const auto& gh = reg.get<GizmoHandleComponent>(hit->entity);
            // Modes as 0/1/2 to avoid including enum header in widget
            int mode = (gh.mode == GizmoMode::Translate) ? 0 :
                (gh.mode == GizmoMode::Rotate) ? 1 : 2;
            int axis = int(gh.axis);
            emit gizmoHandleDoubleClicked(mode, axis);

            // Don�t also do camera focus on gizmo double-click:
            m_suppressClickThisRelease = true;
            return;
        }

        // Non-gizmo: keep your existing focus behavior
        float keepDist = glm::length(cam.getPosition() - hit->worldPos);
        cam.focusOn(hit->worldPos, keepDist);
        update();
    }

    m_suppressClickThisRelease = true;
}