#pragma once

#include "IRenderPass.hpp"
#include "components.hpp"

#include <QObject>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QOpenGLFunctions_4_5_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <deque>
#include <QElapsedTimer>
#include <QTimer>
#include <librealsense2/rs.hpp>
#include "Texture2D.hpp"
#include "Cubemap.hpp"   // your cubemap wrapper

// Forward declarations
class QOpenGLContext;
class QOffscreenSurface;
class QSurface;
class ViewportWidget;
class FluidSystem;
class Shader;
class Scene;
class OpaquePass;
class LightingPass;
class PointCloudPass;
class GizmoPass;

// G-Buffer struct: Holds textures for geometry, normals, and material properties.
// Generated once per frame and shared by all viewports.
struct GBufferFBO {
    int     w = 0, h = 0;
    GLuint  fbo = 0;

    // MRT0: world-space position (RGBA16F)
    GLuint  positionTexture = 0;
    // MRT1: world-space normal   (RGBA16F)
    GLuint  normalTexture = 0;
    // MRT2: albedo + ambient occlusion (RGBA8)
    GLuint  albedoAOTexture = 0;
    // MRT3: metallic + roughness       (RG8)
    GLuint  metalRougTexture = 0;
    // MRT4: emissive color           (RGB16F)
    GLuint  emissiveTexture = 0;

    // Depth-only buffer
    GLuint  depthTexture = 0; // <-- NOTE: This is now depth-only
};

// Post-Processing FBO struct: Used for ping-ponging effects.
struct PostProcessingFBO {
    int w = 0, h = 0;
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthTexture = 0;
};

// Per-viewport FBO struct: Holds the final composed image for one viewport.
struct TargetFBOs {
    int w = 0, h = 0;
    GLuint finalFBO = 0;
    GLuint finalColorTexture = 0;
    GLuint finalDepthTexture = 0; // This will now be depth-only
};

// Per-stage GPU times for one frame, in milliseconds (measured with
// GL_TIME_ELAPSED queries on the engine context; ~2 frames of latency).
struct GpuTimings {
    float geometryMs = 0.0f;
    float lightingMs = 0.0f;
    float postMs = 0.0f;
    float overlayMs = 0.0f;  // grid, splines, fluid surface, gizmos
    float fluidSimMs = 0.0f; // PBF compute + whitewater (solver only)
    float totalMs() const { return geometryMs + lightingMs + postMs + overlayMs + fluidSimMs; }
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
    // Renders all viewports into their offscreen targets on the engine's own
    // GL context, then schedules widget repaints. Driven by the master timer.
    // All engine GL stays off Qt's RHI context so the compositor state cache
    // and context currency are never disturbed.
    void renderAllViewports();
    // Blits a viewport's finished frame (shared texture) into the widget's
    // backbuffer. MUST be called from ViewportWidget::paintGL.
    void presentViewport(ViewportWidget* viewport);
    // Schedules a repaint of all viewports (presentation happens in paintGL).
    void requestViewportUpdates();
    void drawFullscreenQuadWithDepthTest();
    // --- Viewport Management ---
    void onViewportAdded(ViewportWidget* viewport);
    void onViewportWillBeDestroyed(ViewportWidget* viewport);

    // --- Public Helpers & Resource Access ---
    Shader* getShader(const std::string& name);
    /// HDR ordering: lighting outputs linear radiance, TonemapPass applies
    /// ACES+gamma after the fluid composite. KRS_HDR=0 = legacy fallback.
    static bool hdrEnabled();
    const RenderResourceComponent::Buffers& getOrCreateMeshBuffers(QOpenGLFunctions_4_3_Core* gl, QOpenGLContext* ctx, entt::entity entity);
    void updatePointCloud(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose);
    // --- Getters ---
    Scene& getScene() const { return *m_scene; }
    const GBufferFBO& getGBuffer() const { return m_gBuffer; }
    const PostProcessingFBO* getPPFBOs() const { return m_ppFBOs; }
    float getFPS() const { return m_fps; }
    float getFrameTime() const { return m_frameTime; }
    GpuTimings getGpuTimings() const { return m_gpuTimings; }

