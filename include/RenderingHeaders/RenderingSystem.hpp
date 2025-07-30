#pragma once

#include "IRenderPass.hpp"
#include "components.hpp"
#include <librealsense2/rs.hpp> // ADDED: For RealSense point cloud support

#include <QObject>
#include <QMap>
#include <QHash> // ADDED
#include <QSet>  // ADDED
#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <QElapsedTimer>
#include <QTimer>

#define ENABLE_BLACKBOX_LOGGING

// Forward declarations to keep this header light
class QOpenGLContext;
class QOpenGLWidget;
class ViewportWidget;
class Shader;
class Scene;
class PointCloudPass;

// This struct holds the framebuffer objects for a single viewport. (No changes needed)
struct TargetFBOs {
    int w = 0, h = 0;
    GLuint mainFBO = 0, mainColorTexture = 0, mainDepthTexture = 0;
    GLuint glowFBO = 0, glowTexture = 0;
    GLuint pingpongFBO[2] = { 0, 0 }, pingpongTexture[2] = { 0, 0 };
};

class RenderingSystem : public QObject {
    Q_OBJECT

public:
    explicit RenderingSystem(QObject* parent = nullptr);
    ~RenderingSystem();

    // --- Lifecycle & Core API ---
    // RENAMED: This function now initializes resources for a specific context.
    void initializeResourcesForContext(QOpenGLFunctions_4_3_Core* gl, Scene* scene);
    void renderView(ViewportWidget* viewport, QOpenGLFunctions_4_3_Core* gl, int vpW, int vpH, float frameDeltaTime);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);
    void onViewportResized(ViewportWidget* vp, QOpenGLFunctions_4_3_Core* gl, int fbW, int fbH);
    void onMasterUpdate();
    // --- Scene Logic Updaters ---
    void updateSceneLogic(float deltaTime);
    void updateCameraTransforms();

    void updatePointCloud(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose);

    void onViewportWillBeDestroyed(ViewportWidget* viewport);

    // --- Public Helpers & Getters (for Render Passes) ---
    bool isInitialized() const { return m_isInitialized; }
    Scene& getScene() const { return *m_scene; }
    entt::entity getCurrentCameraEntity() const { return m_currentCamera; }
    Shader* getShader(const std::string& name);
    const RenderResourceComponent::Buffers& getOrCreateMeshBuffers(
        QOpenGLFunctions_4_3_Core* gl, QOpenGLContext* ctx, entt::entity entity);
    void ensureContextIsTracked(QOpenGLWidget* viewport);
    QMap<ViewportWidget*, TargetFBOs> m_targets;

    bool isContextInitialized(QOpenGLContext* ctx) const;

public slots:
    // --- Lifecycle Management Slot ---
    void onContextAboutToBeDestroyed();

private:
    // --- Private Helpers ---
    void initOrResizeFBOsForTarget(QOpenGLFunctions_4_3_Core* gl, TargetFBOs& target, int width, int height);
    void updateSplineCaches();

    QTimer        m_frameTimer;     // drives the simulation
    QElapsedTimer m_clock;

    QString shadersRootDir();

    // --- Core Member Variables ---
    bool m_isInitialized = false;
    float m_elapsedTime = 0.0f;
    Scene* m_scene = nullptr;
    entt::entity m_currentCamera = entt::null;

    // --- Render Pass Pipeline ---
    std::vector<std::unique_ptr<IRenderPass>> m_renderPasses;

    // --- Resource Management ---
    // FROM: QMap<QString, Shader*> m_shaders;
    // TO: A map of contexts, each holding its own map of shaders.
    QHash<QOpenGLContext*, QHash<QString, Shader*>> m_perContextShaders;

    // ADDED: A set to track which contexts we have already loaded resources for.
    QSet<QOpenGLContext*> m_initializedContexts;

    // This existing set tracks which contexts we are watching for destruction.
    QSet<QOpenGLContext*> m_trackedContexts; // FROM: std::set TO: QSet for consistency

    PointCloudPass* m_pointCloudPass = nullptr;
};