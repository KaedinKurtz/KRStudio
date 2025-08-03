#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "ViewportWidget.hpp"
#include "Camera.hpp"
#include "components.hpp"
#include "PrimitiveBuilders.hpp"
#include "Scene.hpp"
#include "SplineUtils.hpp"
#include "PreviewViewport.hpp"
#include "MeshUtils.hpp"
#include "EdgeDetectPass.hpp"



#include "OpaquePass.hpp"
#include "LightingPass.hpp"
#include "SelectionGlowPass.hpp"
#include "GridPass.hpp"
#include "SplinePass.hpp"
#include "FieldVisualizerPass.hpp"
#include "PointCloudPass.hpp"

#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <stdexcept>
#include <QCoreApplication>


//==============================================================================
// Constructor & Destructor
//==============================================================================

QString RenderingSystem::shadersRootDir()
{
    // •  Release build:   <app.exe>/shaders
    // •  Debug   build:   <build-dir>/../shaders  (one level up)
#ifdef NDEBUG
    return QCoreApplication::applicationDirPath()
        + QLatin1String("/shaders/");
#else
    //  “…/build/<config>/RoboticsSoftware.exe”  ?  “…/shaders/”
    QDir exeDir{ QCoreApplication::applicationDirPath() };
    return exeDir.absoluteFilePath(QStringLiteral("../shaders/"));
#endif
}

RenderingSystem::RenderingSystem(QObject* parent) : QObject(parent)
{
    qDebug() << "[LIFECYCLE] RenderingSystem created.";
    // This timer is for simulation logic ONLY, not for rendering.
    connect(&m_frameTimer, &QTimer::timeout, this, &RenderingSystem::onMasterUpdate);
}

RenderingSystem::~RenderingSystem()
{
    // Ensure shutdown() is called to release GL resources before destruction.
    qDebug() << "[LIFECYCLE] RenderingSystem destroyed.";
}

void RenderingSystem::initialize(Scene* scene)
{
    if (m_isInitialized) return;
    m_scene = scene;

    // Start the logic timer. Rendering is now driven by external calls to renderFrame().
    m_frameTimer.start(16); // Run scene logic at a steady ~60hz.
    m_clock.start();

    qDebug() << "[RenderingSystem] Initialized. Waiting for first OpenGL context.";
}

void RenderingSystem::onMasterUpdate()
{
    if (!m_scene) return;

    // --- NEW Statistics Logic ---
    // Calculate delta time from the master clock
    const float dt = m_clock.restart() * 0.001f; // dt in seconds
    m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime = dt;

    // Add the new frame time to our history
    m_frameTimeHistory.push_back(dt);
    if (m_frameTimeHistory.size() > m_historySize)
    {
        m_frameTimeHistory.pop_front(); // Keep the history at a fixed size
    }

    // Calculate the average frame time over the history
    if (!m_frameTimeHistory.empty())
    {
        float totalTime = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0f);
        float avgDt = totalTime / m_frameTimeHistory.size();

        m_frameTime = avgDt * 1000.0f; // Store average frame time in milliseconds
        if (avgDt > 0)
        {
            m_fps = 1.0f / avgDt; // Calculate FPS from the average frame time
        }
    }

    // 2. Run ALL scene logic ONCE per frame.
    updateCameraTransforms();
    ViewportWidget::propagateTransforms(m_scene->getRegistry());
    updateSceneLogic(dt); // Pass the correct delta time

    // 3. Trigger a repaint for ALL viewports.
    for (auto* vp : m_targets.keys()) {
        if (vp) {
            vp->update();
        }
    }
}

//==============================================================================
// Resource Initialization
//==============================================================================

