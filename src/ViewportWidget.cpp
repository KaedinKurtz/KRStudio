/**
 * @file ViewportWidget.cpp
 * @brief Implementation of a QOpenGLWidget for rendering a 3D scene.
 *
 * This widget handles the initialization of OpenGL, loading of shaders and meshes,
 * rendering the scene, and processing user input (mouse and keyboard) to
 * control the camera.
 */

#include "ViewportWidget.hpp"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QDebug>
#include <vector>
#include <algorithm>
#include <iomanip>

 /**
  * @brief Constructs a ViewportWidget.
  * @param parent The parent QWidget.
  */
ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      m_glContext(nullptr),
      m_camera(glm::vec3(0.0f, 10.0f, 0.1f)) // Y=10 (directly above), Z slightly offset to avoid gimbal lock if looking straight down

{
    // For debugging purposes, logs the creation of the widget instance.
    qDebug() << "ViewportWidget CONSTRUCTOR called for instance:" << this;

    // Set the focus policy to StrongFocus to ensure the widget can receive
    // keyboard events (e.g., for camera controls).
    setFocusPolicy(Qt::StrongFocus);
}

/**
 * @brief Destroys the ViewportWidget.
 *
 * Handles the necessary cleanup, especially disconnecting the cleanup slot
 * to prevent calls on a destroyed object during context shutdown.
 */
ViewportWidget::~ViewportWidget()
{
    qDebug() << "ViewportWidget DESTRUCTOR called for instance:" << this;

    // Before this object is fully destructed, we must disconnect our cleanup slot
    // from the OpenGL context. This prevents the context's aboutToBeDestroyed signal
    // from trying to call a slot on a "dead" object, which would cause an ASSERT failure.
    if (m_glContext) {
        disconnect(m_glContext, &QOpenGLContext::aboutToBeDestroyed, this, &ViewportWidget::cleanup);
    }

    // The rest of the cleanup for OpenGL resources (m_shader, m_mesh, m_robot)
    // is handled automatically by their std::unique_ptr destructors, which will
    // be called after this function completes.
}

/**
 * @brief A slot for cleaning up GPU resources.
 *
 * This function is connected to the QOpenGLContext's aboutToBeDestroyed signal.
 * It's guaranteed to be called while the context is still current, making it the
 * safe place to release any OpenGL objects (shaders, buffers, etc.).
 */
void ViewportWidget::cleanup()
{
    qDebug() << "Context is about to be destroyed, cleaning up GPU resources...";

    // Reset the unique_ptrs. This explicitly calls the destructors of the
    // managed objects (Shader, Mesh, Robot), which in turn release their
    // associated OpenGL resources.
    m_shader.reset();
    m_mesh.reset();
    m_grid.reset();
    m_robot.reset(); // Also reset the robot if it holds OpenGL resources.

    // The context is now gone, so we clear our pointer to it.
    m_glContext = nullptr;
}

/**
 * @brief Checks for and logs any OpenGL errors.
 * @param location A string indicating where in the code the check is being performed.
 *
 * This is a utility function for debugging. It repeatedly calls glGetError()
 * until the error queue is empty, logging each error with its hexadecimal code.
 */
void ViewportWidget::checkGLError(const char* location) {
    GLenum err;
    // Loop until glGetError returns GL_NO_ERROR.
    while ((err = glGetError()) != GL_NO_ERROR) {
        // Use Qt::hex to format the error code as a hexadecimal string for clarity.
        qDebug() << "OpenGL error at" << location << "- Code:" << Qt::hex << err;
    }
}

/**
 * @brief Initializes OpenGL resources and state.
 *
 * This function is called once by Qt after the OpenGL context has been created.
 * It sets up the rendering environment, loads shaders, creates meshes, and
 * starts the animation timer.
 */
