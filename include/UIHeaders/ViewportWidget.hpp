#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <memory>
#include <entt/fwd.hpp>
#include <entt/entity/registry.hpp>
#include <QLabel> // Include QLabel for stats overlay
#include <QVector>
#include "components.hpp"
#include "GizmoSystem.hpp" // Include GizmoSystem header for gizmo functionality


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

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_4_3_Core
{
    Q_OBJECT
public:
    ViewportWidget(Scene* scene, RenderingSystem* renderingSystem, entt::entity cameraEntity, QWidget* parent = nullptr);
    ~ViewportWidget() override;
    Camera& getCamera();
    entt::entity getCameraEntity() const { return m_cameraEntity; }
    void shutdown();

    void setRenderingSystem(RenderingSystem* system);
    static void propagateTransforms(entt::registry& r);

    void resizeTargetFbos(QOpenGLWidget* vp, int fbW, int fbH);
    QOpenGLDebugLogger* m_logger = nullptr;

    void setGizmoSystem(GizmoSystem* g) { m_gizmo = g; }

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
    void renderNow();

protected: // Change 'private' to 'protected' for these members
    Scene* m_scene;
    entt::entity m_cameraEntity;
    RenderingSystem* m_renderingSystem;
    QPoint m_lastMousePos;


private:

    std::unique_ptr<QOpenGLDebugLogger> m_debugLogger;
    std::unique_ptr<Shader> m_outlineShader;
    unsigned int m_outlineVAO = 0;
    unsigned int m_outlineVBO = 0;

    int m_instanceId; // Add this
    static int s_instanceCounter; // Add this

    //static void updateAnimations(entt::registry& registry, float frameDt);

    bool m_hasSignaledReady = false;

    QLabel* m_statsOverlay;

    QPoint m_pressPos;
    bool   m_maybeClick = false;
    const int m_ClickSlop = 10;  // pixels

    // in ViewportWidget.hpp (private:)
    QPoint m_clickStartPos;
    bool   m_suppressClickThisRelease = false;

    GizmoSystem* m_gizmo = nullptr;

    // Gizmo interaction state
    bool         m_gizmoDragging = false;
    entt::entity m_hoverGizmo = entt::null;
    entt::entity m_activeGizmo = entt::null;

signals: // <<< ADD THIS SECTION
    void viewportReady();
    void glContextReady();
    void selectionChanged(const QVector<entt::entity>& selectedEntities, const Camera& camera);
    void gizmoModeRequested(int mode);
    void gizmoHandleDoubleClicked(int mode, int axis);
};