void RenderingSystem::initializeSharedResources()
{
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCritical() << "[RenderingSystem] Attempted to initialize shared resources with no active OpenGL context.";
        return;
    }

    auto& contextShaders = m_perContextShaders[ctx];

    try {
        const QString shaderDir = shadersRootDir();
        qDebug() << "[RenderingSystem] Loading all shaders from directory:" << shaderDir;

        // Helper lambda to create, store, and map a shader
        auto loadAndStoreShader = [&](const QString& name, auto... paths) {
            std::unique_ptr<Shader> shader = Shader::build(m_gl, paths...);
            Shader* rawPtr = shader.get();
            m_shaderStore.push_back(std::move(shader));
            contextShaders[name] = rawPtr;
            };

        // THE FIX: Use the helper lambda for every shader.
        loadAndStoreShader("gbuffer", (shaderDir + "gbuffer_vert.glsl").toStdString(), (shaderDir + "gbuffer_frag.glsl").toStdString());
        loadAndStoreShader("lighting", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "lighting_frag.glsl").toStdString());
        loadAndStoreShader("phong", (shaderDir + "vertex_shader_vert.glsl").toStdString(), (shaderDir + "fragment_shader_frag.glsl").toStdString());
        loadAndStoreShader("emissive_solid", (shaderDir + "vertex_shader_vert.glsl").toStdString(), (shaderDir + "emissive_solid_frag.glsl").toStdString());
        loadAndStoreShader("grid", (shaderDir + "grid_vert.glsl").toStdString(), (shaderDir + "grid_frag.glsl").toStdString());
        loadAndStoreShader("glow", (shaderDir + "glow_line_vert.glsl").toStdString(), (shaderDir + "glow_line_frag.glsl").toStdString(), (shaderDir + "glow_line_geom.glsl").toStdString());
        loadAndStoreShader("cap", (shaderDir + "cap_vert.glsl").toStdString(), (shaderDir + "cap_frag.glsl").toStdString(), (shaderDir + "cap_geom.glsl").toStdString());
        loadAndStoreShader("pointcloud", (shaderDir + "pointcloud_vert.glsl").toStdString(), (shaderDir + "pointcloud_frag.glsl").toStdString());
        loadAndStoreShader("instanced_arrow", (shaderDir + "instanced_arrow_vert.glsl").toStdString(), (shaderDir + "instanced_arrow_frag.glsl").toStdString());
        loadAndStoreShader("arrow_field_compute", std::vector<std::string>{ (shaderDir + "field_visualizer_comp.glsl").toStdString() });
        loadAndStoreShader("particle_update_compute", std::vector<std::string>{ (shaderDir + "particle_update_comp.glsl").toStdString() });
        loadAndStoreShader("particle_render", (shaderDir + "particle_render_vert.glsl").toStdString(), (shaderDir + "particle_render_frag.glsl").toStdString());
        loadAndStoreShader("flow_vector_compute", std::vector<std::string>{ (shaderDir + "flow_vector_update_comp.glsl").toStdString() });
        loadAndStoreShader("blur", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "gaussian_blur_frag.glsl").toStdString());

    }
    catch (const std::runtime_error& e) {
        qFatal("[RenderingSystem] FATAL: Shader initialization failed: %s", e.what());
    }

    // --- 2. Create and Categorize ALL Render Pass Instances ---
    qDebug() << "[RenderingSystem] Building the render pass pipeline...";

    // Stage 1: Geometry Pass
    m_geometryPass = std::make_unique<OpaquePass>();

    // Stage 2: Lighting Pass
    m_lightingPass = std::make_unique<LightingPass>();

    // Stage 3: Post-Processing Passes
    m_postProcessingPasses.push_back(std::make_unique<SelectionGlowPass>());
    // Future post-processing passes like blur or color grading would be added here.

    // Stage 4: Overlay Passes
    m_overlayPasses.push_back(std::make_unique<GridPass>());
    m_overlayPasses.push_back(std::make_unique<SplinePass>());
    m_overlayPasses.push_back(std::make_unique<FieldVisualizerPass>());
    m_overlayPasses.push_back(std::make_unique<PointCloudPass>());


    // --- 3. Initialize All Passes ---
    qDebug() << "[RenderingSystem] Initializing all render passes for context" << ctx;
    m_geometryPass->initialize(*this, m_gl);
    m_lightingPass->initialize(*this, m_gl);
    for (auto& pass : m_postProcessingPasses) {
        pass->initialize(*this, m_gl);
    }
    for (auto& pass : m_overlayPasses) {
        pass->initialize(*this, m_gl);
    }
}

void RenderingSystem::resizeGLResources()
{
    int maxW = 0, maxH = 0;
    for (auto* vp : m_targets.keys()) {
        maxW = std::max(maxW, int(vp->width() * vp->devicePixelRatioF()));
        maxH = std::max(maxH, int(vp->height() * vp->devicePixelRatioF()));
    }
    if (maxW == 0 || maxH == 0) return;

    if (m_gBuffer.w != maxW || m_gBuffer.h != maxH) {
        initOrResizeGBuffer(m_gl, maxW, maxH);
        initOrResizePPFBOs(m_gl, maxW, maxH);
    }
    for (auto* vp : m_targets.keys()) {
        auto& target = m_targets[vp];
        int targetW = int(vp->width() * vp->devicePixelRatioF());
        int targetH = int(vp->height() * vp->devicePixelRatioF());
        if (target.w != targetW || target.h != targetH) {
            initOrResizeFinalFBO(m_gl, target, targetW, targetH);
        }
    }
}

void RenderingSystem::renderFrame()
{
    if (!m_isInitialized || m_targets.isEmpty()) return;

    // --- Stats Update ---
    const float dt = m_clock.restart() * 0.001f;
    m_frameTimeHistory.push_back(dt);
    if (m_frameTimeHistory.size() > m_historySize) m_frameTimeHistory.pop_front();
    if (!m_frameTimeHistory.empty()) {
        float totalTime = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0f);
        float avgDt = totalTime / m_frameTimeHistory.size();
        m_frameTime = avgDt * 1000.0f;
        m_fps = (avgDt > 0) ? (1.0f / avgDt) : 0.0f;
    }
    m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime = dt;

    resizeGLResources();

    // --- PIPELINE STAGE 1: Geometry Pass (Once per frame) ---
    geometryPass();

    // --- PIPELINE STAGES 2-4: Per-Viewport Rendering ---
    for (auto* viewport : m_targets.keys()) {
        viewport->makeCurrent();
        m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(viewport->context());
        if (!m_gl) continue;

        lightingPass(viewport);
        postProcessingPass(viewport);
        overlayPass(viewport);

        // Final blit from our offscreen FBO to the viewport's default framebuffer (the screen)
        auto& target = m_targets[viewport];
        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, target.finalFBO);
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);             //  source
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, viewport->defaultFramebufferObject());
        m_gl->glDrawBuffer(GL_BACK);                          //  destination
        m_gl->glBlitFramebuffer(
            0, 0, target.w, target.h,
            0, 0, target.w, target.h,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
        );

        viewport->doneCurrent();
    }
}

