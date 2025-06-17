#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
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

#include "ViewportWidget.hpp"
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "IntersectionSystem.hpp"
#include "DebugHelpers.hpp"
#include "LedTweakDialog.hpp"

 

static void propagateTransforms(entt::registry& r)
{
    auto viewParents = r.view<ParentComponent>();          // cache once

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

    // roots (no ParentComponent)
    for (auto e : r.view<TransformComponent>(entt::exclude<ParentComponent>))
        dfs(e, glm::mat4(1.0f));
}

ViewportWidget::ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent),
    m_scene(scene),
    m_cameraEntity(cameraEntity),
    m_renderingSystem(nullptr),
    m_outlineShader(nullptr),
    m_outlineVAO(0),
    m_outlineVBO(0)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);
    setFocusPolicy(Qt::StrongFocus);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&ViewportWidget::update));
    timer->start(16);
}

ViewportWidget::~ViewportWidget() {}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();   // generic 2.0 wrapper – keeps the
    // debug logger and friends happy

/* -- obtain the 4.1 core wrapper for **this** context -------------- */
    QOpenGLContext* ctx = QOpenGLContext::currentContext();

    // Qt-6 way:
    auto* funcs = QOpenGLVersionFunctionsFactory::get<
        QOpenGLFunctions_4_1_Core>(ctx);

#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    // Qt-5 way (kept for completeness)
    // auto* funcs = ctx->versionFunctions<QOpenGLFunctions_4_1_Core>();
#endif

    if (!funcs)
        qFatal("OpenGL 4.1 core functions are not available on this GPU/driver.");

    funcs->initializeOpenGLFunctions();

    /* ---- pass the interface to the renderer -------------------------- */
    m_renderingSystem = std::make_unique<RenderingSystem>(funcs);
    m_renderingSystem->initialize();
}

void ViewportWidget::shutdown()
{
    makeCurrent();
    if (m_renderingSystem) {
        m_renderingSystem->shutdown();
    }
    glDeleteVertexArrays(1, &m_outlineVAO);
    glDeleteBuffers(1, &m_outlineVBO);
    doneCurrent();
}

void ViewportWidget::paintGL()
{
    if (!m_scene || !m_renderingSystem) return;

    auto& registry = m_scene->getRegistry();
    auto& camera = getCamera();
    float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    glm::mat4 viewMatrix = camera.getViewMatrix();
    glm::mat4 projMatrix = camera.getProjectionMatrix(aspect);
    

    // 1. Run Calculation
    auto calculatedOutlines = IntersectionSystem::update(m_scene);

    // 2. Clear Frame
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 3. Render Scene
    m_renderingSystem->setCurrentCamera(m_cameraEntity);
    m_renderingSystem->updateCameraTransforms(registry);
    ::propagateTransforms(registry);
    m_renderingSystem->renderMeshes(registry, viewMatrix, projMatrix, camera.getPosition());
    m_renderingSystem->renderGrid(registry, viewMatrix, projMatrix, camera.getPosition());
    m_renderingSystem->renderSplines(registry, viewMatrix, projMatrix, camera.getPosition());

    static float timer = 0.f;
    timer += frameDt;                        // ~1/60
    bool on = std::fmod(timer, 1.f) < 0.5f;  // 2 Hz blink

    for (auto e : registry.view<RecordLedTag, RenderableMeshComponent>()) {
        auto& m = registry.get<RenderableMeshComponent>(e);
        m.colour = on ? glm::vec4(1, 0.15f, 0.15f, 1)
            : glm::vec4(0.2f, 0, 0, 1);
    }

    // 4. Draw Outline
    m_renderingSystem->drawIntersections(calculatedOutlines, viewMatrix, projMatrix);

    // 5. Draw Selection Overlay
    if (m_outlineShader)
    {
        auto selectedView = registry.view<SelectedComponent>();
        if (!selectedView.empty())
        {
            for (auto entity : selectedView)
            {
                if (auto* box = registry.try_get<BoundingBoxComponent>(entity))
                {
                    DebugHelpers::drawAABBOutline(*this, *m_outlineShader, *box, m_outlineVAO, m_outlineVBO, viewMatrix, projMatrix);
                }
            }
        }
    }
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

void ViewportWidget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

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



