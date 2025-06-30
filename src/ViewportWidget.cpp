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

int ViewportWidget::s_instanceCounter = 0;

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

ViewportWidget::ViewportWidget(Scene* scene, RenderingSystem* renderingSystem, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent),
    m_scene(scene),
    m_cameraEntity(cameraEntity),
    m_renderingSystem(renderingSystem),
    m_outlineShader(nullptr),
    m_outlineVAO(0),
    m_outlineVBO(0)
{
    m_instanceId = s_instanceCounter++; // Assign unique ID
    qDebug() << "Constructing ViewportWidget instance:" << m_instanceId;

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

void ViewportWidget::setRenderingSystem(RenderingSystem* system)
{
    m_renderingSystem = system;
}

ViewportWidget::~ViewportWidget() {}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();                       // hook up *this* v-table

    // -------- optional: OpenGL debug logger ---------------
    m_debugLogger = std::make_unique<QOpenGLDebugLogger>(this);
    if (m_debugLogger->initialize())
    {
        connect(m_debugLogger.get(), &QOpenGLDebugLogger::messageLogged,
            this, [](const QOpenGLDebugMessage& msg)
            { qWarning() << "[OpenGL Debug]" << msg.message(); });
        m_debugLogger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
    }

    // -------- hand renderer our function table ------------
    if (m_renderingSystem && !m_renderingSystem->isInitialized())
    {
        m_renderingSystem->setOpenGLFunctions(this);         // *this* is a QOF_4_1_Core
        m_renderingSystem->initialize(width(), height());    // first (and only) init
    }

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
    if (!m_scene || !m_renderingSystem)   // safety
        return;

    /* --------- framebuffer-pixel size of *this* dock ------------ */
    const int fbW = std::lround(width() * devicePixelRatioF());
    const int fbH = std::lround(height() * devicePixelRatioF());

    /* --------- make sure shared FBO chain is large enough -------- */
    m_renderingSystem->resize(fbW, fbH);          // no work when size unchanged

    /* --------- camera transforms --------------------------------- */
    auto& registry = m_scene->getRegistry();
    auto& cam = getCamera();

    m_renderingSystem->setCurrentCamera(m_cameraEntity);
    m_renderingSystem->updateCameraTransforms(registry);

    const float aspect = (fbH > 0) ? static_cast<float>(fbW) / fbH : 1.0f;

    glm::mat4 V = cam.getViewMatrix();
    glm::mat4 P = cam.getProjectionMatrix(aspect);
    glm::vec3 eyePos = cam.getPosition();

    auto outlines = IntersectionSystem::update(m_scene);

    /* --------- viewport BEFORE composite pass -------------------- */
    glViewport(0, 0, fbW, fbH);

    /* --------- render once into the shared system ---------------- */
    m_renderingSystem->beginFrame(registry);
    m_renderingSystem->renderScene(registry, V, P, eyePos, outlines);
    m_renderingSystem->endFrame();                  // draws composite quad

    /* ---------  optional debug overlay --------------------------- */
    if (m_outlineShader)
    {
        auto sel = registry.view<SelectedComponent>();
        for (auto e : sel)
        {
            if (auto* box = registry.try_get<BoundingBoxComponent>(e))
            {
                DebugHelpers::drawAABBOutline(*this, *m_outlineShader,
                    *box,
                    m_outlineVAO, m_outlineVBO,
                    V, P);
            }
        }
    }
}

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

void ViewportWidget::resizeGL(int w, int h)
{
    if (!m_renderingSystem) return;
    const int ww = std::lround(w * devicePixelRatioF());
    const int hh = std::lround(h * devicePixelRatioF());
    m_renderingSystem->resize(ww, hh);                // single shared FBO
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

static void updateParticleFlow(entt::registry& registry, FieldSolver& fieldSolver, float frameDt)
{
    auto view = registry.view<FieldVisualizerComponent>();
    for (auto entity : view)
    {
        auto& visualizer = view.get<FieldVisualizerComponent>(entity);
        if (!visualizer.isEnabled || visualizer.mode != FieldVisMode::Flow) {
            continue;
        }

        auto& particles = visualizer.particles;
        // (Initialize particle positions/ages/lifetimes if they are empty)

        // Loop through every particle
        for (int i = 0; i < particles.particleCount; ++i)
        {
            particles.ages[i] += frameDt;

            // If a particle is "dead", respawn it
            if (particles.ages[i] >= particles.lifetimes[i]) {
                // Reset its age and give it a new random position and lifetime
                particles.ages[i] = 0.0f;
                // particles.positions[i] = random_point_in_bounds(visualizer.bounds);
                // particles.lifetimes[i] = random_lifetime();
            }

            // --- Advection Step ---
            // Move the particle along the field vector
            glm::vec3 fieldVec = fieldSolver.getVectorAt(registry, particles.positions[i], visualizer.sourceEntities);
            particles.velocities[i] = fieldVec;
            particles.positions[i] += fieldVec * frameDt * visualizer.vectorScale;


            // --- Update Visuals ---
            // Map turbulence (which you'd get from the solver) to color
            // float turbulence = fieldSolver.getTurbulenceAt(registry, particles.positions[i]);
            // particles.colors[i] = map_turbulence_to_color(turbulence);

            // To fade in and out, calculate alpha based on age vs lifetime
            float life_t = particles.ages[i] / particles.lifetimes[i]; // 0.0 to 1.0
            float fade = sin(life_t * 3.14159f); // A sine curve makes a smooth fade in/out
            particles.colors[i].a = fade;
        }
    }
}



