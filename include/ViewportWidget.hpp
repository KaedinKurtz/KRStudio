#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_1_Core>
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

class QOpenGLDebugLogger; // Forward declaration
class QOpenGLDebugMessage; // Forward declaration
class RenderingSystem;
class Scene;

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_4_1_Core
{
    Q_OBJECT
public:
    ViewportWidget(Scene* scene, RenderingSystem* renderingSystem, entt::entity cameraEntity, QWidget* parent = nullptr);
    ~ViewportWidget() override;
    Camera& getCamera();

    void shutdown();

    void setRenderingSystem(RenderingSystem* system);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;

	const float frameDt = 1.0f / 60.0f; // Fixed frame rate for simplicity

public slots:
    // Add this slot
    void handleLoggedMessage(const QOpenGLDebugMessage& debugMessage);

private:

    Scene* m_scene;
    entt::entity m_cameraEntity;
    RenderingSystem* m_renderingSystem;
    std::unique_ptr<QOpenGLDebugLogger> m_debugLogger;
    std::unique_ptr<Shader> m_outlineShader;
    unsigned int m_outlineVAO = 0;
    unsigned int m_outlineVBO = 0;
    QPoint m_lastMousePos;

    static int s_instanceCounter;
    int m_instanceId;

    static void updateAnimations(entt::registry& registry, float frameDt);
};