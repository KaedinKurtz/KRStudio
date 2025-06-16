#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <memory>
#include <entt/fwd.hpp>

class Scene;
class Camera;
class RenderingSystem;
class QOpenGLDebugLogger;
class Shader;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QCloseEvent;
class QPoint;

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent = nullptr);
    ~ViewportWidget() override;
    Camera& getCamera();

    void shutdown();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    Scene* m_scene;
    entt::entity m_cameraEntity;
    std::unique_ptr<RenderingSystem> m_renderingSystem;
    std::unique_ptr<QOpenGLDebugLogger> m_debugLogger;
    std::unique_ptr<Shader> m_outlineShader;
    unsigned int m_outlineVAO = 0;
    unsigned int m_outlineVBO = 0;
    QPoint m_lastMousePos;
};