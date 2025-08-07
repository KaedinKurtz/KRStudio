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
#include "Cubemap.hpp"
#include "Texture2D.hpp"
#include "UtilityHeaders/GLUtils.hpp"
#include "MaterialLoader.hpp"
#include "GLUtils.hpp"

#include "OpaquePass.hpp"
#include "LightingPass.hpp"
#include "SelectionGlowPass.hpp"
#include "GridPass.hpp"
#include "SplinePass.hpp"
#include "FieldVisualizerPass.hpp"
#include "PointCloudPass.hpp"

#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_5_Core>
#include <QDebug>
#include <stdexcept>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>


void inspectFramebufferAttachments(QOpenGLFunctions_4_5_Core* gl, GLuint fbo, const char* fboName)
{
    if (!gl) {
        qWarning() << "Cannot inspect FBO" << fboName << "- GL 4.5 functions not available.";
        return;
    }

    qDebug().noquote() << "\n===========================================================";
    qDebug().noquote() << "DEEP ATTACHMENT INSPECTION for FBO:" << fbo << QString("(%1)").arg(fboName);
    qDebug().noquote() << "-----------------------------------------------------------";
    qDebug().noquote() << QString("%1 | %2 | %3").arg("Parameter", -35).arg("DEPTH", -15).arg("STENCIL", -15);
    qDebug().noquote() << "-----------------------------------------------------------";

    // Helper lambda to query and print a parameter for both attachments
    auto printParam = [&](const QString& name, GLenum pname) {
        GLint depth_val = -1, stencil_val = -1;
        gl->glGetNamedFramebufferAttachmentParameteriv(fbo, GL_DEPTH_ATTACHMENT, pname, &depth_val);
        gl->glGetNamedFramebufferAttachmentParameteriv(fbo, GL_STENCIL_ATTACHMENT, pname, &stencil_val);
        qDebug().noquote() << QString("%1 | %2 | %3").arg(name, -35).arg(depth_val, -15).arg(stencil_val, -15);
        };

    printParam("ATTACHMENT_OBJECT_TYPE", GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
    printParam("ATTACHMENT_OBJECT_NAME", GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME);
    printParam("ATTACHMENT_COMPONENT_TYPE", GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
    printParam("ATTACHMENT_RED_SIZE", GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE);
    printParam("ATTACHMENT_GREEN_SIZE", GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE);
    printParam("ATTACHMENT_BLUE_SIZE", GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE);
    printParam("ATTACHMENT_ALPHA_SIZE", GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE);
    printParam("ATTACHMENT_DEPTH_SIZE", GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE);
    printParam("ATTACHMENT_STENCIL_SIZE", GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE);
    qDebug().noquote() << "===========================================================\n";
}

static GLuint getFullscreenQuadVAO(QOpenGLFunctions_4_3_Core* gl) {
    static GLuint quadVAO = 0;
    if (quadVAO == 0) {
        float quadVertices[] = {
            // positions     // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        GLuint quadVBO;
        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);
        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    return quadVAO;
}


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
    // 0) Grab current GL context
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCritical() << "[RenderingSystem] No active GL context!";
        return;
    }

    auto& contextShaders = m_perContextShaders[ctx];
    const QString shaderDir = shadersRootDir();
    qDebug() << "[RenderingSystem] Loading shaders from:" << shaderDir;

    // 1) Load all standard shaders
    try {
        auto loadAndStoreShader = [&](const QString& name, auto... paths) {
            std::unique_ptr<Shader> shader = Shader::build(m_gl, paths...);
            contextShaders[name] = shader.get();
            m_shaderStore.push_back(std::move(shader));
            };

        // Load the two specialized G-Buffer shaders
        loadAndStoreShader("gbuffer_textured",
            (shaderDir + "gbuffer_vert.glsl").toStdString(),
            (shaderDir + "gbuffer_textured_frag.glsl").toStdString());
        loadAndStoreShader("gbuffer_untextured",
            (shaderDir + "gbuffer_vert.glsl").toStdString(),
            (shaderDir + "gbuffer_untextured_frag.glsl").toStdString());
        loadAndStoreShader("gbuffer_triplanar",
            (shaderDir + "gbuffer_triplanar_vert.glsl").toStdString(),
            (shaderDir + "gbuffer_triplanar_frag.glsl").toStdString());

        // Load all other shaders as before
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
        loadAndStoreShader("skybox", (shaderDir + "skybox_vert.glsl").toStdString(), (shaderDir + "skybox_frag.glsl").toStdString());
    }
    catch (const std::runtime_error& e) {
        qFatal("[RenderingSystem] Shader init failed: %s", e.what());
    }

    // --- NEW: Create Default PBR Textures Directly ---
    qDebug() << "[RenderingSystem] Creating default textures...";

    // Default Albedo (White)
    m_defaultAlbedo = std::make_shared<Texture2D>();
    glm::u8vec4 white(255, 255, 255, 255);
    m_defaultAlbedo->generate(1, 1, GL_RGBA8, GL_RGBA, &white[0]);

    // Default Normal (Flat)
    m_defaultNormal = std::make_shared<Texture2D>();
    glm::u8vec4 flatNormal(128, 128, 255, 255);
    m_defaultNormal->generate(1, 1, GL_RGBA8, GL_RGBA, &flatNormal[0]);

    // Default AO (White - no occlusion)
    m_defaultAO = std::make_shared<Texture2D>();
    m_defaultAO->generate(1, 1, GL_RGBA8, GL_RGBA, &white[0]);

    // Default Metallic (Black - not metallic)
    m_defaultMetallic = std::make_shared<Texture2D>();
    glm::u8vec4 black(0, 0, 0, 255);
    m_defaultMetallic->generate(1, 1, GL_RGBA8, GL_RGBA, &black[0]);

    // Default Roughness (Grey - 0.5 roughness)
    m_defaultRoughness = std::make_shared<Texture2D>();
    glm::u8vec4 grey(128, 128, 128, 255);
    m_defaultRoughness->generate(1, 1, GL_RGBA8, GL_RGBA, &grey[0]);

    // Default Emissive (Black - not emissive)
    m_defaultEmissive = std::make_shared<Texture2D>();
    m_defaultEmissive->generate(1, 1, GL_RGBA8, GL_RGBA, &black[0]);


    // --- 2) Build IBL resources ---
    {
        QString assetDir = QCoreApplication::applicationDirPath() + QLatin1String("/../assets/");
        std::string hdrPath = (assetDir + "env3.hdr").toStdString();

        auto hdrTex = std::make_shared<Texture2D>();
        if (!hdrTex->loadFromFile(hdrPath, /*flipVert=*/true)) {
            qWarning() << "[IBL] Failed to load HDR env:" << QString::fromStdString(hdrPath);
        }
        else {
            qDebug() << "[IBL] HDR loaded successfully.";

            std::string vsCube = (shaderDir + "equirect_to_cubemap_vert.glsl").toStdString();
			std::string dummyVert = (shaderDir + "dummy_vert.glsl").toStdString();
            std::string fsCube = (shaderDir + "equirect_to_cubemap_frag.glsl").toStdString();
            std::string fsIrr = (shaderDir + "irradiance_convolution_frag.glsl").toStdString();
            std::string fsPref = (shaderDir + "prefilter_env_frag.glsl").toStdString();
            std::string vsBRDF = (shaderDir + "brdf_integration_vert.glsl").toStdString();
            std::string fsBRDF = (shaderDir + "brdf_integration_frag.glsl").toStdString();
            std::string fsTriPref = (shaderDir + "trilinear_prefilter_frag.glsl").toStdString();
            std::string vsPref = (shaderDir + "prefilter_vert.glsl").toStdString();
            // 2.1 Equirect ? Cubemap
            try {
                m_envCubemap = Cubemap::fromEquirectangular(*hdrTex, m_gl, vsCube, fsCube, 2048);
                if (!m_envCubemap) throw std::runtime_error("null ptr");
                qDebug() << "[IBL] Environment cubemap created.";
            }
            catch (const std::exception& e) {
                qWarning() << "[IBL] fromEquirectangular failed:" << e.what();
                m_envCubemap.reset();
            }

            // 2.2 Diffuse irradiance
            if (m_envCubemap) {
                try {
                    m_irradianceMap = Cubemap::convolveIrradiance(*m_envCubemap, m_gl, vsCube, fsIrr, 32);
                    if (!m_irradianceMap) throw std::runtime_error("null ptr");
                    qDebug() << "[IBL] Irradiance map ready.";
                }
                catch (const std::exception& e) {
                    qWarning() << "[IBL] convolveIrradiance failed:" << e.what();
                    m_irradianceMap.reset();
                }
            }

            // 2.3 Specular prefilter
            if (m_envCubemap) {
                try {
                    m_prefilteredEnvMap = Cubemap::prefilter(*m_envCubemap, m_gl, vsPref, fsPref, 2048, 5);
                    if (!m_prefilteredEnvMap) throw std::runtime_error("null ptr");
                    qDebug() << "[IBL] Prefiltered env map ready.";
                }
                catch (const std::exception& e) {
                    qWarning() << "[IBL] prefilter failed:" << e.what();
                    m_prefilteredEnvMap.reset();
                }
            }

            // 2.4 BRDF LUT
            try {
                m_brdfLUT = GLUtils::generateBRDFLUT(m_gl, vsBRDF, fsBRDF, 512);
                if (!m_brdfLUT) throw std::runtime_error("null ptr");
                qDebug() << "[IBL] BRDF LUT ready. ID:" << m_brdfLUT->getID();
            }
            catch (const std::exception& e) {
                qWarning() << "[IBL] BRDF LUT generation failed:" << e.what();
                m_brdfLUT.reset();
            }
        }
    }

    // 3) Build render?pass pipeline
    qDebug() << "[RenderingSystem] Building render passes...";
    m_geometryPass = std::make_unique<OpaquePass>();
    m_lightingPass = std::make_unique<LightingPass>();
    m_postProcessingPasses.push_back(std::make_unique<SelectionGlowPass>());
    m_overlayPasses.push_back(std::make_unique<GridPass>());
    m_overlayPasses.push_back(std::make_unique<SplinePass>());
    m_overlayPasses.push_back(std::make_unique<FieldVisualizerPass>());
    m_overlayPasses.push_back(std::make_unique<PointCloudPass>());

    // 4) Initialize all passes
    qDebug() << "[RenderingSystem] Initializing passes for context" << ctx;
    m_geometryPass->initialize(*this, m_gl);
    m_lightingPass->initialize(*this, m_gl);
    for (auto& p : m_postProcessingPasses) p->initialize(*this, m_gl);
    for (auto& p : m_overlayPasses)       p->initialize(*this, m_gl);
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

    // --- STATS UPDATE (unchanged) ---
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

    QElapsedTimer timer;

    // --- PIPELINE STAGE 1: Geometry Pass (Once per frame) ---
    timer.start();
    geometryPass();
    qDebug() << "[Timing] geometryPass took" << timer.elapsed() << "ms";

    // --- PIPELINE STAGES 2-4: Per-Viewport Rendering ---
    for (auto* viewport : m_targets.keys()) {
        if (!viewport) continue;

        viewport->makeCurrent();
        m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(viewport->context());
        if (!m_gl) continue;

        // Lighting
        timer.restart();
        lightingPass(viewport);
        qDebug() << "[Timing]" << "lightingPass (vp" << viewport << ") took" << timer.elapsed() << "ms";

        // Post-process
        timer.restart();
        postProcessingPass(viewport);
        qDebug() << "[Timing]" << "postProcessingPass (vp" << viewport << ") took" << timer.elapsed() << "ms";

        // Overlay
        timer.restart();
        overlayPass(viewport);
        qDebug() << "[Timing]" << "overlayPass (vp" << viewport << ") took" << timer.elapsed() << "ms";

        // Final blit
        timer.restart();
        {
            auto& target = m_targets[viewport];
            m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, target.finalFBO);
            m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
            m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, viewport->defaultFramebufferObject());
            m_gl->glDrawBuffer(GL_BACK);
            m_gl->glBlitFramebuffer(
                0, 0, target.w, target.h,
                0, 0, target.w, target.h,
                GL_COLOR_BUFFER_BIT, GL_NEAREST
            );
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        qDebug() << "[Timing]" << "finalBlit (vp" << viewport << ") took" << timer.elapsed() << "ms";

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

    for (auto ent : m_scene->getRegistry().view<MaterialDirectoryTag>())
    {
        auto& tag = m_scene->getRegistry().get<MaterialDirectoryTag>(ent);
        MaterialComponent mat = loadMaterialFromDirectory(tag.dirPath);
        m_scene->getRegistry().emplace_or_replace<MaterialComponent>(ent, std::move(mat));
    }


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
    // 0) Early-out if no size or no viewports
    if (m_gBuffer.w == 0 || m_gBuffer.h == 0 || m_targets.isEmpty()) {
        return;
    }

    // 1) Acquire the primary viewport and GL funcs
    auto* vp = m_targets.firstKey();
    vp->makeCurrent();
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(vp->context());
    if (!m_gl) {
        qWarning() << "[geometryPass] Failed to acquire GL functions.";
        vp->doneCurrent();
        return;
    }

    // 2) Bind & verify FBO
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer.fbo);
    GLenum fbStatus = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "[geometryPass] Aborting: FBO not complete! Status:" << fbStatus;
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vp->doneCurrent();
        return;
    }

    // 3) Set up draw-buffers
    constexpr GLenum drawBufs[5] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4
    };
    m_gl->glDrawBuffers(5, drawBufs);

    // 4) Viewport + depth-test
    m_gl->glViewport(0, 0, m_gBuffer.w, m_gBuffer.h);
    m_gl->glEnable(GL_DEPTH_TEST);

    // 5) Clear all attachments with a single call.
    // This now succeeds because all color attachments have the same format.
    m_gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // The entire fallback `else` block is no longer needed.

    // 6) Build frame-context & run the opaque pass
    entt::entity camE = vp->getCameraEntity();
    auto& camComp = m_scene->getRegistry().get<CameraComponent>(camE);
    float aspect = float(m_gBuffer.w) / float(m_gBuffer.h);

    static float accumTime = 0.f;
    accumTime += m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;

    auto& target = m_targets[vp];
    RenderFrameContext context{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        camComp.camera.getViewMatrix(),
        camComp.camera.getProjectionMatrix(aspect),
        target,
        m_gBuffer.w,
        m_gBuffer.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        accumTime,
        nullptr
    };
    m_geometryPass->execute(context);

    // 7) Unbind & done
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    vp->doneCurrent();
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
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(
        QOpenGLContext::currentContext());
    if (!m_gl) {
        qDebug() << "[overlayPass] GL funcs unavailable!";
        return;
    }

    auto& target = m_targets[viewport];
    const GLuint finalFBO = target.finalFBO;
    const GLuint gFBO = m_gBuffer.fbo;
    const int   w = target.w;
    const int   h = target.h;

    QElapsedTimer tTotal;
    tTotal.start();

    // --- A) Depth-Only Blit ---
    {
        QElapsedTimer t; t.start();

        // 1) Bind and check BOTH FBOs
        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, gFBO);
        GLenum readStatus = m_gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        qDebug() << "[overlayPass] READ_FRAMEBUFFER status =" << readStatus;

        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, finalFBO);
        GLenum drawStatus = m_gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        qDebug() << "[overlayPass] DRAW_FRAMEBUFFER status =" << drawStatus;

        // 2) Disable color writes on the DRAW side
        //    (for a depth?only blit the color buffer is irrelevant)
        m_gl->glDrawBuffer(GL_NONE);
        m_gl->glReadBuffer(GL_NONE);

        // 3) Perform the depth blit
        m_gl->glBlitFramebuffer(
            0, 0, w, h,
            0, 0, w, h,
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST
        );

        // 4) Restore the draw buffer so subsequent color draws work
        GLenum buf = GL_COLOR_ATTACHMENT0;
        m_gl->glDrawBuffers(1, &buf);

        // 5) Error check & timing
        GLenum err = m_gl->glGetError();
        qDebug() << "[Timing] overlay-depthBlit took" << t.elapsed() << "ms"
            << (err != GL_NO_ERROR ? QString("GL_ERR=0x%1").arg(err, 0, 16) : "");
    }

    // --- B) Prepare for color overlays ---
    {
        QElapsedTimer t; t.start();

        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
        m_gl->glViewport(0, 0, w, h);
        GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
        m_gl->glDrawBuffers(1, drawBufs);
        qDebug() << "[overlayPass] Restored DRAW_BUFFERS[0] =" << drawBufs[0];
        m_gl->glEnable(GL_DEPTH_TEST);
        m_gl->glDepthMask(GL_TRUE);

        GLenum err = m_gl->glGetError();
        qDebug() << "[Timing] overlay-prepColor took" << t.elapsed() << "ms"
            << (err != GL_NO_ERROR
                ? QString("GL_ERR=0x%1").arg(err, 0, 16)
                : QString());
    }

    // --- C) Build shared RenderFrameContext ---
    auto& camComp = m_scene->getRegistry().get<CameraComponent>(
        viewport->getCameraEntity());
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
        target.w,
        target.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        elapsed,
        viewport
    };

    // --- C1) Skybox ---
    {
        Shader* skyboxShader = getShader("skybox");
        auto   envCubemap = getEnvCubemap();
        if (skyboxShader && envCubemap) {
            QElapsedTimer t; t.start();

            m_gl->glDepthFunc(GL_LEQUAL);

            skyboxShader->use(m_gl);
            skyboxShader->setMat4(m_gl, "view", ctx.view);
            skyboxShader->setMat4(m_gl, "projection", ctx.projection);

            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap->getID());
            skyboxShader->setInt(m_gl, "skybox", 0);

            m_gl->glBindVertexArray(GLUtils::getUnitCubeVAO(m_gl));
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 36);
            m_gl->glBindVertexArray(0);

            m_gl->glDepthFunc(GL_LESS);

            GLenum err = m_gl->glGetError();
            qDebug() << "[Timing] overlay-skybox took" << t.elapsed() << "ms"
                << (err != GL_NO_ERROR
                    ? QString("GL_ERR=0x%1").arg(err, 0, 16)
                    : QString());
        }
    }

    // --- C2) Other overlay passes ---
    for (auto& pass : m_overlayPasses) {
        QElapsedTimer t; t.start();

        qDebug() << "[overlayPass] Executing overlay pass:" << typeid(*pass).name();
        pass->execute(ctx);

        GLenum err = m_gl->glGetError();
        qDebug() << "[Timing] overlay-" << typeid(*pass).name()
            << "took" << t.elapsed() << "ms"
            << (err != GL_NO_ERROR
                ? QString("GL_ERR=0x%1").arg(err, 0, 16)
                : QString());
    }

    // --- D) Cleanup & total time ---
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl->glDrawBuffer(GL_BACK);
    qDebug() << "[overlayPass] Unbound all FBOs; RESET draw buffer to GL_BACK";
    qDebug() << "--------------------------------- FRAME END ---------------------------------";
    qDebug() << "[Timing] overlay total took" << tTotal.elapsed() << "ms";
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
            mat.albedoColor = glm::mix(plc.offColor, plc.onColor, t);
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

    // --- CORRECTED VERTEX ATTRIBUTES ---
    // The 'stride' for all attributes is the size of the entire Vertex struct.
    // The 'offset' is the byte offset of that attribute within the struct.
    const GLsizei stride = sizeof(Vertex);
    // Attribute 0: Position (vec3)
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    // Attribute 1: Normal (vec3)
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    // CORRECTED: Attribute 2: Texture Coordinates (vec2)
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv)); // <-- Changed texCoords to uv

    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tangent));

    // Attribute 4: Bitangent
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, bitangent));



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
    // --- 1) Delete old resources to prevent leaks ---
    if (m_gBuffer.fbo)              gl->glDeleteFramebuffers(1, &m_gBuffer.fbo);
    if (m_gBuffer.positionTexture)  gl->glDeleteTextures(1, &m_gBuffer.positionTexture);
    if (m_gBuffer.normalTexture)    gl->glDeleteTextures(1, &m_gBuffer.normalTexture);
    if (m_gBuffer.albedoAOTexture)  gl->glDeleteTextures(1, &m_gBuffer.albedoAOTexture);
    if (m_gBuffer.metalRougTexture) gl->glDeleteTextures(1, &m_gBuffer.metalRougTexture);
    if (m_gBuffer.emissiveTexture)  gl->glDeleteTextures(1, &m_gBuffer.emissiveTexture);
    if (m_gBuffer.depthTexture)     gl->glDeleteTextures(1, &m_gBuffer.depthTexture);
    // Note: No stencil renderbuffer to delete anymore.

    // If the size is zero, just clear the struct and exit.
    if (w == 0 || h == 0) {
        m_gBuffer = {};
        return;
    }

    m_gBuffer.w = w;
    m_gBuffer.h = h;

    // --- 2) Create and bind the Framebuffer Object ---
    gl->glGenFramebuffers(1, &m_gBuffer.fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer.fbo);

    // --- 3) Create and attach UNIFORM COLOR textures ---
    createTexture(gl, m_gBuffer.positionTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gBuffer.positionTexture, 0);

    createTexture(gl, m_gBuffer.normalTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gBuffer.normalTexture, 0);

    // CHANGED: Albedo + AO is now RGBA16F for format uniformity.
    createTexture(gl, m_gBuffer.albedoAOTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gBuffer.albedoAOTexture, 0);

    // CHANGED: Metallic + Roughness is now RGBA16F for format uniformity.
    createTexture(gl, m_gBuffer.metalRougTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_gBuffer.metalRougTexture, 0);

    createTexture(gl, m_gBuffer.emissiveTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, m_gBuffer.emissiveTexture, 0);

    // Tell OpenGL which color attachments we'll be drawing to.
    const GLenum drawAttachments[5] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4
    };
    gl->glDrawBuffers(5, drawAttachments);

    // --- 4) Create and attach DEPTH-ONLY buffer ---
    createTexture(gl, m_gBuffer.depthTexture, GL_DEPTH_COMPONENT32F, w, h, GL_DEPTH_COMPONENT, GL_FLOAT);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gBuffer.depthTexture, 0);

    // --- 5) Final check and unbind ---
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "G-Buffer FBO is not complete!";
    }

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

