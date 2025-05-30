#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QPoint>

#include "Camera.hpp"
#include "Robot.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"

#include <memory>

// FIX: Inherit PUBLICLY from QOpenGLFunctions_3_3_Core
class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget();
    const Robot* getRobot() const { return m_robot.get(); } // <-- ADD THIS
    QTimer& getTimer() { return m_timer; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    Camera m_camera;
    std::unique_ptr<Robot> m_robot;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Mesh> m_mesh;

    QTimer m_timer;
    QPoint m_lastMousePos;
};