void RenderingSystem::onViewportAdded(ViewportWidget* viewport)
{
    if (m_targets.contains(viewport)) return;
    qDebug() << "[RenderingSystem] Viewport added. Context:" << viewport->context();
    ensureContextIsTracked(viewport->context());
    m_targets.insert(viewport, TargetFBOs());

    viewport->makeCurrent();
    if (!m_isInitialized) {
        m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(viewport->context());
        if (!m_gl) throw std::runtime_error("Failed to get QOpenGLFunctions_4_3_Core");
        initializeSharedResources();
        m_isInitialized = true;
    }
    initializeViewportResources(viewport);
    viewport->doneCurrent();
}

void RenderingSystem::onViewportWillBeDestroyed(ViewportWidget* viewport)
{
    if (!viewport || !m_targets.contains(viewport)) return;
    qDebug() << "[CLEANUP] Releasing GPU resources for viewport" << viewport;
    viewport->makeCurrent();
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(viewport->context());
    if (gl) initOrResizeFinalFBO(gl, m_targets[viewport], 0, 0);
    viewport->doneCurrent();
    m_targets.remove(viewport);
}

//==============================================================================
// Pipeline Stage Implementations
//==============================================================================

void RenderingSystem::geometryPass()
{
    if (m_gBuffer.w == 0 || m_gBuffer.h == 0 || m_targets.isEmpty())
        return;

    auto* primaryVp = m_targets.firstKey();
    primaryVp->makeCurrent();
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(primaryVp->context());
    if (!m_gl) {
        primaryVp->doneCurrent();
        return;
    }

    qDebug() << "--- 1. GEOMETRY PASS ---";

    // 1) Bind the G-Buffer FBO
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer.fbo);
    qDebug() << "  - Bound G-Buffer FBO:" << m_gBuffer.fbo;
    // DEBUG: Is the FBO complete?
    GLenum status = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    qDebug() << "[geometryPass] FBO status =" << status
        << ((status == GL_FRAMEBUFFER_COMPLETE) ? "(COMPLETE)" : "(NOT COMPLETE)");

    // ?? CRITICAL: Enable all three draw-buffers so your shader's
    //    layout(location=0,1,2) outputs actually go somewhere:
    GLenum bufs[3] = {
        GL_COLOR_ATTACHMENT0,  // FragPos
        GL_COLOR_ATTACHMENT1,  // Normal
        GL_COLOR_ATTACHMENT2   // Albedo + Spec
    };
    m_gl->glDrawBuffers(3, bufs);
    // DEBUG: Print back what GL thinks you bound
    for (int i = 0; i < 3; ++i) {
        GLint val = 0;
        m_gl->glGetIntegerv(GL_DRAW_BUFFER0 + i, &val);
        qDebug() << "[geometryPass] DRAW_BUFFER[" << i << "] =" << val;
    }
    // 2) Full-target viewport + clear all attachments + depth
    m_gl->glViewport(0, 0, m_gBuffer.w, m_gBuffer.h);
    m_gl->glEnable(GL_DEPTH_TEST);
    const GLfloat black[] = { 0, 0, 0, 0 };
    m_gl->glClearBufferfv(GL_COLOR, 0, black);
    m_gl->glClearBufferfv(GL_COLOR, 1, black);
    m_gl->glClearBufferfv(GL_COLOR, 2, black);
    m_gl->glClear(GL_DEPTH_BUFFER_BIT);
    qDebug() << "  - Cleared FULL G-Buffer attachments.";

    // 3) Build the per-frame context and run your OpaquePass (draw only)
    entt::entity primaryCamEntity = primaryVp->getCameraEntity();
    auto& cameraComponent = m_scene->getRegistry().get<CameraComponent>(primaryCamEntity);
    float aspectRatio = float(m_gBuffer.w) / float(m_gBuffer.h);

    static float elapsedTime = 0.0f;
    elapsedTime += m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;

    RenderFrameContext context{
        m_gl,
        m_scene->getRegistry(),
        *this,
        cameraComponent.camera,
        cameraComponent.camera.getViewMatrix(),
        cameraComponent.camera.getProjectionMatrix(aspectRatio),
        m_targets.first(),
        m_gBuffer.w,
        m_gBuffer.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        elapsedTime,
        nullptr
    };

    m_geometryPass->execute(context);
    qDebug() << "  - OpaquePass executed.";

    // 4) Unbind
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    primaryVp->doneCurrent();
}

