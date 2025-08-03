#pragma once

#include "IRenderPass.hpp"
#include "components.hpp"

#include <QObject>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <deque>
#include <QElapsedTimer>
#include <QTimer>
#include <librealsense2/rs.hpp>

// Forward declarations
class QOpenGLContext;
class ViewportWidget;
class Shader;
class Scene;
class OpaquePass;
class LightingPass;
class PointCloudPass;

// G-Buffer struct: Holds textures for geometry, normals, and material properties.
// Generated once per frame and shared by all viewports.
struct GBufferFBO {
    int w = 0, h = 0;
    GLuint fbo = 0;
    GLuint positionTexture = 0;
    GLuint normalTexture = 0;
    GLuint albedoSpecTexture = 0;
    GLuint depthTexture = 0;
};

// Post-Processing FBO struct: Used for ping-ponging effects.
struct PostProcessingFBO {
    int w = 0, h = 0;
    GLuint fbo = 0;
    GLuint colorTexture = 0;
};

// Per-viewport FBO struct: Holds the final composed image for one viewport.
struct TargetFBOs {
    int w = 0, h = 0;
    GLuint finalFBO = 0;
    GLuint finalColorTexture = 0;
    GLuint finalDepthTexture = 0;
};

namespace rs2 {
    class points;
    class video_frame;
}

class RenderingSystem : public QObject {
    Q_OBJECT

public:
    explicit RenderingSystem(QObject* parent = nullptr);
    ~RenderingSystem();

    // --- Core Lifecycle & Pipeline ---
    void initialize(Scene* scene);
    void shutdown();
    void renderFrame(); // The new main entry point for rendering
    void drawFullscreenQuadWithDepthTest();
    // --- Viewport Management ---
    void onViewportAdded(ViewportWidget* viewport);
    void onViewportWillBeDestroyed(ViewportWidget* viewport);

    // --- Public Helpers & Resource Access ---
    Shader* getShader(const std::string& name);
    const RenderResourceComponent::Buffers& getOrCreateMeshBuffers(QOpenGLFunctions_4_3_Core* gl, QOpenGLContext* ctx, entt::entity entity);
    void updatePointCloud(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose);

    // --- Getters ---
    Scene& getScene() const { return *m_scene; }
    const GBufferFBO& getGBuffer() const { return m_gBuffer; }
    const PostProcessingFBO* getPPFBOs() const { return m_ppFBOs; }
    float getFPS() const { return m_fps; }
    float getFrameTime() const { return m_frameTime; }

    const TargetFBOs* getTargetFBO(ViewportWidget* vp) const;

public slots:
    void onContextAboutToBeDestroyed();

private slots:
    void onMasterUpdate(); // Drives simulation logic (not rendering)

private:
    // --- Pipeline Stages (Internal Logic) ---
    void geometryPass();
    void lightingPass(ViewportWidget* viewport);
    void postProcessingPass(ViewportWidget* viewport);
    void overlayPass(ViewportWidget* viewport);

    // --- Resource Initialization & Resizing ---
    void initializeSharedResources();
    void initializeViewportResources(ViewportWidget* viewport);
    void resizeGLResources();
    void initOrResizeGBuffer(QOpenGLFunctions_4_3_Core* gl, int w, int h);
    void initOrResizePPFBOs(QOpenGLFunctions_4_3_Core* gl, int w, int h);
    void initOrResizeFinalFBO(QOpenGLFunctions_4_3_Core* gl, TargetFBOs& target, int w, int h);
    void ensureContextIsTracked(QOpenGLContext* context);

    // --- Internal Scene Logic Updaters ---
    void updateCameraTransforms();
    void updateSceneLogic(float deltaTime);
    void updateSplineCaches();
    QString shadersRootDir();

    // --- Core Members ---
    bool m_isInitialized = false;
    Scene* m_scene = nullptr;
    QOpenGLFunctions_4_3_Core* m_gl = nullptr; // Cached pointer to GL functions

    // --- Render Passes (Organized by stage) ---
    std::unique_ptr<IRenderPass> m_geometryPass;
    std::unique_ptr<IRenderPass> m_lightingPass;
    std::vector<std::unique_ptr<IRenderPass>> m_postProcessingPasses;
    std::vector<std::unique_ptr<IRenderPass>> m_overlayPasses;

    // --- Framebuffers ---
    GBufferFBO m_gBuffer;
    PostProcessingFBO m_ppFBOs[2];
    QMap<ViewportWidget*, TargetFBOs> m_targets;

    // --- Resource Management (THE FIX) ---
    // The unique_ptrs now live in a simple list for automatic memory management.
    std::vector<std::unique_ptr<Shader>> m_shaderStore;
    // The hash map now stores non-owning raw pointers for fast lookup.
    QHash<QOpenGLContext*, QHash<QString, Shader*>> m_perContextShaders;
    QSet<QOpenGLContext*> m_trackedContexts;

    // --- Timing & Stats ---
    QTimer m_frameTimer;
    QElapsedTimer m_clock;
    float m_fps = 0.0f;
    float m_frameTime = 0.0f;
    std::deque<float> m_frameTimeHistory;
    const int m_historySize = 100;
};