#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QPoint>
#include <QOpenGLContext>
#include "Camera.hpp"
#include "Robot.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include <memory>

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget();

    Camera& getCamera() { return m_camera; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // --- RE-ADDED MISSING DECLARATIONS ---
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void cleanup();

private:
    void checkGLError(const char* location);

    Camera m_camera;
    std::unique_ptr<Robot> m_robot;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Mesh> m_mesh;

    QTimer m_animationTimer;
    QPoint m_lastMousePos;

    QOpenGLContext* m_glContext = nullptr;
};