void RenderingSystem::lightingPass(ViewportWidget* viewport)
{
    qDebug() << "--- 2. LIGHTING PASS for viewport:" << viewport << "---";
    // grab our GL functions
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
    if (!m_gl) return;

    // 1) get this viewport's target FBO
    auto& target = m_targets[viewport];

    // 2) bind it and ensure we draw to COLOR_ATTACHMENT0
    // Bind & clear final FBO once
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, target.finalFBO);
    GLenum db = GL_COLOR_ATTACHMENT0;
    m_gl->glDrawBuffers(1, &db);
    m_gl->glViewport(0, 0, target.w, target.h);
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
    
            // Build a frame-context that points at the same G-Buffer textures
        auto& camComp = m_scene->getRegistry().get<CameraComponent>(
            viewport->getCameraEntity());
    float aspect = float(target.w) / float(target.h);
    static float elapsed = 0.0f;
    elapsed += m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;
    
        RenderFrameContext ctx{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        camComp.camera.getViewMatrix(),
        camComp.camera.getProjectionMatrix(aspect),
        target,
        target.w,
        target.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        elapsed,
        viewport
         };
    
        // Let the LightingPass do *its* binding of gPosition,gNormal,gAlbedoSpec
        m_lightingPass->execute(ctx);
    qDebug() << "  - LightingPass executed.";
}

void RenderingSystem::postProcessingPass(ViewportWidget* viewport)
{
    if (m_postProcessingPasses.empty()) return;
    if (m_postProcessingPasses.size() == 1 /* and it’s a no-op */) {
        qDebug() << "[postProcessingPass] No real effects—skipping.";
        return;
    }
    qDebug() << "--- 3. POST-PROCESSING PASS for viewport:" << viewport << "---";

    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(
        QOpenGLContext::currentContext());
    if (!m_gl) {
        qDebug() << "[postProcessingPass] GL funcs unavailable!";
        return;
    }

    auto& target = m_targets[viewport];
    const auto* ppFBOs = getPPFBOs();
    int readIdx = 0, writeIdx = 1;  // ping?pong indices

    m_postProcessingPasses.clear();
    m_postProcessingPasses.push_back(
        std::make_unique<EdgeDetectPass>()
    );

    for (size_t i = 0; i < m_postProcessingPasses.size(); ++i) {
        GLuint dstFBO = ppFBOs[writeIdx].fbo;
        qDebug() << "[postProc #" << i << "] === Binding WRITE FBO =" << dstFBO;

        // A) bind and set draw buffer
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
        GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
        m_gl->glDrawBuffers(1, drawBufs);
        qDebug() << "[postProc #" << i << "] Set DRAW_BUFFERS[0] =" << drawBufs[0];

        // B) status before clear
        GLenum status = m_gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        qDebug() << "[postProc #" << i << "] pre?clear FBO status =" << status;

        // C) read back a sample pixel before clear
        {
            int cx = ppFBOs[writeIdx].w / 2, cy = ppFBOs[writeIdx].h / 2;
            unsigned char before[4] = {};
            m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
            m_gl->glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, before);
            qDebug() << "[postProc #" << i << "] pre?clear center pixel ="
                << (int)before[0] << (int)before[1]
                << (int)before[2] << (int)before[3];
        }

        // D) clear only on first pass (to mimic your existing logic)
        if (i > 0) {
            qDebug() << "[postProc #" << i << "] Clearing COLOR buffer";
            m_gl->glClear(GL_COLOR_BUFFER_BIT);
        }
        else {
            qDebug() << "[postProc #" << i << "] Skipping clear (i==0)";
        }

        // E) read back a sample pixel after clear
        {
            int cx = ppFBOs[writeIdx].w / 2, cy = ppFBOs[writeIdx].h / 2;
            unsigned char after[4] = {};
            m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
            m_gl->glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, after);
            qDebug() << "[postProc #" << i << "] post?clear center pixel ="
                << (int)after[0] << (int)after[1]
                << (int)after[2] << (int)after[3];
        }

        // F) bind the source texture (your lighting result)
        GLuint srcTex = (i == 0)
            ? target.finalColorTexture
            : ppFBOs[readIdx].colorTexture;
        qDebug() << "[postProc #" << i << "] Binding SRC texture =" << srcTex;
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, srcTex);

        // G) run the pass
        RenderFrameContext ctx{
            m_gl,
            m_scene->getRegistry(),
            *this,
            Camera(), glm::mat4(1.0f), glm::mat4(1.0f),
            target, target.w, target.h,
            0.0f, 0.0f, viewport
        };
        qDebug() << "[postProc #" << i << "] Executing postProc pass";
        m_postProcessingPasses[i]->execute(ctx);

        std::swap(readIdx, writeIdx);
    }

    // --- Final blit back to target.finalFBO ---
    GLuint srcFBO = ppFBOs[readIdx].fbo;
    GLuint dstFBO = target.finalFBO;
    qDebug() << "[postProc final] Blitting from FBO" << srcFBO << "?" << dstFBO;

    // A) Bind read & draw
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    m_gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // B) status before blit
    {
        GLenum status = m_gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        qDebug() << "[postProc final] pre?blit FBO status =" << status;
    }

    // C) sample pixel in target.finalFBO *before* blit
    {
        int cx = target.w / 2, cy = target.h / 2;
        unsigned char before[4] = {};
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, before);
        qDebug() << "[postProc final] pre?blit center pixel ="
            << (int)before[0] << (int)before[1]
            << (int)before[2] << (int)before[3];
    }

    // D) Perform blit
    m_gl->glBlitFramebuffer(
        0, 0, ppFBOs[readIdx].w, ppFBOs[readIdx].h,
        0, 0, target.w, target.h,
        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
        GL_NEAREST
    );
    qDebug() << "[postProc final] Blitted COLOR+DEPTH from ppFBO[" << readIdx << "]";

    // E) sample pixel in target.finalFBO *after* blit
    {
        int cx = target.w / 2, cy = target.h / 2;
        unsigned char after[4] = {};
        m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        m_gl->glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, after);
        qDebug() << "[postProc final] post?blit center pixel ="
            << (int)after[0] << (int)after[1]
            << (int)after[2] << (int)after[3];
    }

    // F) cleanup
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    qDebug() << "[postProcessingPass] Complete.";
}