    const TargetFBOs* getTargetFBO(ViewportWidget* vp) const;

    // --- Fluid simulation (GPU PBF, stepped on the engine context) ---
    FluidSystem* getFluidSystem() const { return m_fluid.get(); }
    void setSimulationPlaying(bool playing);
    void resetFluidSimulation();

    // IBL getters
    std::shared_ptr<Cubemap>   getIrradianceMap()    const;
    std::shared_ptr<Cubemap>   getPrefilteredEnvMap() const;
    std::shared_ptr<Texture2D> getBRDFLUT()           const;
    std::shared_ptr<Cubemap>   getEnvCubemap()           const;
    std::shared_ptr<Texture2D> getDefaultAlbedo() const { return m_defaultAlbedo; }
    std::shared_ptr<Texture2D> getDefaultNormal() const { return m_defaultNormal; }
    std::shared_ptr<Texture2D> getDefaultAO() const { return m_defaultAO; }
    std::shared_ptr<Texture2D> getDefaultMetallic() const { return m_defaultMetallic; }
    std::shared_ptr<Texture2D> getDefaultRoughness() const { return m_defaultRoughness; }
    std::shared_ptr<Texture2D> getDefaultEmissive() const { return m_defaultEmissive; }

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

    // --- Engine GL context (isolated from Qt's RHI compositor context) ---
    // All engine rendering happens on this context against an offscreen
    // surface; widgets only blit the shared finalColorTexture in paintGL.
    QOpenGLContext* m_engineContext = nullptr;
    QOffscreenSurface* m_engineSurface = nullptr;
    GLsync m_frameFence = nullptr; // engine->widget visibility sync (shared object)
    // Supersampling: internal render resolution = native * m_renderScale,
    // downsampled to native on present (KRS_RENDER_SCALE env override).
    float m_renderScale = 1.0f;

    // --- GPU stage timing (engine-context GL_TIME_ELAPSED query rings) ---
    static constexpr int kGpuStages = 5;     // geometry, lighting, post, overlay, fluid sim
    static constexpr int kGpuQueryRing = 3;  // written frame N, read frame N+2
    GLuint m_gpuQueries[kGpuStages][kGpuQueryRing] = {};
    bool m_gpuQueriesInitialized = false;
    quint64 m_gpuQueryFrame = 0;
    GpuTimings m_gpuTimings;
    struct PresentFBO { GLuint fbo = 0; GLuint wrappedTexture = 0; };
    QHash<ViewportWidget*, PresentFBO> m_presentFBOs; // owned by each widget's RHI context
    // Makes the engine context current; returns the previously current
    // context/surface so the caller can restore them (Qt may be mid-paint).
    bool makeEngineCurrent(QOpenGLContext*& prevCtx, QSurface*& prevSurf);
    void doneEngineCurrent(QOpenGLContext* prevCtx, QSurface* prevSurf);

    // --- Render Passes (Organized by stage) ---
    std::unique_ptr<IRenderPass> m_geometryPass;
    std::unique_ptr<IRenderPass> m_lightingPass;
    std::vector<std::unique_ptr<IRenderPass>> m_postProcessingPasses;
    std::vector<std::unique_ptr<IRenderPass>> m_overlayPasses;

    // --- Fluid sim (lives on the engine context) ---
    std::unique_ptr<FluidSystem> m_fluid;

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

    // IBL resources
    std::shared_ptr<Cubemap>   m_envCubemap;
    std::shared_ptr<Cubemap>   m_irradianceMap;
    std::shared_ptr<Cubemap>   m_prefilteredEnvMap;
    std::shared_ptr<Texture2D> m_brdfLUT;

    std::shared_ptr<Texture2D> m_defaultAlbedo;
    std::shared_ptr<Texture2D> m_defaultNormal;
    std::shared_ptr<Texture2D> m_defaultAO;
    std::shared_ptr<Texture2D> m_defaultMetallic;
    std::shared_ptr<Texture2D> m_defaultRoughness;
    std::shared_ptr<Texture2D> m_defaultEmissive;
};