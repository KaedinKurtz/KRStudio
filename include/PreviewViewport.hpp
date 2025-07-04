#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <memory>
#include <entt/entt.hpp>
#include "RobotDescription.hpp" // Keep for API compatibility

class Scene;
class RenderingSystem;

class PreviewViewport : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit PreviewViewport(QWidget* parent = nullptr);
    ~PreviewViewport() override;

    void setRenderingSystem(RenderingSystem* system, Scene* scene, entt::entity camera);

public slots:
    // --- ADDED BACK FOR API COMPATIBILITY ---
    // These slots are called by your RobotEnrichmentDialog. We add them back
    // with stub implementations to allow the project to compile.
    void updateRobot(const RobotDescription& description);
    void setAnimationSpeed(int sliderValue);
    // --- END OF ADDED SECTION ---

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;

private:
    RenderingSystem* m_renderingSystem = nullptr;
    Scene* m_scene = nullptr;
    entt::entity m_cameraEntity = entt::null;

    QPoint m_lastMousePos;
};