void RenderingSystem::overlayPass(ViewportWidget* viewport)
{
    qDebug() << "--- 4. OVERLAY PASS for viewport:" << viewport << "---";
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
    if (!m_gl) {
        qDebug() << "[overlayPass] GL funcs unavailable!";
        return;
    }

    auto& target = m_targets[viewport];
    const GLuint finalFBO = target.finalFBO;
    const GLuint gFBO = m_gBuffer.fbo;
    const int   w = target.w;
    const int   h = target.h;

    // --- A) Depth-Only Blit ---
    // Bind READ (source) and DRAW (dest) FBOs
    qDebug() << "[overlayPass] Depth blit: READ_FBO =" << gFBO
        << ", DRAW_FBO =" << finalFBO;
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, gFBO);
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, finalFBO);

    // Check completeness
    {
        GLenum status = m_gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        qDebug() << "[overlayPass] Depth-BLIT DRAW_FRAMEBUFFER status =" << status
            << (status == GL_FRAMEBUFFER_COMPLETE ? "(COMPLETE)" : "(INCOMPLETE)");
    }

    // Copy depth only
    m_gl->glBlitFramebuffer(
        0, 0, w, h,
        0, 0, w, h,
        GL_DEPTH_BUFFER_BIT,
        GL_NEAREST
    );
    qDebug() << "[overlayPass] Blitted DEPTH from G-Buffer ? finalFBO";

    // --- B) Prepare for color overlays ---
    // Bind finalFBO as both READ & DRAW for overlay draws
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
    m_gl->glViewport(0, 0, w, h);

    // Enable only color attachment 0 for drawing
    GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
    m_gl->glDrawBuffers(1, drawBufs);
    qDebug() << "[overlayPass] Restored DRAW_BUFFERS[0] =" << drawBufs[0];

    // Ensure depth test is on, and mask is correct
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthMask(GL_TRUE);

    // Optionally clear nothing (keep the lit scene), or clear color/depth here if desired
    // m_gl->glClear(GL_DEPTH_BUFFER_BIT);

    // --- C) Execute overlay passes (grid, splines, etc.) ---
    // Build context
    auto& camComp = m_scene->getRegistry().get<CameraComponent>(viewport->getCameraEntity());
    float aspect = float(w) / float(h);
    static float elapsed = 0.0f;
    elapsed += m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;

    RenderFrameContext ctx{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        camComp.camera.getViewMatrix(),
        camComp.camera.getProjectionMatrix(aspect),
        target,
        w, h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        elapsed,
        viewport
    };

    for (auto& pass : m_overlayPasses) {
        qDebug() << "[overlayPass] Executing overlay pass:" << typeid(*pass).name();
        pass->execute(ctx);
    }

    // --- D) Cleanup: bind default framebuffer and reset draw buffer ---
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLenum backBuf = GL_BACK;
    m_gl->glDrawBuffer(backBuf);
    qDebug() << "[overlayPass] Unbound all FBOs; RESET draw buffer to GL_BACK";
    qDebug() << "--------------------------------- FRAME END ---------------------------------";
}

void RenderingSystem::shutdown()
{
    if (!m_isInitialized) return;
    qDebug() << "[LIFECYCLE] Shutting down all GPU resources...";
    if (m_targets.isEmpty()) return;

    ViewportWidget* vp = m_targets.firstKey();
    vp->makeCurrent();
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(vp->context());
    if (!gl) return;

    // Delete all our custom FBOs
    initOrResizeGBuffer(gl, 0, 0);
    initOrResizePPFBOs(gl, 0, 0);
    for (auto& target : m_targets) {
        initOrResizeFinalFBO(gl, target, 0, 0);
    }
    m_targets.clear();

    // THE FIX: Destroy shaders from the m_shaderStore
    for (auto& shader : m_shaderStore) {
        shader->destroy(gl);
    }
    m_shaderStore.clear(); // This will delete the unique_ptrs, freeing memory.
    m_perContextShaders.clear(); // The raw pointers are now invalid.

    // Clean up per-entity mesh buffers
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view) {
        auto& res = view.get<RenderResourceComponent>(entity);
        for (auto const& [context, buffers] : res.perContext) {
            if (buffers.VAO) gl->glDeleteVertexArrays(1, &buffers.VAO);
            if (buffers.VBO) gl->glDeleteBuffers(1, &buffers.VBO);
            if (buffers.EBO) gl->glDeleteBuffers(1, &buffers.EBO);
        }
        res.perContext.clear();
    }

    vp->doneCurrent();
    m_isInitialized = false;
    qDebug() << "[LIFECYCLE] GPU resources shut down successfully.";
}


