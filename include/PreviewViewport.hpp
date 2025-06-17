#pragma once

#include "RobotDescription.hpp"
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_1_Core>
#include <memory>
#include <entt/entt.hpp>
#include <map> // For the mesh cache

class Scene;
class Shader;
class Mesh;
class QTimer;

class PreviewViewport : public QOpenGLWidget, public QOpenGLFunctions_4_1_Core
{
    Q_OBJECT

public:
    explicit PreviewViewport(QWidget* parent = nullptr);
    ~PreviewViewport() override;

    // --- ADD THIS SECTION ---
public slots:
    // A public slot to receive a new robot description and update the preview.
    void updateRobot(const RobotDescription& description);
    // A public slot to receive the new speed value from the UI slider.
    void setAnimationSpeed(int sliderValue);
    // --- END OF ADDED SECTION ---

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private slots:
    void onAnimationTick();

private:
    void applyJointAnimations();

    std::unique_ptr<Scene> m_scene;
    entt::entity m_cameraEntity;
    std::unique_ptr<RobotDescription> m_robotDesc;

    // Rendering
    std::unique_ptr<Shader> m_phongShader;
    struct CachedMesh {
        std::unique_ptr<Mesh> mesh;
        unsigned int VAO = 0;
        unsigned int VBO = 0;
        unsigned int EBO = 0;
    };
    std::map<std::string, CachedMesh> m_meshCache;

    // Animation
    QTimer* m_animationTimer;
    float m_totalTime = 0.0f;
    float m_animationSpeed = 1.0f;
};
