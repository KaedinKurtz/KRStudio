#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QPoint>
#include <memory>
#include "entt/entity/fwd.hpp"
#include <glm/glm.hpp>

// Forward declarations
class Scene;
class Shader;
class Mesh;
class Camera;
class QTimer;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;


class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    ViewportWidget(Scene* scene, entt::entity cameraEntity, QWidget* parent = nullptr);
    ~ViewportWidget();

    Camera& getCamera();

protected:
    // Overrides from QOpenGLWidget
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    Scene* m_scene;
    entt::entity m_cameraEntity;

    // OpenGL Resources for main rendering
    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Mesh> m_gridMesh;
    std::unique_ptr<Shader> m_phongShader;
    std::unique_ptr<Mesh> m_cubeMesh;

    std::map<std::string, std::unique_ptr<Mesh>> m_meshCache;

    // --- INTEGRATED: OpenGL resources for intersection outline rendering ---
    std::unique_ptr<Shader> m_outlineShader; // The shader program for drawing simple colored lines.
    unsigned int m_outlineVAO;               // The Vertex Array Object for the outline geometry.
    unsigned int m_outlineVBO;               // The Vertex Buffer Object for the outline geometry.

    // Other members
    QTimer* m_animationTimer;
    QPoint m_lastMousePos;
};