void ViewportWidget::initializeGL() {
    qDebug() << "ViewportWidget INITIALIZEGL - START for instance:" << this << " with context:" << context();
    initializeOpenGLFunctions();
    // Store a pointer to the current OpenGL context.
    m_glContext = this->context();
    m_grid = std::make_unique<Grid>(this);
    // Connect our cleanup slot to the context's destruction signal.
    // Qt::DirectConnection ensures the slot runs immediately in the signal's thread.
    connect(m_glContext, &QOpenGLContext::aboutToBeDestroyed, this, &ViewportWidget::cleanup, Qt::DirectConnection);

    // Initializes the OpenGL function pointers for the current context.
    
    checkGLError("After initializeOpenGLFunctions");

    // Set the background color (dark grey).
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f); checkGLError("After glClearColor");
    // Enable the depth test to ensure objects are drawn in the correct order.
    glEnable(GL_DEPTH_TEST); checkGLError("After glEnable(GL_DEPTH_TEST)");

    // Create the shader program from vertex and fragment shader source files.
    m_shader = std::make_unique<Shader>(this, "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
    checkGLError("After Shader Creation");

    m_grid->setFog(true, glm::vec3(0.1f, 0.1f, 0.25f), 10.0f, 30.0f);

    // Define the vertex data for a simple cube.
    // Each line represents a vertex position (x, y, z).
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
    // Create the mesh object, which will upload the vertex data to the GPU.
    m_mesh = std::make_unique<Mesh>(this, cube_vertices);
    checkGLError("After Mesh Creation");

    // Load and initialize the robot model from a URDF file.
    m_robot = std::make_unique<Robot>("simple_arm.urdf");
    checkGLError("After Robot Creation");

    // Set up and start a timer to drive the animation loop.
    if (!m_animationTimer.isActive()) {
        // Connect the timer's timeout signal to a lambda function.
        connect(&m_animationTimer, &QTimer::timeout, this, [this]() {
            // This lambda is executed every time the timer fires.
            if (m_robot) { m_robot->update(1.0 / 60.0); } // Update robot physics/state.
            update(); // Schedule a repaint of the widget.
            });
        // Start the timer to fire at approximately 60 frames per second.
        m_animationTimer.start(1000 / 60);
    }

    qDebug() << "ViewportWidget INITIALIZEGL - END for instance:" << this;
}

/**
 * @brief Renders the OpenGL scene.
 *
 * This function is called by Qt whenever the widget needs to be repainted.
 * It clears the screen and draws all objects in the scene.
 */
void ViewportWidget::paintGL() {
    checkGLError("Start of paintGL");
    // Clear the color and depth buffers.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLError("After glClear");

    float aspectRatio;
    if (height() > 0) { // Avoid division by zero
        aspectRatio = static_cast<float>(width()) / static_cast<float>(height());
    }

    // --- 1. DRAW THE GRID ---
    // The grid is typically drawn first or with depth writing disabled if drawn last,
    // so it appears behind opaque objects.
    if (m_grid) {
        qDebug() << "ViewportWidget: About to draw grid. Camera pos:" << m_camera.getPosition().x << m_camera.getPosition().y << m_camera.getPosition().z;
        qDebug() << "ViewportWidget: Camera distance for grid:" << m_camera.getDistance(); // Or your u_cameraDistance source
        m_grid->draw(m_camera, aspectRatio);
        checkGLError("After m_grid->draw()");
        qDebug() << "ViewportWidget: Finished drawing grid.";
    }
    else {
        qDebug() << "ViewportWidget: m_grid is null, not drawing grid.";
    }


    // --- 2. DRAW THE MAIN ROBOT/OBJECTS ---
    if (!m_shader || m_shader->ID == 0 || !m_mesh || !m_robot) {
        qDebug() << "PaintGL: Main Shader, Mesh, or Robot not ready.";
        return;
    }

    // Activate the shader program for rendering.
    m_shader->use(); checkGLError("After m_shader->use()");

    // Get the view (camera) and projection matrices from the camera controller.
    glm::mat4 viewMat = m_camera.getViewMatrix();
    glm::mat4 projMat = m_camera.getProjectionMatrix(aspectRatio);

    // Pass the matrices to the shader as uniforms.
    m_shader->setMat4("view", viewMat); checkGLError("After setMat4(view)");
    m_shader->setMat4("projection", projMat); checkGLError("After setMat4(projection)");

    // The robot's draw method will set the appropriate model matrices for each link.
    // An identity matrix can be used here as a placeholder if needed, but the
    // robot's draw call will override it.
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    m_shader->setMat4("model", modelMatrix); checkGLError("After setMat4(model) for simple cube");

    // Instruct the robot to draw itself using the provided shader and a mesh for its links.
    m_robot->draw(*m_shader, *m_mesh); checkGLError("After m_robot->draw()");
}