//==============================================================================
// Lifecycle Management Slots & Helpers
//==============================================================================

// This function is now a private helper.
// Its signature changed from taking a QOpenGLWidget* to a QOpenGLContext*.
void RenderingSystem::ensureContextIsTracked(QOpenGLContext* context) {
    if (!context) return;

    // Check if the context is already in our set of tracked contexts.
    if (!m_trackedContexts.contains(context)) {
        m_trackedContexts.insert(context);
        // Connect to the context's destruction signal to trigger our cleanup slot.
        connect(context, &QOpenGLContext::aboutToBeDestroyed, this, &RenderingSystem::onContextAboutToBeDestroyed);
        qDebug() << "[LIFECYCLE] Now tracking new context:" << context;
    }
}

// This is now a public slot, and its signature has changed to accept the context directly.
void RenderingSystem::onContextAboutToBeDestroyed() {
    auto* context = qobject_cast<QOpenGLContext*>(sender());
    if (!context || !m_trackedContexts.contains(context)) {
        return;
    }

    qDebug() << "[LIFECYCLE] Context" << context << "is being destroyed. Cleaning up associated resources.";

    // To perform GPU deletions, a valid context must be made current.
    // We try to find a "survivor" context to do the cleanup.
    QOpenGLFunctions_4_3_Core* gl = nullptr;
    ViewportWidget* survivorWidget = nullptr;
    for (auto* widget : m_targets.keys()) {
        if (widget->context() && widget->context() != context) {
            survivorWidget = widget;
            break;
        }
    }

    if (survivorWidget) {
        survivorWidget->makeCurrent();
        gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(survivorWidget->context());
    }
    else {
        // If there are no other contexts, we can't safely delete GPU resources.
        // We'll just clean up the CPU-side maps.
        qWarning() << "No survivor context found to clean up GPU resources for context" << context;
    }

    if (gl) {
        // --- 1. Delegate cleanup to each render pass ---
        // We now call onContextDestroyed for each categorized pass.
        if (m_geometryPass) m_geometryPass->onContextDestroyed(context, gl);
        if (m_lightingPass) m_lightingPass->onContextDestroyed(context, gl);
        for (const auto& pass : m_postProcessingPasses) {
            pass->onContextDestroyed(context, gl);
        }
        for (const auto& pass : m_overlayPasses) {
            pass->onContextDestroyed(context, gl);
        }

        // --- 2. Clean up per-entity mesh resources for the dying context ---
        auto& registry = m_scene->getRegistry();
        auto view = registry.view<RenderResourceComponent>();
        for (auto entity : view) {
            auto& res = view.get<RenderResourceComponent>(entity);
            if (res.perContext.count(context)) {
                const auto& buffers = res.perContext.at(context);
                if (buffers.VAO) gl->glDeleteVertexArrays(1, &buffers.VAO);
                if (buffers.VBO) gl->glDeleteBuffers(1, &buffers.VBO);
                if (buffers.EBO) gl->glDeleteBuffers(1, &buffers.EBO);
            }
        }
    }

    // --- 3. Always clean up CPU-side maps ---
    m_trackedContexts.remove(context);
    m_perContextShaders.remove(context);

    // Erase the per-context entries from all RenderResourceComponents
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view) {
        view.get<RenderResourceComponent>(entity).perContext.erase(context);
    }

    if (survivorWidget) {
        survivorWidget->doneCurrent();
    }

    qDebug() << "[LIFECYCLE] Finished cleanup for context" << context;
}

//==============================================================================
// Scene Logic Updaters
//==============================================================================

// This is now a private helper function.
void RenderingSystem::updateCameraTransforms() {
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<CameraComponent, TransformComponent>();
    for (auto e : view) {
        // Get the camera object and its transform component.
        auto& cam = view.get<CameraComponent>(e).camera;
        auto& xf = view.get<TransformComponent>(e);

        // Update the transform component's position and rotation from the camera's state.
        xf.translation = cam.getPosition();
        glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - cam.getPosition());
        glm::vec3 up = glm::vec3(0, 1, 0); // Assuming world up is Y
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::cross(right, fwd);
        xf.rotation = glm::quat_cast(glm::mat3(right, up, -fwd));
    }
}

