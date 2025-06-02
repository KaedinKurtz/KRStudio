#include "ViewportWidget.hpp"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QDebug>
#include <vector>
#include <algorithm>
#include <iomanip>

// (Constructor is fine)
ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    qDebug() << "ViewportWidget CONSTRUCTOR called for instance:" << this;
    setFocusPolicy(Qt::StrongFocus);
}


ViewportWidget::~ViewportWidget()
{
    qDebug() << "ViewportWidget DESTRUCTOR called for instance:" << this;

    // Disconnect our cleanup slot from the context before this object is fully
    // destructed. This prevents the context from trying to call a slot on a
    // "dead" object during shutdown, which resolves the ASSERT failure.
    if (m_glContext) {
        disconnect(m_glContext, &QOpenGLContext::aboutToBeDestroyed, this, &ViewportWidget::cleanup);
    }

    // The rest of the cleanup is handled automatically by the unique_ptr members.
}

// --- NEW CLEANUP SLOT ---
void ViewportWidget::cleanup()
{
    qDebug() << "Context is about to be destroyed, cleaning up GPU resources...";
    // This function is called automatically by the signal from the dying context.
    // It is guaranteed to be called while that context is still current.
    m_shader.reset();
    m_mesh.reset();
    m_robot.reset(); // Also reset the robot if it holds GL resources
    m_glContext = nullptr; // The context is gone, so clear our pointer
}

// --- CORRECTED ERROR CHECK LOG ---
void ViewportWidget::checkGLError(const char* location) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        // Use Qt::hex for proper formatting with qDebug
        qDebug() << "OpenGL error at" << location << "- Code:" << Qt::hex << err;
    }
}

void ViewportWidget::initializeGL() {
    qDebug() << "ViewportWidget INITIALIZEGL - START for instance:" << this << " with context:" << context();

    m_glContext = this->context();
    connect(m_glContext, &QOpenGLContext::aboutToBeDestroyed, this, &ViewportWidget::cleanup, Qt::DirectConnection);

    initializeOpenGLFunctions();
    checkGLError("After initializeOpenGLFunctions");

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f); checkGLError("After glClearColor");
    glEnable(GL_DEPTH_TEST); checkGLError("After glEnable(GL_DEPTH_TEST)");

    m_camera.logState("Before shader/mesh creation in initializeGL");
    m_shader = std::make_unique<Shader>(this, "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
    checkGLError("After Shader Creation");

    // --- THIS IS THE FIX: Added the actual vertex data back in ---
    const std::vector<float> cube_vertices = {
        // positions
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,

        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,

        -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,

         0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,

        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f,

        -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f
    };
    m_mesh = std::make_unique<Mesh>(this, cube_vertices);
    checkGLError("After Mesh Creation");

    m_robot = std::make_unique<Robot>("simple_arm.urdf");
    checkGLError("After Robot Creation");

    if (!m_animationTimer.isActive()) {
        connect(&m_animationTimer, &QTimer::timeout, this, [this]() {
            if (m_robot) { m_robot->update(1.0 / 60.0); }
            update();
            });
        m_animationTimer.start(1000 / 60);
    }

    m_camera.logState("After all setup in initializeGL");
    qDebug() << "ViewportWidget INITIALIZEGL - END for instance:" << this;
}

void ViewportWidget::paintGL() {
    checkGLError("Start of paintGL");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLError("After glClear");

    if (!m_shader || !m_shader->ID || !m_mesh) {
        qDebug() << "PaintGL: Shader (or ID) or Mesh not ready.";
        return;
    }

    m_shader->use(); checkGLError("After m_shader->use()");

    float aspectRatio = static_cast<float>(width()) / std::max(1, height());
    glm::mat4 viewMat = m_camera.getViewMatrix();
    glm::mat4 projMat = m_camera.getProjectionMatrix(aspectRatio);

    m_shader->setMat4("view", viewMat); checkGLError("After setMat4(view)");
    m_shader->setMat4("projection", projMat); checkGLError("After setMat4(projection)");

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    m_shader->setMat4("model", modelMatrix); checkGLError("After setMat4(model) for simple cube");

    m_robot->draw(*m_shader, *m_mesh); checkGLError("After m_mesh->draw() for simple cube");
}

void ViewportWidget::resizeGL(int w, int h) {
    qDebug() << "ViewportWidget RESIZEGL called for instance:" << this << "with size:" << w << "x" << h;
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h); checkGLError("After glViewport in resizeGL");
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();

    bool isPanning = (event->buttons() & Qt::MiddleButton);
    bool isOrbiting = (event->buttons() & Qt::LeftButton);

    if (isOrbiting || isPanning) {
        m_camera.processMouseMovement(dx, -dy, isPanning);
        update();
    }

    m_lastMousePos = event->pos();
    QOpenGLWidget::mouseMoveEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    m_camera.processMouseScroll(event->angleDelta().y() / 120.0f);
    update();
    QOpenGLWidget::wheelEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_P) {
        m_camera.toggleProjection();
        update();
    }
    else if (event->key() == Qt::Key_R) {
        qDebug() << "R key pressed - Forcing setToKnownGoodView";

        // --- THIS IS THE FIX ---
        // The function no longer takes the aspectRatio argument.
        // Simply call it without any parameters.
        m_camera.setToKnownGoodView();

        update();
    }
    QOpenGLWidget::keyPressEvent(event); // Call base class
}