void RenderingSystem::initOrResizeFinalFBO(QOpenGLFunctions_4_3_Core* gl_base, TargetFBOs& target, int w, int h)
{
    // --- Delete old resources ---
    if (target.finalFBO)           gl_base->glDeleteFramebuffers(1, &target.finalFBO);
    if (target.finalColorTexture)  gl_base->glDeleteTextures(1, &target.finalColorTexture);
    if (target.finalDepthTexture)  gl_base->glDeleteTextures(1, &target.finalDepthTexture);

    if (w == 0 || h == 0) { target = {}; return; }

    target.w = w; target.h = h;
    gl_base->glGenFramebuffers(1, &target.finalFBO);
    gl_base->glBindFramebuffer(GL_FRAMEBUFFER, target.finalFBO);

    // --- Color Texture ---
    createTexture(gl_base, target.finalColorTexture, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
    gl_base->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.finalColorTexture, 0);

    // --- Separate Depth and Stencil attachments ---
    createTexture(gl_base, target.finalDepthTexture, GL_DEPTH_COMPONENT32F, w, h, GL_DEPTH_COMPONENT, GL_FLOAT);
    gl_base->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.finalDepthTexture, 0);

    // --- DEEP INSPECTION ---
    auto* gl45 = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(QOpenGLContext::currentContext());
    inspectFramebufferAttachments(gl45, target.finalFBO, "Final FBO");

    // --- Final check and unbind ---
    GLenum status = gl_base->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "Final Target FBO for viewport is not complete! Status:" << status;
    }
    gl_base->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_base->glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void RenderingSystem::initializeViewportResources(ViewportWidget* viewport)
{
    // This function is currently a placeholder. As your engine grows, you might need
    // to create resources that are truly unique to a viewport (not just size-dependent).
    // That logic would go here. For now, it can be empty.
    (void)viewport; // Suppress unused parameter warning
}

std::shared_ptr<Cubemap> RenderingSystem::getIrradianceMap() const {
    return m_irradianceMap;
}

std::shared_ptr<Cubemap> RenderingSystem::getPrefilteredEnvMap() const {
    return m_prefilteredEnvMap;
}

std::shared_ptr<Texture2D> RenderingSystem::getBRDFLUT() const {
    return m_brdfLUT;
}

std::shared_ptr<Cubemap> RenderingSystem::getEnvCubemap() const {
    return m_envCubemap;
}