// This is now a private helper function.
void RenderingSystem::updateSceneLogic(float deltaTime) {
    // We maintain a static variable for elapsed time since it's no longer in SceneProperties.
    static float elapsedTime = 0.0f;
    elapsedTime += deltaTime;

    auto& registry = m_scene->getRegistry();

    // First, update all spline caches if they are dirty.
    updateSplineCaches();

    // Update pulsing spline animations based on elapsed time.
    auto pulseView = registry.view<PulsingSplineTag, SplineComponent>();
    if (!pulseView.size_hint() == 0) { // Use .empty() for modern C++ check
        float brightness = (sin(elapsedTime * 3.0f) + 1.0f) / 2.0f;
        float finalBrightness = 0.1f + 0.9f * brightness;
        for (auto entity : pulseView) {
            pulseView.get<SplineComponent>(entity).glowColour.a = 1.0f * finalBrightness;
        }
    }

    // Update blinking LED animations.
    auto ledView = registry.view<RecordLedTag, MaterialComponent, PulsingLightComponent>();
    if (!ledView.size_hint() == 0) {
        for (auto e : ledView) {
            auto& mat = ledView.get<MaterialComponent>(e);
            const auto& plc = ledView.get<PulsingLightComponent>(e);

            // Mix between off and on colors based on a sine wave over time.
            float t = (sin(elapsedTime * plc.speed) + 1.0f) * 0.5f; // Oscillates between 0 and 1
            mat.albedo = glm::mix(plc.offColor, plc.onColor, t);
        }
    }
}

//==============================================================================
// Helpers
//==============================================================================

const RenderResourceComponent::Buffers& RenderingSystem::getOrCreateMeshBuffers(
    QOpenGLFunctions_4_3_Core* gl, QOpenGLContext* ctx, entt::entity entity)
{
    auto& res = m_scene->getRegistry().get_or_emplace<RenderResourceComponent>(entity);

    // Check if buffers for THIS context already exist and are valid.
    auto it = res.perContext.find(ctx);
    if (it != res.perContext.end()) {
        if (gl->glIsVertexArray(it->second.VAO)) {
            return it->second; // Return existing, valid buffers.
        }
    }

    // If we get here, we need to create the buffers for this context.
    qDebug() << "RenderingSystem: Creating new mesh buffers for entity" << (uint32_t)entity << "in context" << ctx;

    auto& mesh = m_scene->getRegistry().get<RenderableMeshComponent>(entity);
    RenderResourceComponent::Buffers newBuffers;

    gl->glGenVertexArrays(1, &newBuffers.VAO);
    gl->glGenBuffers(1, &newBuffers.VBO);
    gl->glGenBuffers(1, &newBuffers.EBO);

    gl->glBindVertexArray(newBuffers.VAO);

    gl->glBindBuffer(GL_ARRAY_BUFFER, newBuffers.VBO);
    gl->glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, newBuffers.EBO);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);

    gl->glEnableVertexAttribArray(0); // Position
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    gl->glEnableVertexAttribArray(1); // Normal
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    gl->glBindVertexArray(0);

    // Store the new handles and return them.
    res.perContext[ctx] = newBuffers;
    return res.perContext.at(ctx);
}

Shader* RenderingSystem::getShader(const std::string& name) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (m_perContextShaders.contains(ctx)) {
        // THE FIX: .value() on a QHash of raw pointers is perfectly safe.
        return m_perContextShaders.value(ctx).value(QString::fromStdString(name), nullptr);
    }
    qWarning() << "Shader not found for context:" << ctx << "Name:" << QString::fromStdString(name);
    return nullptr;
}

// This is now a private helper function.
void RenderingSystem::updateSplineCaches() {
    auto& registry = m_scene->getRegistry();

    // Get a view of all entities that have a SplineComponent.
    auto view = registry.view<SplineComponent>();

    // Iterate through all spline entities.
    for (auto entity : view) {
        auto& spline = view.get<SplineComponent>(entity);

        // Only perform the expensive cache calculation if the control points have changed.
        if (spline.isDirty) {
            // Call the utility function to recalculate the spline's vertices.
            SplineUtils::updateCache(spline);
            // The updateCache function is responsible for setting spline.isDirty = false;
        }
    }
}

void RenderingSystem::updatePointCloud(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose) {
    // Iterate through all registered overlay passes.
    for (const auto& pass : m_overlayPasses) {
        // Use dynamic_cast to safely check if the current pass is a PointCloudPass.
        if (auto* pcPass = dynamic_cast<PointCloudPass*>(pass.get())) {
            // If it is, call its update method with the new data and exit the function.
            pcPass->update(points, colorFrame, pose);
            return;
        }
    }
}

const TargetFBOs* RenderingSystem::getTargetFBO(ViewportWidget* vp) const
{
    // Use find() to get an iterator to the item in the map.
    auto it = m_targets.find(vp);

    // Check if the iterator is valid (i.e., the viewport was found).
    if (it != m_targets.end()) {
        // it.value() returns a reference to the actual object in the map (an l-value).
        // We can safely take its address.
        return &it.value();
    }

    // If the viewport was not found, return nullptr.
    return nullptr;
}

//==============================================================================
// FBO Management (Private Helper Functions)
//==============================================================================

