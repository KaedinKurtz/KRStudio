#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QPoint>
#include <memory>
#include <QOpenGLContext> // <-- Include for QOpenGLContext

#include "entt/entt.hpp"

class Scene;
class Camera;
class Shader;
class Mesh;

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent = nullptr);
    ~ViewportWidget();

    Camera& getCamera();
    const Camera& getCamera() const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    // --- FIX: Slot to clean up OpenGL resources tied to a specific context ---
    void cleanup();

private:
    void checkGLError(const char* location);

    Scene* m_scene;
    entt::entity m_cameraEntity;

    // --- FIX: Pointer to the current OpenGL context ---
    // This allows us to disconnect the cleanup signal in the destructor.
    QOpenGLContext* m_glContext = nullptr;

    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Mesh>   m_gridMesh;
    std::unique_ptr<Shader> m_phongShader;
    std::unique_ptr<Mesh>   m_cubeMesh;

    QTimer m_animationTimer;
    QPoint m_lastMousePos;
};
