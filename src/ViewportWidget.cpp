#include "ViewportWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"
#include "IntersectionSystem.hpp"
#include "RenderingSystem.hpp"
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
#include <QMatrix4x4>
#include <utility>
#include <iomanip>

// Recursive helper for computing world transforms
static glm::mat4 calculateMainWorldTransform(entt::entity entity, entt::registry& registry, int depth = 0) {
    QString indent(depth * 4, ' ');
    auto* tagPtr = registry.try_get<TagComponent>(entity);
    QString tag = tagPtr ? QString::fromStdString(tagPtr->tag) : "NO_TAG";
    qDebug().noquote() << indent << "[MainFK] Entity" << entity << "tagged as" << tag;

    auto& transformComp = registry.get<TransformComponent>(entity);
    glm::mat4 local = transformComp.getTransform();
    if (registry.all_of<ParentComponent>(entity)) {
        auto& parentComp = registry.get<ParentComponent>(entity);
        if (registry.valid(parentComp.parent)) {
            glm::mat4 parentWorld = calculateMainWorldTransform(parentComp.parent, registry, depth + 1);
            return parentWorld * local;
        }
    }
    return local;
}

ViewportWidget::ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_scene(scene)
    , m_cameraEntity(cameraEntity)
    , m_animationTimer(new QTimer(this))
    , m_outlineVAO(0)
    , m_outlineVBO(0)
    , m_cleanedUp(false) {
    QSurfaceFormat fmt = format();
    fmt.setOption(QSurfaceFormat::DebugContext);
    setFormat(fmt);
    Q_ASSERT(m_scene);
    setFocusPolicy(Qt::StrongFocus);
}

ViewportWidget::~ViewportWidget() {
    // Centralized cleanup if not already done
    if (!m_cleanedUp) {
        makeCurrent();
        cleanupGL();
        doneCurrent();
    }
}

void ViewportWidget::cleanupGL() {
    // 1) Ensure valid GL context
    makeCurrent();

    // 2) Shut down RenderingSystem (deletes VAOs/VBOs/EBOs internally)
    RenderingSystem::shutdown(m_scene);

    // 3) Leave the Scene* intact.  Ownership remains with the caller.
    //    Do NOT call m_scene.reset() or registry.clear() here.

    // 4) Release context
    doneCurrent();
    m_cleanedUp = true;
}

Camera& ViewportWidget::getCamera() {
    return m_scene->getRegistry().get<CameraComponent>(m_cameraEntity).camera;
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();
    if (!context()) {
        qWarning() << "[ViewportWidget] initializeGL() called without a valid context.";
        return;
    }

    m_debugLogger = std::make_unique<QOpenGLDebugLogger>(this);
    if (m_debugLogger->initialize()) {
        connect(m_debugLogger.get(), &QOpenGLDebugLogger::messageLogged, this,
            [this](const QOpenGLDebugMessage& msg) {
                qDebug() << "[GL Debug]" << msg;
            });
        m_debugLogger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
    }
    else {
        qWarning() << "[ViewportWidget] OpenGL debug logger unavailable.";
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    try {
        m_outlineShader = std::make_unique<Shader>(this, "shaders/outline_vert.glsl", "shaders/outline_frag.glsl");
        RenderingSystem::initialize();
    }
    catch (const std::exception& e) {
        qCritical() << "[ViewportWidget] Failed to initialize RenderingSystem or shaders:" << e.what();
    }

    glGenVertexArrays(1, &m_outlineVAO);
    glGenBuffers(1, &m_outlineVBO);
    glBindVertexArray(m_outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_outlineVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);

    connect(m_animationTimer, &QTimer::timeout, this, [this]() { update(); });
    m_animationTimer->start(16);
}

void ViewportWidget::paintGL() {
    IntersectionSystem::update(m_scene);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_scene) return;

    auto& camera = getCamera();
    float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspect);

    RenderingSystem::render(m_scene, view, proj, camera.getPosition());
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    bool pan = event->buttons() & Qt::MiddleButton;
    bool orbit = event->buttons() & Qt::LeftButton;
    if (pan || orbit) getCamera().processMouseMovement(dx, -dy, pan);
    m_lastMousePos = event->pos();
    QOpenGLWidget::mouseMoveEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    getCamera().processMouseScroll(event->angleDelta().y() / 120.0f);
    QOpenGLWidget::wheelEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_P) getCamera().toggleProjection();
    else if (event->key() == Qt::Key_R) getCamera().setToKnownGoodView();
    QOpenGLWidget::keyPressEvent(event);
}

void ViewportWidget::closeEvent(QCloseEvent* event) {
    QOpenGLWidget::closeEvent(event);
}