// A helper to create a single 2D texture with common parameters.
// This reduces code duplication in the FBO functions.
void createTexture(QOpenGLFunctions_4_3_Core* gl, GLuint& texID, GLenum internalFormat, int w, int h, GLenum format, GLenum type) {
    if (texID) gl->glDeleteTextures(1, &texID); // Delete old texture if it exists
    gl->glGenTextures(1, &texID);
    gl->glBindTexture(GL_TEXTURE_2D, texID);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderingSystem::initOrResizeGBuffer(QOpenGLFunctions_4_3_Core* gl, int w, int h)
{
    // --- Delete old resources ---
    if (m_gBuffer.fbo) gl->glDeleteFramebuffers(1, &m_gBuffer.fbo);
    if (m_gBuffer.positionTexture) gl->glDeleteTextures(1, &m_gBuffer.positionTexture);
    if (m_gBuffer.normalTexture) gl->glDeleteTextures(1, &m_gBuffer.normalTexture);
    if (m_gBuffer.albedoSpecTexture) gl->glDeleteTextures(1, &m_gBuffer.albedoSpecTexture);
    if (m_gBuffer.depthTexture) gl->glDeleteTextures(1, &m_gBuffer.depthTexture);

    if (w == 0 || h == 0) {
        m_gBuffer = {};
        return;
    }

    m_gBuffer.w = w;
    m_gBuffer.h = h;

    gl->glGenFramebuffers(1, &m_gBuffer.fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer.fbo);

    // --- Position Texture ---
    gl->glGenTextures(1, &m_gBuffer.positionTexture);
    gl->glBindTexture(GL_TEXTURE_2D, m_gBuffer.positionTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // THE FIX
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // THE FIX
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gBuffer.positionTexture, 0);

    // --- Normal Texture ---
    gl->glGenTextures(1, &m_gBuffer.normalTexture);
    gl->glBindTexture(GL_TEXTURE_2D, m_gBuffer.normalTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // THE FIX
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // THE FIX
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gBuffer.normalTexture, 0);

    // --- Albedo + Specular Texture ---
    gl->glGenTextures(1, &m_gBuffer.albedoSpecTexture);
    gl->glBindTexture(GL_TEXTURE_2D, m_gBuffer.albedoSpecTexture);
    gl->glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,           // 8 bits per channel guaranteed
        w, h, 0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // THE FIX
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // THE FIX
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gBuffer.albedoSpecTexture, 0);

    GLuint attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    gl->glDrawBuffers(3, attachments);

    // --- Depth Texture ---
    gl->glGenTextures(1, &m_gBuffer.depthTexture);
    gl->glBindTexture(GL_TEXTURE_2D, m_gBuffer.depthTexture);
    gl->glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_DEPTH24_STENCIL8,
        w, h,
        0,
        GL_DEPTH_STENCIL,
        GL_UNSIGNED_INT_24_8,
        nullptr);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_gBuffer.depthTexture, 0);

    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "G-Buffer FBO is not complete!";
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void RenderingSystem::initOrResizePPFBOs(QOpenGLFunctions_4_3_Core* gl, int w, int h)
{
    // --- Delete old resources ---
    for (int i = 0; i < 2; ++i) {
        if (m_ppFBOs[i].fbo) gl->glDeleteFramebuffers(1, &m_ppFBOs[i].fbo);
        if (m_ppFBOs[i].colorTexture) gl->glDeleteTextures(1, &m_ppFBOs[i].colorTexture);
    }
    if (w == 0 || h == 0) {
        m_ppFBOs[0] = {}; m_ppFBOs[1] = {};
        return;
    }

    // --- Create new resources ---
    for (int i = 0; i < 2; ++i) {
        m_ppFBOs[i].w = w; m_ppFBOs[i].h = h;
        gl->glGenFramebuffers(1, &m_ppFBOs[i].fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, m_ppFBOs[i].fbo);

        gl->glGenTextures(1, &m_ppFBOs[i].colorTexture);
        gl->glBindTexture(GL_TEXTURE_2D, m_ppFBOs[i].colorTexture);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // THE FIX
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // THE FIX
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ppFBOs[i].colorTexture, 0);
        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "Post-Processing FBO " << i << " is not complete!";
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderingSystem::initOrResizeFinalFBO(QOpenGLFunctions_4_3_Core* gl, TargetFBOs& target, int w, int h)
{
    // --- Delete old resources ---
    if (target.finalFBO) gl->glDeleteFramebuffers(1, &target.finalFBO);
    if (target.finalColorTexture) gl->glDeleteTextures(1, &target.finalColorTexture);
    if (target.finalDepthTexture) gl->glDeleteTextures(1, &target.finalDepthTexture);

    if (w == 0 || h == 0) {
        target = {};
        return;
    }

    target.w = w;
    target.h = h;
    gl->glGenFramebuffers(1, &target.finalFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, target.finalFBO);

    // --- Color Texture ---
    gl->glGenTextures(1, &target.finalColorTexture);
    gl->glBindTexture(GL_TEXTURE_2D, target.finalColorTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // THE FIX
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // THE FIX
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.finalColorTexture, 0);

    {
        GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
        gl->glDrawBuffers(1, drawBufs);
        
    }

    // --- Depth Texture ---
    gl->glGenTextures(1, &target.finalDepthTexture);
    gl->glBindTexture(GL_TEXTURE_2D, target.finalDepthTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, target.finalDepthTexture, 0);

    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Final Target FBO for viewport is not complete!";
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderingSystem::initializeViewportResources(ViewportWidget* viewport)
{
    // This function is currently a placeholder. As your engine grows, you might need
    // to create resources that are truly unique to a viewport (not just size-dependent).
    // That logic would go here. For now, it can be empty.
    (void)viewport; // Suppress unused parameter warning
}