/**
 * @brief Handles widget resize events.
 * @param w The new width of the widget.
 * @param h The new height of the widget.
 *
 * This function is called by Qt whenever the widget is resized. It updates
 * the OpenGL viewport to match the new widget dimensions.
 */
void ViewportWidget::resizeGL(int w, int h) {
    qDebug() << "ViewportWidget RESIZEGL called for instance:" << this << "with size:" << w << "x" << h;
    // Prevent division by zero or negative dimensions.
    if (w <= 0 || h <= 0) return;
    // Set the OpenGL viewport to cover the entire new widget size.
    glViewport(0, 0, w, h); checkGLError("After glViewport in resizeGL");
}

/**
 * @brief Handles mouse press events.
 * @param event The QMouseEvent containing event data.
 *
 * Stores the initial position of the mouse click to be used for calculating
 * movement delta in mouseMoveEvent.
 */
void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    // Store the last mouse position to calculate the delta in the next move event.
    m_lastMousePos = event->pos();
    QOpenGLWidget::mousePressEvent(event); // Pass event to base class.
}

/**
 * @brief Handles mouse move events.
 * @param event The QMouseEvent containing event data.
 *
 * Used to control the camera's orbit (left mouse button) and pan
 * (middle mouse button) functionalities.
 */
void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    // Calculate the change in mouse position since the last event.
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();

    // Determine the action based on which button is pressed.
    bool isPanning = (event->buttons() & Qt::MiddleButton);
    bool isOrbiting = (event->buttons() & Qt::LeftButton);

    if (isOrbiting || isPanning) {
        // Update the camera's position/orientation based on the mouse movement.
        // We negate dy because screen coordinates increase downwards, while OpenGL's
        // y-axis usually points upwards.
        m_camera.processMouseMovement(dx, -dy, isPanning);
        update(); // Schedule a repaint to show the camera's new view.
    }

    // Update the last mouse position for the next event.
    m_lastMousePos = event->pos();
    QOpenGLWidget::mouseMoveEvent(event); // Pass event to base class.
}

/**
 * @brief Handles mouse wheel events.
 * @param event The QWheelEvent containing event data.
 *
 * Controls the camera's zoom level.
 */
void ViewportWidget::wheelEvent(QWheelEvent* event) {
    // Process the scroll wheel movement to zoom the camera in or out.
    // The angleDelta is typically in 1/8ths of a degree, so we divide by 120.
    m_camera.processMouseScroll(event->angleDelta().y() / 120.0f);
    update(); // Schedule a repaint.
    QOpenGLWidget::wheelEvent(event); // Pass event to base class.
}

/**
 * @brief Handles key press events.
 * @param event The QKeyEvent containing event data.
 *
 * Defines keyboard shortcuts for camera actions like toggling projection
 * and resetting the view.
 */
void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    // Toggle between perspective and orthographic projection.
    if (event->key() == Qt::Key_P) {
        m_camera.toggleProjection();
        update();
    }
    // Reset the camera to a default, known-good viewing position.
    else if (event->key() == Qt::Key_R) {
        qDebug() << "R key pressed - Resetting camera view";
        m_camera.setToKnownGoodView();
        update();
    }
    QOpenGLWidget::keyPressEvent(event); // Pass event to base class.
}