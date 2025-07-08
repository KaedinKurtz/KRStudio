#pragma once

#include "IRenderPass.hpp" // The new interface
#include "components.hpp"           // For RenderResourceComponent

#include <QObject>
#include <QMap>
#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Forward declarations to keep this header light
class QOpenGLContext;
class QOpenGLWidget;
class ViewportWidget;
class Shader;
class Scene;

// This struct holds the framebuffer objects for a single viewport.
// It is managed by the RenderingSystem.
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
    void initializeSharedResources(QOpenGLFunctions_4_3_Core* gl, Scene* scene);
    void renderView(ViewportWidget* viewport, QOpenGLFunctions_4_3_Core* gl, int vpW, int vpH);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);
    void onViewportResized(ViewportWidget* vp, QOpenGLFunctions_4_3_Core* gl, int fbW, int fbH);

    // --- Scene Logic Updaters ---
    void updateSceneLogic(float deltaTime);
    void updateCameraTransforms();

    // --- Public Helpers & Getters (for Render Passes) ---
    bool isInitialized() const { return m_isInitialized; }
    Scene& getScene() const { return *m_scene; }
    entt::entity getCurrentCameraEntity() const { return m_currentCamera; }
    Shader* getShader(const std::string& name);
    const RenderResourceComponent::Buffers& getOrCreateMeshBuffers(
        QOpenGLFunctions_4_3_Core* gl, QOpenGLContext* ctx, entt::entity entity);
    void ensureContextIsTracked(QOpenGLWidget* viewport);
    QMap<ViewportWidget*, TargetFBOs> m_targets; // Use QMap for pointer keys

public slots:
    // --- Lifecycle Management Slot ---
    void onContextAboutToBeDestroyed();

private:
    // --- Private Helpers ---
    
    void initOrResizeFBOsForTarget(QOpenGLFunctions_4_3_Core* gl, TargetFBOs& target, int width, int height);
    void updateSplineCaches();

    // --- Core Member Variables ---
    bool m_isInitialized = false;
    float m_elapsedTime = 0.0f;
    Scene* m_scene = nullptr;
    entt::entity m_currentCamera = entt::null;

    // --- Render Pass Pipeline ---
    std::vector<std::unique_ptr<IRenderPass>> m_renderPasses;

    // --- Resource Management ---
    QMap<QString, Shader*> m_shaders;
    
    std::set<QOpenGLContext*> m_trackedContexts;
};