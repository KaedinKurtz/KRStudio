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
#include "SimulationController.hpp"   // G.0 PhysX-core lifecycle gate

#include "OpaquePass.hpp"
#include "LightingPass.hpp"
#include "SelectionGlowPass.hpp"
#include "GridPass.hpp"
#include "SplinePass.hpp"
#include "FieldVisualizerPass.hpp"
#include "PointCloudPass.hpp"
#include "GizmoPass.hpp"
#include "FluidPass.hpp"
#include "MpmPass.hpp"
#include "FluidSurfacePass.hpp"
#include "CollisionDebugPass.hpp"
#include "TonemapPass.hpp"
#include "GlassPass.hpp"
#include "SmokeSystem.hpp"
#include "MpmSystem.hpp"
#include "MpmAdjoint.hpp"
#include "HilClock.hpp"
#include "HilBridges.hpp"
#include "TrajectoryVerifier.hpp"
#include "CadImporter.hpp"
#include <cstdio>   // std::fflush (KRS_STEP_INSPECT recon dump)
#include <cstdlib>  // std::_Exit  (KRS_STEP_INSPECT one-shot exit)
#include "FemSolver.hpp"
#include "RobotDynamics.hpp"    // Phase A GATE A oracle self-tests
#include "ArticulationGate.hpp" // Phase A GATE A PhysX articulation gate
#include "VisibleArticGate.hpp" // Phase V GATE V (V.1 / V-assign) solid->link assignment
#include "FemSystem.hpp"
#include "FemVizPass.hpp"
#include "SmokePass.hpp"
#include "MeshMaterialSource.hpp"
#include "DfsphBackend.hpp"
#include "FluidSystem.hpp"
#include "SdfColliderQuery.hpp" // Phase B GATE C (krs::fluid::runCollisionSyncGateC)
#include "IntegrationHarness.hpp" // Phase 0 GATE 0a/0b (krs::integ conservation + causal harnesses)
#include "RayPick.hpp"            // Phase 3 GATE 3.1 (krs::pick raycast)
#include "MqttBridge.hpp"         // Phase 4 GATE M (krs::mqtt broker + joint round-trip)
#include "BridgeNodes.hpp"        // Phase 5 GATE ND (krs::nodes graph->backend bridge)
#include "MqttNodes.hpp"          // Phase 2 GATE NODE-MQTT (krs::nodes auto-MQTT nodes)
#include "ControllerGates.hpp"    // Phase 4 controller gates (krs::ctrl C-track/C-knob/C-glass)
#include "NodeEditorGate.hpp"     // node-editor front-end gates (krs::nodes INPUT-BIND / TYPE / TIME)
#include "NodeEditQueue.hpp"      // force immediate UI-edit mode for the headless gates
#include "ConnectControlGate.hpp" // node-editor GATE CONNECT-AND-CONTROL

#include <QOpenGLContext>
#include <QOffscreenSurface>
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
    // Allow manual override, e.g. RS_SHADERS=D:/RoboticsSoftware/shaders
    if (qEnvironmentVariableIsSet("RS_SHADERS")) {
        const QString envDir = qEnvironmentVariable("RS_SHADERS");
        QDir d(envDir);
        if (d.exists())
            return d.absoluteFilePath(QStringLiteral("./"));
    }

    const QString exeDir = QCoreApplication::applicationDirPath();

    // Try the most sensible location first: beside the EXE.
    QStringList candidates = {
        exeDir + QLatin1String("/shaders/"),
        exeDir + QLatin1String("/../shaders/"),
        exeDir + QLatin1String("/../../shaders/")
    };

    // Use a sentinel file we know should exist
    const QString sentinel = QStringLiteral("gbuffer_vert.glsl");

    for (const QString& dir : candidates) {
        if (QFileInfo::exists(QDir(dir).filePath(sentinel))) {
            return QDir(dir).absoluteFilePath(QStringLiteral("./"));
        }
    }

    qWarning() << "[RenderingSystem] Could not find shaders. Tried:"
        << candidates << " (missing" << sentinel << ")";
    // Fallback (still return something sane)
    return QDir(exeDir + QLatin1String("/shaders/")).absoluteFilePath(QStringLiteral("./"));
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

    // Create the engine's own GL context + offscreen surface. All engine
    // rendering happens here; Qt's RHI compositor context is never touched.
    // The two share objects (textures, buffers, programs, sync objects) via
    // the application-wide share group (Qt::AA_ShareOpenGLContexts).
    if (!m_engineContext) {
        m_engineSurface = new QOffscreenSurface(nullptr, this);
        m_engineSurface->setFormat(QSurfaceFormat::defaultFormat());
        m_engineSurface->create();

        m_engineContext = new QOpenGLContext(this);
        m_engineContext->setFormat(QSurfaceFormat::defaultFormat());
        m_engineContext->setShareContext(QOpenGLContext::globalShareContext());
        if (!m_engineContext->create())
            qCritical() << "[RenderingSystem] Failed to create engine GL context!";
        else
            qDebug() << "[RenderingSystem] Engine GL context created, sharing with"
                     << QOpenGLContext::globalShareContext();
    }

    // Supersampling factor: internal targets render at native * scale and are
    // downsampled on present. 1.0 = native. Try 1.5-2.0 for smoother edges on
    // GPUs with headroom (cost grows with the square of the factor).
    if (qEnvironmentVariableIsSet("KRS_RENDER_SCALE")) {
        bool ok = false;
        const float s = qEnvironmentVariable("KRS_RENDER_SCALE").toFloat(&ok);
        if (ok) m_renderScale = std::clamp(s, 0.5f, 4.0f);
    }
    qInfo() << "[RenderingSystem] Render scale:" << m_renderScale;

    // Start the logic timer. Rendering is driven by renderAllViewports().
    m_frameTimer.start(16); // Run scene logic at a steady ~60hz.
    m_clock.start();

    qDebug() << "[RenderingSystem] Initialized. Waiting for first OpenGL context.";
}

bool RenderingSystem::makeEngineCurrent(QOpenGLContext*& prevCtx, QSurface*& prevSurf)
{
    prevCtx = QOpenGLContext::currentContext();
    prevSurf = prevCtx ? prevCtx->surface() : nullptr;
    if (!m_engineContext) return false;
    return m_engineContext->makeCurrent(m_engineSurface);
}

void RenderingSystem::doneEngineCurrent(QOpenGLContext* prevCtx, QSurface* prevSurf)
{
    if (m_engineContext) m_engineContext->doneCurrent();
    // Restore whatever context Qt had current (it may be mid-paint-cycle).
    if (prevCtx && prevSurf) prevCtx->makeCurrent(prevSurf);
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
            // This now correctly constructs a std::vector from the path arguments
            // before passing it to the Shader::build function.
            std::unique_ptr<Shader> shader = Shader::build(m_gl, { paths... });
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
        loadAndStoreShader("gbuffer_tessellated_triplanar",
            (shaderDir + "gbuffer_tess_vert.glsl").toStdString(),
            (shaderDir + "gbuffer_tess_tesc.glsl").toStdString(),
            (shaderDir + "gbuffer_tess_tese.glsl").toStdString(),
            (shaderDir + "gbuffer_triplanar_tess_frag.glsl").toStdString()
        );
        loadAndStoreShader("gbuffer_triplanar_pom",
            (shaderDir + "gbuffer_pom_vert.glsl").toStdString(),
            (shaderDir + "gbuffer_triplanar_pom_frag.glsl").toStdString()
        );
        loadAndStoreShader("pp_mesh_mask",
            (shaderDir + "pp_mesh_mask_vert.glsl").toStdString(),
            (shaderDir + "pp_mesh_mask_frag.glsl").toStdString());

        loadAndStoreShader("mask_flat",
            (shaderDir + "mask_flat_vert.glsl").toStdString(),
            (shaderDir + "mask_flat_frag.glsl").toStdString());

        loadAndStoreShader("solid_mask",
            (shaderDir + "vertex_shader_vert.glsl").toStdString(),
            (shaderDir + "mask_flat_frag.glsl").toStdString());

        loadAndStoreShader("mask_depthcompare",
            (shaderDir + "post_process_vert.glsl").toStdString(),
            (shaderDir + "mask_depthcompare_frag.glsl").toStdString());

        loadAndStoreShader("outline_sobel",
            (shaderDir + "post_process_vert.glsl").toStdString(),
            (shaderDir + "outline_sobel_frag.glsl").toStdString());

        loadAndStoreShader("edge_detect_advanced",
            (shaderDir + "post_process_vert.glsl").toStdString(),
            (shaderDir + "edge_detect_advanced_frag.glsl").toStdString());

        loadAndStoreShader("gizmo_flat",
            (shaderDir + "gizmo_flat_vert.glsl").toStdString(),
			(shaderDir + "gizmo_flat_frag.glsl").toStdString());

        loadAndStoreShader("gizmo_highlight",
            (shaderDir + "gizmo_highlight_vert.glsl").toStdString(),
            (shaderDir + "gizmo_highlight_frag.glsl").toStdString());

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

        // Fluid (PBF) pipeline
        loadAndStoreShader("fluid_integrate", std::vector<std::string>{ (shaderDir + "fluid_integrate_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_grid", std::vector<std::string>{ (shaderDir + "fluid_grid_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_lambda", std::vector<std::string>{ (shaderDir + "fluid_lambda_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_deltap", std::vector<std::string>{ (shaderDir + "fluid_deltap_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_finalize", std::vector<std::string>{ (shaderDir + "fluid_finalize_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_render", (shaderDir + "fluid_particle_vert.glsl").toStdString(), (shaderDir + "fluid_particle_frag.glsl").toStdString());
        loadAndStoreShader("collision_debug", (shaderDir + "collision_debug_vert.glsl").toStdString(), (shaderDir + "collision_debug_frag.glsl").toStdString());
        // Screen-space fluid surface pipeline
        loadAndStoreShader("fluid_ssf_depth", (shaderDir + "fluid_ssf_depth_vert.glsl").toStdString(), (shaderDir + "fluid_ssf_depth_frag.glsl").toStdString());
        loadAndStoreShader("fluid_ssf_smooth", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "fluid_ssf_smooth_frag.glsl").toStdString());
        loadAndStoreShader("fluid_ssf_thickness", (shaderDir + "fluid_ssf_depth_vert.glsl").toStdString(), (shaderDir + "fluid_ssf_thickness_frag.glsl").toStdString());
        loadAndStoreShader("fluid_ssf_composite", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "fluid_ssf_composite_frag.glsl").toStdString());
        loadAndStoreShader("fluid_ssf_backdepth", (shaderDir + "fluid_ssf_depth_vert.glsl").toStdString(), (shaderDir + "fluid_ssf_backdepth_frag.glsl").toStdString());
        loadAndStoreShader("fluid_foam_accum_decay", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "fluid_foam_accum_decay_frag.glsl").toStdString());
        loadAndStoreShader("fluid_foam_accum_inject", (shaderDir + "fluid_foam_accum_inject_vert.glsl").toStdString(), (shaderDir + "fluid_foam_accum_inject_frag.glsl").toStdString());
        loadAndStoreShader("fluid_caustics", std::vector<std::string>{ (shaderDir + "fluid_caustics_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_caustics_apply", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "fluid_caustics_apply_frag.glsl").toStdString());
        loadAndStoreShader("fluid_aniso", std::vector<std::string>{ (shaderDir + "fluid_aniso_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_compact", std::vector<std::string>{ (shaderDir + "fluid_compact_comp.glsl").toStdString() });
        // Eulerian gas solver compute shaders.
        loadAndStoreShader("smoke_emit", std::vector<std::string>{ (shaderDir + "smoke_emit_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_advect", std::vector<std::string>{ (shaderDir + "smoke_advect_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_curl", std::vector<std::string>{ (shaderDir + "smoke_curl_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_forces", std::vector<std::string>{ (shaderDir + "smoke_forces_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_combust", std::vector<std::string>{ (shaderDir + "smoke_combust_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_divergence", std::vector<std::string>{ (shaderDir + "smoke_divergence_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_jacobi", std::vector<std::string>{ (shaderDir + "smoke_jacobi_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_project", std::vector<std::string>{ (shaderDir + "smoke_project_comp.glsl").toStdString() });
        loadAndStoreShader("smoke_raymarch", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "smoke_raymarch_frag.glsl").toStdString());
        // MLS-MPM continuum solver: P2G -> grid update -> G2P. The grid int
        // buffer is cleared each substep with glClearBufferData (4.3 core),
        // so no clear shader is needed. mpm_render is registered in M5.
        loadAndStoreShader("mpm_p2g", std::vector<std::string>{ (shaderDir + "mpm_p2g_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_grid", std::vector<std::string>{ (shaderDir + "mpm_grid_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_g2p", std::vector<std::string>{ (shaderDir + "mpm_g2p_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_render", (shaderDir + "mpm_render_vert.glsl").toStdString(), (shaderDir + "mpm_render_frag.glsl").toStdString());
        // Phase 5: FEM body surface recolour (shares the MPM viz ramp + range).
        loadAndStoreShader("fem_viz", (shaderDir + "fem_viz_vert.glsl").toStdString(), (shaderDir + "fem_viz_frag.glsl").toStdString());
        // MLS-MPM thermodynamics: heat scatter -> normalize -> diffuse -> gather.
        loadAndStoreShader("mpm_heat_scatter", std::vector<std::string>{ (shaderDir + "mpm_heat_scatter_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_heat_normalize", std::vector<std::string>{ (shaderDir + "mpm_heat_normalize_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_heat_diffuse", std::vector<std::string>{ (shaderDir + "mpm_heat_diffuse_comp.glsl").toStdString() });
        loadAndStoreShader("mpm_heat_gather", std::vector<std::string>{ (shaderDir + "mpm_heat_gather_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_foam_emit", std::vector<std::string>{ (shaderDir + "fluid_foam_emit_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_foam_update", std::vector<std::string>{ (shaderDir + "fluid_foam_update_comp.glsl").toStdString() });
        loadAndStoreShader("fluid_foam_render", (shaderDir + "fluid_foam_vert.glsl").toStdString(), (shaderDir + "fluid_foam_frag.glsl").toStdString());
        loadAndStoreShader("particle_render", (shaderDir + "particle_render_vert.glsl").toStdString(), (shaderDir + "particle_render_frag.glsl").toStdString());
        loadAndStoreShader("flow_vector_compute", std::vector<std::string>{ (shaderDir + "flow_vector_update_comp.glsl").toStdString() });
        loadAndStoreShader("blur", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "gaussian_blur_frag.glsl").toStdString());
        loadAndStoreShader("skybox", (shaderDir + "skybox_vert.glsl").toStdString(), (shaderDir + "skybox_frag.glsl").toStdString());
        loadAndStoreShader("tonemap", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "tonemap_frag.glsl").toStdString());
        loadAndStoreShader("glass", (shaderDir + "glass_vert.glsl").toStdString(), (shaderDir + "glass_frag.glsl").toStdString());
        loadAndStoreShader("outline_edge", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "outline_edge_frag.glsl").toStdString());
        loadAndStoreShader("composite_simple", (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "composite_simple_frag.glsl").toStdString());
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

    // Phase A.1b: world-scale UV checker for imported CAD bodies. UVs are metres, so at
    // albedoTiling.x = 1 this 1-tile texture spans 1 m: each metre shows one 8x8 checker with
    // an orange grid (reads continuity across smooth seams) + a red origin cell (orientation).
    {
        const int N = 256, cells = 8, cell = N / cells;
        std::vector<glm::u8vec4> px(size_t(N) * N);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            const int cx = x / cell, cy = y / cell;
            glm::u8vec4 c = ((cx + cy) & 1) ? glm::u8vec4(60, 62, 72, 255) : glm::u8vec4(198, 204, 214, 255);
            if (x % cell < 2 || y % cell < 2) c = glm::u8vec4(255, 138, 0, 255);   // orange grid lines
            if (cx == 0 && cy == 0)           c = glm::u8vec4(220, 44, 44, 255);   // red origin cell
            px[size_t(y) * N + x] = c;
        }
        m_cadChecker = std::make_shared<Texture2D>();
        m_cadChecker->generate(N, N, GL_RGBA8, GL_RGBA, px.data());
        m_cadChecker->setWrap(GL_REPEAT, GL_REPEAT);
        m_cadChecker->generateMipmaps();
    }

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
        QString assetDir = QCoreApplication::applicationDirPath() + QLatin1String("/assets/");
        std::string hdrPath = (assetDir + "env2.hdr").toStdString();

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
            // IBL bake resolution. 2048 float cubemaps (env + prefiltered,
            // each ~200+ MB with mips) caused VRAM pressure that broke window
            // composition on integrated GPUs. 512/128 is visually equivalent
            // for irradiance/reflection probes at far lower memory cost.
            // KRS_IBL_SIZE overrides for experimentation.
            const int iblSize = qEnvironmentVariableIsSet("KRS_IBL_SIZE")
                ? qEnvironmentVariableIntValue("KRS_IBL_SIZE") : 512;
            const int prefilterSize = std::max(64, iblSize / 4);

            // 2.1 Equirect ? Cubemap
            try {
                m_envCubemap = Cubemap::fromEquirectangular(*hdrTex, m_gl, vsCube, fsCube, iblSize);
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
                    m_prefilteredEnvMap = Cubemap::prefilter(*m_envCubemap, m_gl, vsPref, fsPref, prefilterSize, 5);
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
    m_overlayPasses.push_back(std::make_unique<CollisionDebugPass>());
    // MLS-MPM particles: opaque, depth-tested against the scene so water and
    // glass composite over them correctly.
    m_overlayPasses.push_back(std::make_unique<MpmPass>());
    m_overlayPasses.push_back(std::make_unique<FemVizPass>()); // Phase 5: recolour FEM solid meshes
    // Fluid must depth-test against the real scene; GizmoPass clears the
    // depth buffer to draw on top, so it must come last.
    m_overlayPasses.push_back(std::make_unique<FluidSurfacePass>());
    // Glass refracts the lit HDR frame INCLUDING the water, so it runs
    // between the fluid composite and the display transform.
    m_overlayPasses.push_back(std::make_unique<GlassPass>());
    // Volumetric gas (smoke/fire) composites in linear HDR over the scene,
    // depth-occluded, before the tonemap.
    m_overlayPasses.push_back(std::make_unique<SmokePass>());
    // Display transform AFTER the water/foam composite (they blend in linear
    // HDR), BEFORE the gizmo (authored in display space).
    m_overlayPasses.push_back(std::make_unique<TonemapPass>());
    m_overlayPasses.push_back(std::make_unique<GizmoPass>());

    // Fluid solver lives on the engine context alongside the passes.
    m_fluid = std::make_unique<FluidSystem>();
    m_fluid->initialize(*this, m_gl);
    // Reference-fidelity CPU tier (real SI units); no-op stub when the
    // SPlisHSPlasH superbuild is disabled.
    m_fluid->setExternalSolver(FluidBackend::DfsphCpu, std::make_unique<DfsphBackend>());

    // Eulerian gas solver (smoke + fire), engine context.
    m_smoke = std::make_unique<SmokeSystem>();
    m_smoke->initialize(*this, m_gl);

    // Unified MLS-MPM continuum solver (fluid/elastic/sand/snow), engine context.
    m_mpm = std::make_unique<MpmSystem>();
    m_mpm->initialize(*this, m_gl);

    // Phase 5: async FEM oracle driver for rigid solid bodies.
    m_fem = std::make_unique<krs::fem::FemSystem>();

    // 4) Initialize all passes
    qDebug() << "[RenderingSystem] Initializing passes for context" << ctx;
    m_geometryPass->initialize(*this, m_gl);
    m_lightingPass->initialize(*this, m_gl);
    for (auto& p : m_postProcessingPasses) p->initialize(*this, m_gl);
    for (auto& p : m_overlayPasses)       p->initialize(*this, m_gl);

    // ------------------------------------------------------------------
    // KRS_OVERNIGHT_BENCH: one consolidated headless dashboard across every
    // self-test gate, with a final tally + process exit code (= #failed
    // groups). The rigid-body KRS_BENCH (7 analytic checks) runs via its own
    // BenchmarkRunner path on a built sim world and is reported separately.
    // ------------------------------------------------------------------
    // Headless gates run AFTER MainWindow construction, which turns the NodeEditQueue's deferred mode ON
    // (UI edits coalesce per frame in the live app). Gates drive a widget and read the node's output
    // immediately, so force IMMEDIATE mode here. (GATE THREAD toggles deferral locally and resets it.)
    krs::nodes::NodeEditQueue::instance().setDeferred(false);

    // Phase F GATE G (F0): standalone headless render self-test. Validates the
    // colormap encoding / determinism / depth-bias / projection (G1-G9).
    if (qEnvironmentVariableIntValue("KRS_RENDER_SELFTEST") != 0) {
        std::printf("\n================= KRS_RENDER_SELFTEST =================\n");
        const bool ok = runRenderGates();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase V GATE V.2: render the visible FANUC, tracked features -> predicted pixels.
    if (qEnvironmentVariableIntValue("KRS_FANUC_RENDER_SELFTEST") != 0) {
        std::printf("\n================= KRS_FANUC_RENDER_SELFTEST =================\n");
        const bool ok = runFanucRenderGateV2();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase A-CLOSE GATE U (AC1/AC2/AC3): applied texture rides the UV body + tiling scales.
    if (qEnvironmentVariableIntValue("KRS_APPLYTEX_SELFTEST") != 0) {
        std::printf("\n================= KRS_APPLYTEX_SELFTEST =================\n");
        const bool ok = runAppliedTextureGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase V GATE V.6: the boot path (shared helper + demo drive + tick) moves the FANUC.
    if (qEnvironmentVariableIntValue("KRS_FANUC_BOOT_SELFTEST") != 0) {
        std::printf("\n================= KRS_FANUC_BOOT_SELFTEST =================\n");
        const bool ok = krs::dyn::runFanucBootGateV6();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase V diagnostic: dump per-solid vert/index/bbox/link for the imported FANUC.
    if (qEnvironmentVariableIntValue("KRS_FANUC_SOLID_DUMP") != 0) {
        std::printf("\n================= KRS_FANUC_SOLID_DUMP =================\n");
        const bool ok = krs::dyn::runFanucSolidDump();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase A GATE U: world-scale + coverage of the B-Rep UV generation.
    if (qEnvironmentVariableIntValue("KRS_UV_SELFTEST") != 0) {
        std::printf("\n================= KRS_UV_SELFTEST =================\n");
        const bool ok = krs::cad::runUvGateU();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase B GATE C3: dynamic-flip continuity (live pose + velocity).
    if (qEnvironmentVariableIntValue("KRS_FLIP_SELFTEST") != 0) {
        std::printf("\n================= KRS_FLIP_SELFTEST =================\n");
        const bool ok = SimulationController::runFlipContinuityGateC3();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase B GATE C (C1/C2/C4): SDF mesh collider rides the live body (no ghost) + neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_COLLISIONSYNC_SELFTEST") != 0) {
        std::printf("\n================= KRS_COLLISIONSYNC_SELFTEST =================\n");
        const bool ok = krs::fluid::runCollisionSyncGateC();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 0 GATE 0a: conservation instrument (closed 2-body collision conserves momentum + leak neg-ctrl).
    if (qEnvironmentVariableIntValue("KRS_CONSERVATION_SELFTEST") != 0) {
        std::printf("\n================= KRS_CONSERVATION_SELFTEST =================\n");
        const bool ok = krs::integ::runConservationGate0();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 0 GATE 0b: causal-chain instrument (localizes a severed pipeline stage + neg-ctrl).
    if (qEnvironmentVariableIntValue("KRS_CAUSALCHAIN_SELFTEST") != 0) {
        std::printf("\n================= KRS_CAUSALCHAIN_SELFTEST =================\n");
        const bool ok = krs::integ::runCausalChainGate0();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 0 GATE 0c: headless GPU-fluid+SDF (real FluidSystem::update vs a moving collider + ghost neg-ctrl).
    if (qEnvironmentVariableIntValue("KRS_GPUFLUIDSDF_SELFTEST") != 0) {
        std::printf("\n================= KRS_GPUFLUIDSDF_SELFTEST =================\n");
        const bool ok = runGpuFluidSdfGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 GATE 1.2: FLUID<->RIGID Newton's 3rd law (delivered impulse == rigid momentum gained).
    if (qEnvironmentVariableIntValue("KRS_FLUIDRIGID_SELFTEST") != 0) {
        std::printf("\n================= KRS_FLUIDRIGID_SELFTEST =================\n");
        const bool ok = runFluidRigidImpulseGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 GATE 1.3: ARTICULATION<->COLLISION (collision transform tracks live FK + stale neg-ctrl).
    if (qEnvironmentVariableIntValue("KRS_ARTICCOLLISION_SELFTEST") != 0) {
        std::printf("\n================= KRS_ARTICCOLLISION_SELFTEST =================\n");
        const bool ok = krs::dyn::runArticCollisionGate1_3();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 GATE 1.4: MPM<->THERMAL energy conservation (Fourier conduction + energy-leak neg-ctrl).
    if (m_mpm && qEnvironmentVariableIntValue("KRS_MPMTHERMAL_SELFTEST") != 0) {
        std::printf("\n================= KRS_MPMTHERMAL_SELFTEST =================\n");
        const bool ok = m_mpm->runThermalGate1_4(*this, m_gl);
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 GATE 1.5: FEM static equilibrium (net reaction == applied load + unbalanced neg-ctrl).
    if (qEnvironmentVariableIntValue("KRS_FEMEQUIL_SELFTEST") != 0) {
        std::printf("\n================= KRS_FEMEQUIL_SELFTEST =================\n");
        const bool ok = krs::fem::FemSolver::runEquilibriumGate1_5();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 2 GATE 2: the full canonical causal chain (cmd->FK->push->cube->fluid) + severed-stage localization.
    if (qEnvironmentVariableIntValue("KRS_CANONICALCHAIN_SELFTEST") != 0) {
        std::printf("\n================= KRS_CANONICALCHAIN_SELFTEST =================\n");
        const bool ok = runCanonicalChainGate2();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE 3.1: raycast accuracy (ray-triangle pick >=99% vs the coarse AABB pick).
    if (qEnvironmentVariableIntValue("KRS_RAYCAST_SELFTEST") != 0) {
        std::printf("\n================= KRS_RAYCAST_SELFTEST =================\n");
        const bool ok = krs::pick::runRaycastGate3_1();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE F: B-Rep feature selector (ray-pick -> exact analytic axis/radius <1e-9).
    if (qEnvironmentVariableIntValue("KRS_BREPSEL_SELFTEST") != 0) {
        std::printf("\n================= KRS_BREPSEL_SELFTEST =================\n");
        const bool ok = krs::cad::runBRepSelectorGateF();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE J: joint/mate tooling (derive revolute frame from two bores <1e-6 vs oracle).
    if (qEnvironmentVariableIntValue("KRS_JOINT_SELFTEST") != 0) {
        std::printf("\n================= KRS_JOINT_SELFTEST =================\n");
        const bool ok = krs::cad::runJointGateJ();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 GATE M: Mosquitto broker on startup + canonical joint cmd/state round-trip.
    if (qEnvironmentVariableIntValue("KRS_MQTT_SELFTEST") != 0) {
        std::printf("\n================= KRS_MQTT_SELFTEST =================\n");
        const bool ok = krs::mqtt::runMqttGateM();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 5 GATE ND: visual node graph wired to the live ECS backend (headless).
    if (qEnvironmentVariableIntValue("KRS_NODE_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODE_SELFTEST =================\n");
        const bool ok = krs::nodes::runNodeGraphGateND();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 GATE NODE-UI: in-node widget param drives output + bounded footprint.
    if (qEnvironmentVariableIntValue("KRS_NODEUI_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODEUI_SELFTEST =================\n");
        const bool ok = krs::nodes::runNodeUiGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE NODE-LIB: library nodes vs closed-form references.
    if (qEnvironmentVariableIntValue("KRS_NODELIB_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODELIB_SELFTEST =================\n");
        const bool ok = krs::nodes::runNodeLibraryGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 2 GATE NODE-MQTT: a publish-node drives the live robot through the bus (FK-verified).
    if (qEnvironmentVariableIntValue("KRS_NODEMQTT_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODEMQTT_SELFTEST =================\n");
        const bool ok = krs::nodes::runMqttNodeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 GATE C-track: computed-torque tracking vs the soft-PD lag under a moving setpoint.
    if (qEnvironmentVariableIntValue("KRS_CTRACK_SELFTEST") != 0) {
        std::printf("\n================= KRS_CTRACK_SELFTEST =================\n");
        const bool ok = krs::ctrl::runControllerTrackGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 GATE C-knob: a goal-knob node's dial drives the live joint (FK-verified).
    if (qEnvironmentVariableIntValue("KRS_CKNOB_SELFTEST") != 0) {
        std::printf("\n================= KRS_CKNOB_SELFTEST =================\n");
        const bool ok = krs::ctrl::runControllerKnobGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 GATE C-glass: the glass robot tracks the planned config (not the live one).
    if (qEnvironmentVariableIntValue("KRS_CGLASS_SELFTEST") != 0) {
        std::printf("\n================= KRS_CGLASS_SELFTEST =================\n");
        const bool ok = krs::ctrl::runControllerGlassGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 5 GATE NODE-E2E: a canvas program drives the live robot; severing localizes the break.
    if (qEnvironmentVariableIntValue("KRS_NODEE2E_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODEE2E_SELFTEST =================\n");
        const bool ok = krs::nodes::runNodeE2EGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Node-editor GATE INPUT-BIND: per-input widget mounted in the body drives the output.
    if (qEnvironmentVariableIntValue("KRS_INPUTBIND_SELFTEST") != 0) {
        std::printf("\n================= KRS_INPUTBIND_SELFTEST =================\n");
        const bool ok = krs::nodes::runInputBindGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Node-editor GATE TYPE: port type compatibility.
    if (qEnvironmentVariableIntValue("KRS_TYPE_SELFTEST") != 0) {
        std::printf("\n================= KRS_TYPE_SELFTEST =================\n");
        const bool ok = krs::nodes::runTypeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Node-editor GATE TIME: live time source drives a sine over wall-clock.
    if (qEnvironmentVariableIntValue("KRS_TIME_SELFTEST") != 0) {
        std::printf("\n================= KRS_TIME_SELFTEST =================\n");
        const bool ok = krs::nodes::runTimeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Node-editor GATE CONNECT-AND-CONTROL: wired program (live time, widget-set value) drives the robot.
    if (qEnvironmentVariableIntValue("KRS_CONNECTCTRL_SELFTEST") != 0) {
        std::printf("\n================= KRS_CONNECTCTRL_SELFTEST =================\n");
        const bool ok = krs::nodes::runConnectControlGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Node-editor GATE FRAME: every registered node type exposes ports via the real QtNodes model.
    if (qEnvironmentVariableIntValue("KRS_FRAME_SELFTEST") != 0) {
        std::printf("\n================= KRS_FRAME_SELFTEST =================\n");
        const bool ok = krs::nodes::runFrameGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Node-editor GATE VIS: visualizer readout/gauge displayed value matches input (digits/decimals).
    if (qEnvironmentVariableIntValue("KRS_VIS_SELFTEST") != 0) {
        std::printf("\n================= KRS_VIS_SELFTEST =================\n");
        const bool ok = krs::nodes::runVisGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // SPINE GATE DEMO-GRAPH: editing the canvas sine changes the live robot's motion (boot graph drives it).
    if (qEnvironmentVariableIntValue("KRS_DEMOGRAPH_SELFTEST") != 0) {
        std::printf("\n================= KRS_DEMOGRAPH_SELFTEST =================\n");
        const bool ok = krs::nodes::runDemoGraphGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // SPINE GATE OWNERSHIP: the node command is the sole joint driver; no graph -> rest; switching is robust.
    if (qEnvironmentVariableIntValue("KRS_OWNERSHIP_SELFTEST") != 0) {
        std::printf("\n================= KRS_OWNERSHIP_SELFTEST =================\n");
        const bool ok = krs::nodes::runOwnershipGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Control-library GATE PID: PID node closes a plant onto a step, matching an independent reference.
    if (qEnvironmentVariableIntValue("KRS_PID_SELFTEST") != 0) {
        std::printf("\n================= KRS_PID_SELFTEST =================\n");
        const bool ok = krs::nodes::runPidGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Control-library GATE FILTER: Kalman/low-pass/moving-average each vs an independent reference.
    if (qEnvironmentVariableIntValue("KRS_FILTER_SELFTEST") != 0) {
        std::printf("\n================= KRS_FILTER_SELFTEST =================\n");
        const bool ok = krs::nodes::runFilterGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE THREAD: UI edits decoupled from physics -- tick rate idle vs hammered (async) vs old sync path.
    if (qEnvironmentVariableIntValue("KRS_THREAD_SELFTEST") != 0) {
        std::printf("\n================= KRS_THREAD_SELFTEST =================\n");
        const bool ok = krs::nodes::runThreadGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE DRAGDROP: a catalog drop instances the correct typed node with ports+widgets at the drop pos.
    if (qEnvironmentVariableIntValue("KRS_DRAGDROP_SELFTEST") != 0) {
        std::printf("\n================= KRS_DRAGDROP_SELFTEST =================\n");
        const bool ok = krs::nodes::runDragDropGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE F3: hard-feature disambiguation (small bore / shared edge / edge-vs-face).
    if (qEnvironmentVariableIntValue("KRS_DISAMBIG_SELFTEST") != 0) {
        std::printf("\n================= KRS_DISAMBIG_SELFTEST =================\n");
        const bool ok = krs::cad::runBRepDisambiguationGateF3();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE F5: dense-scene pick stress (>=20 bodies / >=100k tris + latency).
    if (qEnvironmentVariableIntValue("KRS_DENSE_SELFTEST") != 0) {
        std::printf("\n================= KRS_DENSE_SELFTEST =================\n");
        const bool ok = krs::pick::runDenseSceneGateF5();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 GATE J4: joint validation fuzz (random feature x type x extremes -> 0 corrupt).
    if (qEnvironmentVariableIntValue("KRS_JOINTFUZZ_SELFTEST") != 0) {
        std::printf("\n================= KRS_JOINTFUZZ_SELFTEST =================\n");
        const bool ok = krs::cad::runJointFuzzGateJ4();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 GATE M5: MQTT robustness (broker kill/reconnect + N>=128 + malformed payloads).
    if (qEnvironmentVariableIntValue("KRS_MQTTROBUST_SELFTEST") != 0) {
        std::printf("\n================= KRS_MQTTROBUST_SELFTEST =================\n");
        const bool ok = krs::mqtt::runMqttRobustnessGateM5();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase G G.0: standalone PhysX-core lifecycle gate (borrow/release safety).
    if (qEnvironmentVariableIntValue("KRS_SIM_LIFECYCLE_SELFTEST") != 0) {
        std::printf("\n================= KRS_SIM_LIFECYCLE_SELFTEST =================\n");
        const bool ok = SimulationController::runLifecycleSelfTest();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase G GATE H: live SimulationController articulation vs oracle (H1 in G.1).
    if (qEnvironmentVariableIntValue("KRS_ARTIC_LIVE_SELFTEST") != 0) {
        std::printf("\n================= KRS_ARTIC_LIVE_SELFTEST =================\n");
        const bool ok = krs::dyn::runArticulationLiveGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase A GATE D: FANUC sandbox demo stability (pick-and-place over >=1000 cycles).
    if (qEnvironmentVariableIntValue("KRS_DEMO_SELFTEST") != 0) {
        std::printf("\n================= KRS_DEMO_SELFTEST =================\n");
        const bool ok = krs::dyn::runDemoGateD();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase V GATE V (V.1 / V-assign): 17-solid -> serial-link assignment correctness.
    if (qEnvironmentVariableIntValue("KRS_VASSIGN_SELFTEST") != 0) {
        std::printf("\n================= KRS_VASSIGN_SELFTEST =================\n");
        const bool ok = krs::dyn::runVisibleArticGateV();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    if (qEnvironmentVariableIntValue("KRS_OVERNIGHT_BENCH") != 0) {
        std::printf("\n================= KRS_OVERNIGHT_BENCH =================\n");
        struct GateRes { const char* name; bool ok; };
        GateRes g[] = {
            { "Phase A dynamics oracle (FK/M/dyn/IK/loop)", krs::dyn::runSelfTests() },
            { "Phase A articulation gate (A1/A2/A3/A5)",    krs::dyn::runArticulationGate() },
            { "FEM oracle (axial/cantilever/conduction/Kt)", krs::fem::FemSolver::runSelfTests() },
            { "MPM fidelity suite (analytic ground truth)",  m_mpm ? m_mpm->runSelfTests(*this, m_gl) : true },
            { "Adjoint MLS-MPM gradient check (<1e-5)",      krs::mpmad::runSelfTests() },
            { "HIL jitter (1 kHz deterministic loop)",       krs::hil::runJitterSelfTest() },
            { "HIL bridges (camera loopback + CAN)",         krs::hil::runBridgeSelfTest() },
            { "Trajectory HIL multi-fidelity verify",        krs::hil::runTrajectoryHilSelfTest() },
            { "OCCT STEP pipeline (round-trip + features)",  krs::cad::runSelfTest() },
            { "GATE U B-Rep UVs (U1-U6: scale/continuity/density/coverage/param/rides-body)", krs::cad::runUvGateU() },
            { "GATE U applied-tex (AC1 rides/AC2 tiling/AC3 tag-select + neg-ctrls)", runAppliedTextureGate() },
            { "Render gates G1-G9 (colormap/determinism/proj)", runRenderGates() },
            { "GATE V.2 visible FANUC render (features->pixels)", runFanucRenderGateV2() },
            { "SimController lifecycle (PhysX core borrow)", SimulationController::runLifecycleSelfTest() },
            { "GATE C3 dynamic-flip continuity (pose+velocity)", SimulationController::runFlipContinuityGateC3() },
            { "GATE C SDF collider rides body (C2 no-ghost/C4 lag + neg-ctrl)", krs::fluid::runCollisionSyncGateC() },
            { "GATE 0a conservation instrument (2-body momentum + leak neg-ctrl)", krs::integ::runConservationGate0() },
            { "GATE 0b causal-chain instrument (severed-stage localization)", krs::integ::runCausalChainGate0() },
            { "GATE 0c GPU fluid vs moving SDF (no-penetration + ghost neg-ctrl)", runGpuFluidSdfGate() },
            { "GATE 1.2 fluid<->rigid Newton 3rd (impulse==momentum + inert-box neg-ctrl)", runFluidRigidImpulseGate() },
            { "GATE 1.3 artic<->collision (collision xform tracks live FK + stale neg-ctrl)", krs::dyn::runArticCollisionGate1_3() },
            { "GATE 1.4 MPM<->thermal energy conservation (Fourier + energy-leak neg-ctrl)", m_mpm ? m_mpm->runThermalGate1_4(*this, m_gl) : true },
            { "GATE 1.5 FEM static equilibrium (net reaction == applied load + neg-ctrls)", krs::fem::FemSolver::runEquilibriumGate1_5() },
            { "GATE 2 canonical chain cmd->FK->push->cube->fluid (severed-stage localization)", runCanonicalChainGate2() },
            { "GATE 3.1 raycast ray-triangle pick >=99% (AABB-only neg-ctrl)", krs::pick::runRaycastGate3_1() },
            { "GATE F B-Rep selector (ray-pick -> analytic axis/radius <1e-9 + mesh-fit neg-ctrl)", krs::cad::runBRepSelectorGateF() },
            { "GATE J joint tooling (derive revolute frame <1e-6 vs oracle -> RobotArticSpec + reject neg-ctrl)", krs::cad::runJointGateJ() },
            { "GATE M MQTT (real broker; joint cmd->FK->state round-trip <1e-4; broadcast duality)", krs::mqtt::runMqttGateM() },
            { "GATE ND node graph (scene->node->ECS effect + graph->robot + disconnected/type neg-ctrls)", krs::nodes::runNodeGraphGateND() },
            { "GATE F3 disambiguation (small bore vs large face / shared edge / edge-vs-face)", krs::cad::runBRepDisambiguationGateF3() },
            { "GATE F5 dense-scene pick (>=20 bodies/>=100k tris, >=99% at scale + latency)", krs::pick::runDenseSceneGateF5() },
            { "GATE J4 joint fuzz (20k feature x type x extremes -> 0 corrupt graphs)", krs::cad::runJointFuzzGateJ4() },
            { "GATE M5 MQTT robustness (broker kill/reconnect + N>=128 + malformed rejected)", krs::mqtt::runMqttRobustnessGateM5() },
            { "GATE NODE-UI (in-node widget param drives output + bounded footprint)", krs::nodes::runNodeUiGate() },
            { "GATE NODE-LIB (math/signal/time/logic nodes vs closed-form, <tol)", krs::nodes::runNodeLibraryGate() },
            { "GATE NODE-MQTT (publish-node drives live robot over the bus, FK <1e-4)", krs::nodes::runMqttNodeGate() },
            { "GATE C-track (computed torque tracks moving setpoint; soft PD lags)", krs::ctrl::runControllerTrackGate() },
            { "GATE C-knob (goal-knob node drives live joint, FK <1e-4)", krs::ctrl::runControllerKnobGate() },
            { "GATE C-glass (glass robot tracks planned config, not live)", krs::ctrl::runControllerGlassGate() },
            { "GATE NODE-E2E (canvas program drives live robot; severing localizes)", krs::nodes::runNodeE2EGate() },
            { "GATE INPUT-BIND (mounted per-input widget drives node output, N-of-M)", krs::nodes::runInputBindGate() },
            { "GATE TYPE (compatible ports connect, incompatible blocked)", krs::nodes::runTypeGate() },
            { "GATE TIME (live time source drives a sine over wall-clock)", krs::nodes::runTimeGate() },
            { "GATE CONNECT-AND-CONTROL (wired program, widget value, live time -> live robot)", krs::nodes::runConnectControlGate() },
            { "GATE FRAME (every registered node type exposes ports via the real QtNodes model, N of M)", krs::nodes::runFrameGate() },
            { "GATE VIS (readout/gauge displayed value matches input; digits/decimals; disconnected inert)", krs::nodes::runVisGate() },
            { "GATE DEMO-GRAPH (editing the canvas sine changes the live robot; boot graph IS the driver)", krs::nodes::runDemoGraphGate() },
            { "GATE OWNERSHIP (node command sole joint driver, FK<1e-4; no graph->rest; switch-robust)", krs::nodes::runOwnershipGate() },
            { "GATE PID (PID node closes a plant onto a step vs independent reference; P-only retains offset)", krs::nodes::runPidGate() },
            { "GATE FILTER (Kalman/low-pass/moving-average each vs independent reference + neg-ctrl)", krs::nodes::runFilterGate() },
            { "GATE THREAD (async UI edits keep tick rate; old synchronous path stalls it)", krs::nodes::runThreadGate() },
            { "GATE DRAGDROP (catalog drop instances the correct typed node with ports+widgets)", krs::nodes::runDragDropGate() },
            { "GATE H live SERIAL articulation (H1/H2 vs oracle)", krs::dyn::runArticulationLiveGate() },
            { "GATE D FANUC SERIAL demo stability (D1-D4)",        krs::dyn::runDemoGateD() },
            { "GATE V solid->link assignment (V1 + V-assign)",     krs::dyn::runVisibleArticGateV() },
            { "GATE V.6 FANUC boot path moves (shared helper)",    krs::dyn::runFanucBootGateV6() },
        };
        int fails = 0;
        std::printf("\n--------------- OVERNIGHT BENCH DASHBOARD ---------------\n");
        for (const auto& x : g) { std::printf("  [%s] %s\n", x.ok ? "PASS" : "FAIL", x.name); if (!x.ok) ++fails; }
        const int n = int(sizeof(g) / sizeof(g[0]));
        std::printf("  ----- %d / %d gate groups PASS  (rigid KRS_BENCH 7/7 runs separately) -----\n",
                    n - fails, n);
        std::fflush(stdout);
        std::_Exit(fails);
    }

    // Headless MLS-MPM fidelity suite (analytic ground-truth checks).
    if (m_mpm && qEnvironmentVariableIntValue("KRS_MPM_SELFTEST") != 0) {
        m_mpm->runSelfTests(*this, m_gl);
        krs::mpmad::runSelfTests();   // CPU adjoint gradient checks (ADJOINT_GRADIENT_CHECK)
        krs::hil::runJitterSelfTest();// HIL_JITTER (1 kHz deterministic loop)
        krs::hil::runBridgeSelfTest();// LOOPBACK_FRAME_INTEGRITY + CAN round-trip
        krs::hil::runTrajectoryHilSelfTest(); // TRAJECTORY_HIL_LOOP (multi-fidelity verify)
    }

    // Headless OCCT pipeline check (STEP round-trip + B-Rep meshing + cylindrical
    // feature recognition + exact volume). Pure CPU/OCCT, no GL dependency.
    if (qEnvironmentVariableIntValue("KRS_CAD_SELFTEST") != 0) {
        krs::cad::runSelfTest();
    }

    // Phase A: STEP topology recon. KRS_STEP_INSPECT=<path> dumps solids +
    // shared-hinge axis clusters (reveals the FANUC parallelogram loop).
    if (qEnvironmentVariableIsSet("KRS_STEP_INSPECT")) {
        krs::cad::inspectStep(qEnvironmentVariable("KRS_STEP_INSPECT").toStdString());
        std::fflush(stdout);
        std::_Exit(0);   // recon one-shot — dump topology and terminate cleanly
    }

    // Headless FEM oracle check (axial bar, cantilever vs Euler-Bernoulli, 1D bar
    // conduction, plate-with-hole stress concentration). Pure CPU/Eigen, no GL.
    if (qEnvironmentVariableIntValue("KRS_FEM_SELFTEST") != 0) {
        krs::fem::FemSolver::runSelfTests();
    }

    // Phase A GATE A (oracle track): Eigen-native Featherstone self-tests
    // (FK / mass-matrix / dynamics / IK / loop-closure). Pure CPU/Eigen, no GL.
    if (qEnvironmentVariableIntValue("KRS_DYN_SELFTEST") != 0) {
        krs::dyn::runSelfTests();
    }

    // Phase A GATE A (PhysX plant track): PxArticulationReducedCoordinate built
    // from the same spec as the oracle, validated A1/A2/A3/A5 against it.
    if (qEnvironmentVariableIntValue("KRS_ARTIC_SELFTEST") != 0) {
        krs::dyn::runArticulationGate();
    }
}

void RenderingSystem::resizeGLResources()
{
    // Internal targets render at native pixel size * m_renderScale (SSAA);
    // presentViewport downsamples back to native with a linear blit.
    int maxW = 0, maxH = 0;
    for (auto* vp : m_targets.keys()) {
        maxW = std::max(maxW, int(vp->width() * vp->devicePixelRatioF() * m_renderScale));
        maxH = std::max(maxH, int(vp->height() * vp->devicePixelRatioF() * m_renderScale));
    }
    if (maxW == 0 || maxH == 0) return;

    if (m_gBuffer.w != maxW || m_gBuffer.h != maxH) {
        initOrResizeGBuffer(m_gl, maxW, maxH);
        initOrResizePPFBOs(m_gl, maxW, maxH);
    }
    for (auto* vp : m_targets.keys()) {
        auto& target = m_targets[vp];
        int targetW = int(vp->width() * vp->devicePixelRatioF() * m_renderScale);
        int targetH = int(vp->height() * vp->devicePixelRatioF() * m_renderScale);
        if (target.w != targetW || target.h != targetH) {
            initOrResizeFinalFBO(m_gl, target, targetW, targetH);
        }
    }
}

void RenderingSystem::setSimulationPlaying(bool playing)
{
    if (m_fluid) m_fluid->setPlaying(playing);
    if (m_smoke) m_smoke->setPlaying(playing);
    if (m_mpm) m_mpm->setPlaying(playing);
}

void RenderingSystem::resetFluidSimulation()
{
    if (m_fluid) m_fluid->reset();
    if (m_smoke) m_smoke->reset();
    if (m_mpm) m_mpm->reset();
}

void RenderingSystem::requestViewportUpdates()
{
    // Schedule a repaint of every viewport. The actual rendering happens in
    // ViewportWidget::paintGL -> renderViewport(), inside Qt's paint cycle,
    // where external GL commands are sanctioned by the RHI compositor.
    for (auto* vp : m_targets.keys()) {
        if (vp) vp->update();
    }
}

void RenderingSystem::renderAllViewports()
{
    // Runs on the ENGINE context (offscreen). Never touches Qt's RHI context.
    if (!m_isInitialized || m_targets.isEmpty()) return;

    QOpenGLContext* prevCtx = nullptr; QSurface* prevSurf = nullptr;
    if (!makeEngineCurrent(prevCtx, prevSurf)) {
        qWarning() << "[RenderingSystem] Engine context makeCurrent failed!";
        return;
    }
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(m_engineContext);
    if (!m_gl) { doneEngineCurrent(prevCtx, prevSurf); return; }

    // --- Frame stats ---
    // Nanosecond clock: restart() returns integer MILLISECONDS, which
    // quantised dt to 8-vs-9 ms at 120 Hz — animated motion (the orbiting
    // key light) visibly jittered from the rounding alone.
    const float dt = float(double(m_clock.nsecsElapsed()) * 1e-9);
    m_clock.restart();
    m_elapsedSeconds += double(dt);
    m_frameTimeHistory.push_back(dt);
    if (m_frameTimeHistory.size() > m_historySize) m_frameTimeHistory.pop_front();
    if (!m_frameTimeHistory.empty()) {
        float totalTime = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0f);
        float avgDt = totalTime / m_frameTimeHistory.size();
        m_frameTime = avgDt * 1000.0f;
        m_fps = (avgDt > 0) ? (1.0f / avgDt) : 0.0f;
    }
    m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime = dt;

    // Phase A.1b: imported CAD bodies (UVTexturedMaterialTag) get the world-scale UV checker as
    // their albedoMap once -> OpaquePass then selects the UV-texture (gbuffer_textured) path, so
    // the texture rides the body in OBJECT space (no triplanar world-space swimming).
    if (m_cadChecker) {
        auto& reg = m_scene->getRegistry();
        for (auto e : reg.view<UVTexturedMaterialTag, MaterialComponent>()) {
            auto& mat = reg.get<MaterialComponent>(e);
            if (!mat.albedoMap) mat.albedoMap = m_cadChecker;
        }
    }

    processMaterialReloads();
    resizeGLResources();

    // --- GPU stage timing: ring of GL_TIME_ELAPSED queries (engine ctx). ---
    // Write index this frame, read the slot written kGpuQueryRing-1 frames
    // ago — old enough that the result is available without stalling.
    if (!m_gpuQueriesInitialized) {
        m_gl->glGenQueries(kGpuStages * kGpuQueryRing, &m_gpuQueries[0][0]);
        m_gpuQueriesInitialized = true;
    }
    const int qWrite = int(m_gpuQueryFrame % kGpuQueryRing);
    const int qRead = int((m_gpuQueryFrame + 1) % kGpuQueryRing); // oldest slot

    // --- Fluid + gas step (engine context; no-op unless playing) ---
    m_gl->glBeginQuery(GL_TIME_ELAPSED, m_gpuQueries[4][qWrite]);
    if (m_fluid)
        m_fluid->update(*this, m_gl, m_scene->getRegistry(), dt);
    if (m_smoke)
        m_smoke->update(*this, m_gl, m_scene->getRegistry(), dt);
    if (m_mpm)
        m_mpm->update(*this, m_gl, m_scene->getRegistry(), dt);
    // Phase 5: drive the FEM oracle AFTER MPM calibration so its scalar range
    // unions into the shared viz range; publishes nodal fields to FEM bodies.
    if (m_fem && m_mpm)
        m_fem->update(m_scene->getRegistry(), m_gl, m_mpm.get(), int(m_mpm->appearance().mode));
    m_gl->glEndQuery(GL_TIME_ELAPSED);

    if (m_gpuQueryFrame >= kGpuQueryRing - 1) {
        float* dst[kGpuStages] = { &m_gpuTimings.geometryMs, &m_gpuTimings.lightingMs,
                                   &m_gpuTimings.postMs, &m_gpuTimings.overlayMs,
                                   &m_gpuTimings.fluidSimMs };
        for (int s = 0; s < kGpuStages; ++s) {
            GLuint available = 0;
            m_gl->glGetQueryObjectuiv(m_gpuQueries[s][qRead], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 ns = 0;
                m_gl->glGetQueryObjectui64v(m_gpuQueries[s][qRead], GL_QUERY_RESULT, &ns);
                *dst[s] = float(double(ns) * 1e-6); // ns -> ms
            }
        }
    }
    ++m_gpuQueryFrame;

    // --- Geometry pass (once per frame), then per-viewport passes. ---
    // Stage-major order (all lighting, then all post, then all overlays) so
    // each stage sits inside a single timer query; per-viewport ordering
    // within a stage is independent (each writes only its own target).
    m_gl->glBeginQuery(GL_TIME_ELAPSED, m_gpuQueries[0][qWrite]);
    geometryPass();
    m_gl->glEndQuery(GL_TIME_ELAPSED);

    auto validTarget = [this](ViewportWidget* vp) {
        if (!vp) return false;
        const auto& t = m_targets[vp];
        return t.w > 0 && t.h > 0;
    };

    m_gl->glBeginQuery(GL_TIME_ELAPSED, m_gpuQueries[1][qWrite]);
    for (auto* viewport : m_targets.keys())
        if (validTarget(viewport)) lightingPass(viewport);
    m_gl->glEndQuery(GL_TIME_ELAPSED);

    m_gl->glBeginQuery(GL_TIME_ELAPSED, m_gpuQueries[2][qWrite]);
    for (auto* viewport : m_targets.keys())
        if (validTarget(viewport)) postProcessingPass(viewport);
    m_gl->glEndQuery(GL_TIME_ELAPSED);

    m_gl->glBeginQuery(GL_TIME_ELAPSED, m_gpuQueries[3][qWrite]);
    for (auto* viewport : m_targets.keys())
        if (validTarget(viewport)) overlayPass(viewport);
    m_gl->glEndQuery(GL_TIME_ELAPSED);

    // HIL: hand the finished frame to the virtual-camera ring (engine context
    // is current here — GL readback must happen on this thread, not the async
    // sensor consumer). Throttled to 30 Hz internally.
    publishHilCameraFrame();

    // Publish this frame to the widgets: fence guarantees the engine's writes
    // are visible to the RHI contexts before they blit (sync objects are
    // shared across the share group).
    if (m_frameFence) m_gl->glDeleteSync(m_frameFence);
    m_frameFence = m_gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_gl->glFlush();

    doneEngineCurrent(prevCtx, prevSurf);

    // Ask Qt to repaint the viewports; each paintGL calls presentViewport().
    requestViewportUpdates();
}

void RenderingSystem::publishHilCameraFrame()
{
    if (qEnvironmentVariableIntValue("KRS_HIL_CAMERA") == 0) return;
    // Throttle to the sensor frame rate (30 Hz) — far below the render rate.
    if (m_hilCamLastPublish >= 0.0 && (m_elapsedSeconds - m_hilCamLastPublish) < (1.0 / 30.0)) return;

    ViewportWidget* vp = nullptr;                          // first viewport with a finished target
    for (auto* v : m_targets.keys()) {
        const auto& t = m_targets[v];
        if (t.w > 0 && t.h > 0 && t.finalFBO) { vp = v; break; }
    }
    if (!vp) return;
    const auto& t = m_targets[vp];

    if (!m_hilCam || m_hilCamW != t.w || m_hilCamH != t.h) { // (re)open at the current resolution
        if (m_hilCam) m_hilCam->close();
        m_hilCam = krs::hil::makeVirtualCamera("krs_hil_cam");
        if (!m_hilCam->open(t.w, t.h)) { m_hilCam.reset(); return; }
        m_hilCamW = t.w; m_hilCamH = t.h;
        qInfo() << "[HIL] camera bridge open" << t.w << "x" << t.h << "via" << m_hilCam->backendName();
    }

    const size_t px = size_t(t.w) * size_t(t.h);
    m_hilReadF.resize(px * 4); m_hilReadU.resize(px * 4);
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, t.finalFBO);
    m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_FLOAT, m_hilReadF.data()); // VRAM RGBA16F -> sysmem
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // The final texture is the tonemapped display image (~[0,1]); clamp+encode
    // to RGBA8 and flip Y so row 0 is the top scanline (V4L2 / webcam convention).
    for (int y = 0; y < t.h; ++y) {
        const float* src = &m_hilReadF[size_t(t.h - 1 - y) * size_t(t.w) * 4];
        unsigned char* dst = &m_hilReadU[size_t(y) * size_t(t.w) * 4];
        for (int c = 0; c < t.w * 4; ++c) {
            float v = src[c]; v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            dst[c] = (unsigned char)(v * 255.0f + 0.5f);
        }
    }
    m_hilCam->writeFrame(m_hilReadU.data(), m_hilReadU.size(), m_hilFrameId++);
    m_hilCamLastPublish = m_elapsedSeconds;
    if (m_hilFrameId % 30 == 0)
        qInfo() << "[HIL] camera published" << m_hilFrameId << "frames" << t.w << "x" << t.h;
}

void RenderingSystem::presentViewport(ViewportWidget* viewport)
{
    // PRECONDITION: called from ViewportWidget::paintGL with that widget's
    // (RHI compositor) context current. Touch as little GL state as possible
    // and restore what we touch.
    if (!m_isInitialized || !m_targets.contains(viewport)) return;
    auto& target = m_targets[viewport];
    if (target.w <= 0 || target.h <= 0 || target.finalColorTexture == 0) return;

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
    if (!gl) return;

    // Wait (server-side) for the engine frame to be complete.
    if (m_frameFence) gl->glWaitSync(m_frameFence, 0, GL_TIMEOUT_IGNORED);

    // Read-FBO on THIS context wrapping the shared engine-rendered color
    // texture (FBOs are not shared across contexts).
    PresentFBO& pf = m_presentFBOs[viewport];
    if (pf.fbo == 0) gl->glGenFramebuffers(1, &pf.fbo);

    GLint prevReadFbo = 0;
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, pf.fbo);
    // Re-bind + re-attach EVERY frame: cross-context texture writes only
    // become visible to this context when the texture object is re-bound
    // here, and after a resize the texture id can be reused for a new
    // object — a one-time attachment would keep showing the stale first frame.
    GLint prevTex2D = 0;
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);
    gl->glBindTexture(GL_TEXTURE_2D, target.finalColorTexture);
    gl->glBindTexture(GL_TEXTURE_2D, prevTex2D);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, target.finalColorTexture, 0);
    pf.wrappedTexture = target.finalColorTexture;
    gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, viewport->defaultFramebufferObject());
    // Downsample from the (possibly supersampled) engine target to the
    // widget's native pixel size. GL_LINEAR averages when m_renderScale > 1.
    const int dstW = int(viewport->width() * viewport->devicePixelRatioF());
    const int dstH = int(viewport->height() * viewport->devicePixelRatioF());
    gl->glBlitFramebuffer(
        0, 0, target.w, target.h,
        0, 0, dstW, dstH,
        GL_COLOR_BUFFER_BIT,
        (target.w == dstW && target.h == dstH) ? GL_NEAREST : GL_LINEAR
    );

    // Restore the bindings Qt had.
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, viewport->defaultFramebufferObject());
}

void RenderingSystem::onViewportAdded(ViewportWidget* viewport)
{
    if (m_targets.contains(viewport)) return;
    qDebug() << "[RenderingSystem] Viewport added. Engine context:" << m_engineContext;
    m_targets.insert(viewport, TargetFBOs());

    // ALL engine GL initialization happens on the engine's own context, never
    // on the widget's (Qt RHI) context — see renderAllViewports().
    QOpenGLContext* prevCtx = nullptr; QSurface* prevSurf = nullptr;
    if (!makeEngineCurrent(prevCtx, prevSurf)) {
        qCritical() << "[RenderingSystem] Engine context makeCurrent failed in onViewportAdded!";
        return;
    }
    ensureContextIsTracked(m_engineContext);

    if (!m_isInitialized) {
        m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(m_engineContext);
        if (!m_gl) throw std::runtime_error("Failed to get QOpenGLFunctions_4_3_Core");
        initializeSharedResources();
        m_isInitialized = true;
    }
    initializeViewportResources(viewport);

    for (auto ent : m_scene->getRegistry().view<MaterialDirectoryTag>())
        m_scene->getRegistry().emplace_or_replace<MaterialReloadRequest>(ent);
    processMaterialReloads();

    doneEngineCurrent(prevCtx, prevSurf);
}

// Engine-context material (re)loads: the texture browser (or anything else)
// tags an entity with MaterialReloadRequest and the next engine frame loads
// the pack here — uploading new Texture2Ds and destroying the old ones on
// the context that owns them.
void RenderingSystem::processMaterialReloads()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    std::vector<std::pair<entt::entity, MaterialReloadRequest>> pending;
    for (auto ent : reg.view<MaterialDirectoryTag, MaterialReloadRequest>())
        pending.emplace_back(ent, reg.get<MaterialReloadRequest>(ent));
    for (auto [ent, req] : pending) {
        const auto& tag = reg.get<MaterialDirectoryTag>(ent);
        MaterialComponent mat = loadMaterialFromDirectory(tag.dirPath);
        const MaterialComponent* old = reg.try_get<MaterialComponent>(ent);
        if (req.heightScaleOverride >= 0.0f) mat.heightScale = req.heightScaleOverride;
        else if (old && old->heightScale > 0.0f) mat.heightScale = old->heightScale;
        else if (mat.heightMap) mat.heightScale = 0.1f;
        if (req.tilingOverride > 0.0f) mat.albedoTiling = glm::vec2(req.tilingOverride);
        else if (old) mat.albedoTiling = old->albedoTiling;
        reg.emplace_or_replace<MaterialComponent>(ent, std::move(mat));
        reg.remove<MaterialReloadRequest>(ent);
    }

    // Mesh-native (baked) textures queued by SceneBuilder at spawn time.
    std::vector<entt::entity> pendingNative;
    for (auto ent : reg.view<PendingMaterialData>()) pendingNative.push_back(ent);
    for (auto ent : pendingNative) {
        const MeshMaterialSource& src = reg.get<PendingMaterialData>(ent).source;
        MaterialComponent mat;
        auto build = [&](const std::optional<MeshMaterialSource::Map>& map)
            -> std::shared_ptr<Texture2D> {
            if (!map) return nullptr;
            auto tex = std::make_shared<Texture2D>();
            const bool ok = map->bytes.empty()
                ? tex->loadFromFile(map->filePath, map->srgb)
                : tex->loadFromMemory(map->bytes.data(), map->bytes.size(), map->srgb);
            return ok ? tex : nullptr;
        };
        mat.albedoMap = build(src.albedo);
        mat.normalMap = build(src.normal);
        mat.roughnessMap = build(src.roughness);
        mat.metallicMap = build(src.metallic);
        mat.aoMap = build(src.ao);
        mat.emissiveMap = build(src.emissive);
        mat.heightMap = build(src.height);
        if (mat.heightMap) mat.heightScale = 0.05f;
        if (mat.albedoMap)
            reg.emplace_or_replace<MaterialComponent>(ent, std::move(mat));
        reg.remove<PendingMaterialData>(ent);
    }
}

void RenderingSystem::onViewportWillBeDestroyed(ViewportWidget* viewport)
{
    if (!viewport || !m_targets.contains(viewport)) return;
    qDebug() << "[CLEANUP] Releasing GPU resources for viewport" << viewport;
    // The viewport's target FBO lives on the ENGINE context.
    QOpenGLContext* prevCtx = nullptr; QSurface* prevSurf = nullptr;
    if (makeEngineCurrent(prevCtx, prevSurf)) {
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(m_engineContext);
        if (gl) initOrResizeFinalFBO(gl, m_targets[viewport], 0, 0);
        doneEngineCurrent(prevCtx, prevSurf);
    }
    // The present FBO lives on the widget's context and dies with it.
    m_presentFBOs.remove(viewport);
    m_targets.remove(viewport);
}

//==============================================================================
// Pipeline Stage Implementations
//==============================================================================

void RenderingSystem::geometryPass()
{
    // PRECONDITION: the engine context is current (called from
    // renderAllViewports). No makeCurrent/doneCurrent here.
    // 0) Early-out if no size or no viewports
    if (!m_geometryPass || m_gBuffer.w == 0 || m_gBuffer.h == 0 || m_targets.isEmpty()) {
        return;
    }

    auto* vp = m_targets.firstKey();
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
    if (!m_gl) {
        qWarning() << "[geometryPass] Failed to acquire GL functions.";
        return;
    }

    // 2) Bind & verify FBO
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer.fbo);
    GLenum fbStatus = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "[geometryPass] Aborting: FBO not complete! Status:" << fbStatus;
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    auto& target = m_targets[vp];
    // view/projection are REFERENCES in the context: hoist the getter
    // temporaries or they dangle before the pass runs (same UB class as the
    // Round 3 camera bug).
    const glm::mat4 viewMat = camComp.camera.getViewMatrix();
    const glm::mat4 projMat = camComp.camera.getProjectionMatrix(aspect);
    RenderFrameContext context{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        viewMat,
        projMat,
        target,
        m_gBuffer.w,
        m_gBuffer.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        float(m_elapsedSeconds),
        nullptr
    };
    m_geometryPass->execute(context);

    // 7) Unbind & done
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderingSystem::lightingPass(ViewportWidget* viewport)
{
    if (!m_lightingPass) return;
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

    // RenderFrameContext.view/projection are REFERENCES: binding them to
    // the temporaries returned by the camera getters dangles the moment
    // this statement ends (UB — later passes read whatever reuses the
    // stack slot; the fluid pass once read a tint color as a view matrix).
    const glm::mat4 viewMat = camComp.camera.getViewMatrix();
    const glm::mat4 projMat = camComp.camera.getProjectionMatrix(aspect);
    RenderFrameContext ctx{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        viewMat,
        projMat,
        target,
        target.w,
        target.h,
        m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime,
        float(m_elapsedSeconds),
        viewport
    };

    // Let the LightingPass do *its* binding of gPosition,gNormal,gAlbedoSpec
    m_lightingPass->execute(ctx);
}

void RenderingSystem::postProcessingPass(ViewportWidget* viewport)
{
    if (m_postProcessingPasses.empty()) return;

    // GL funcs
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(
        QOpenGLContext::currentContext());
    if (!m_gl) {
        qWarning() << "[postProcessingPass] GL funcs unavailable!";
        return;
    }

    // Resolve target FBOs for this viewport
    auto it = m_targets.find(viewport);
    if (it == m_targets.end()) {
        qWarning() << "[postProcessingPass] No target FBO for viewport";
        return;
    }
    TargetFBOs& target = it.value();
    const auto* ppFBOs = getPPFBOs();

    // --- IMPORTANT: build correct camera + matrices for THIS viewport ---
    Camera& cam = viewport->getCamera();
    const int vpW = target.w;
    const int vpH = target.h;
    const float aspect = (vpH > 0) ? (vpW / float(vpH)) : 1.0f;

    const glm::mat4 view = cam.getViewMatrix();
    const glm::mat4 projection = cam.getProjectionMatrix(aspect);

    // Build the RenderFrameContext ONCE and reuse for each post pass
    RenderFrameContext ctx{
        m_gl,
        m_scene->getRegistry(),
        *this,
        cam,
        view,
        projection,
        target,            // TargetFBOs&
        vpW,
        vpH,
        0.0f,              // deltaTime (if you want)
        float(m_elapsedSeconds),
        viewport
    };

    // Execute each post-processing pass in sequence
    for (size_t i = 0; i < m_postProcessingPasses.size(); ++i) {
        m_postProcessingPasses[i]->execute(ctx);
    }
}


void RenderingSystem::overlayPass(ViewportWidget* viewport)
{
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(
        QOpenGLContext::currentContext());
    if (!m_gl) {
        qWarning() << "[overlayPass] GL funcs unavailable!";
        return;
    }

    auto& target = m_targets[viewport];
    const GLuint finalFBO = target.finalFBO;
    const GLuint gFBO = m_gBuffer.fbo;
    const int   w = target.w;
    const int   h = target.h;

    // --- A) Depth-Only Blit ---
    {
        m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, gFBO);
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, finalFBO);

        // Disable color writes on the DRAW side
        // (for a depth-only blit the color buffer is irrelevant)
        m_gl->glDrawBuffer(GL_NONE);
        m_gl->glReadBuffer(GL_NONE);

        m_gl->glBlitFramebuffer(
            0, 0, w, h,
            0, 0, w, h,
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST
        );

        // Restore the draw buffer so subsequent color draws work
        GLenum buf = GL_COLOR_ATTACHMENT0;
        m_gl->glDrawBuffers(1, &buf);
    }

    // --- B) Prepare for color overlays ---
    {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
        m_gl->glViewport(0, 0, w, h);
        GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
        m_gl->glDrawBuffers(1, drawBufs);
        m_gl->glEnable(GL_DEPTH_TEST);
        m_gl->glDepthMask(GL_TRUE);
    }

    // --- C) Build shared RenderFrameContext ---
    auto& camComp = m_scene->getRegistry().get<CameraComponent>(
        viewport->getCameraEntity());
    float aspect = float(w) / float(h);
    static float elapsed = 0.0f;
    elapsed += m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime;

    // view/projection must be locals: the context stores REFERENCES, and
    // binding them to getter temporaries is a dangling reference for every
    // pass below (this painted the whole screen as 25cm-deep water — the
    // fluid pass was reading the camera's tint color as its view matrix).
    const glm::mat4 viewMat = camComp.camera.getViewMatrix();
    const glm::mat4 projMat = camComp.camera.getProjectionMatrix(aspect);
    RenderFrameContext ctx{
        m_gl,
        m_scene->getRegistry(),
        *this,
        camComp.camera,
        viewMat,
        projMat,
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
        }
    }

    // --- C2) Other overlay passes ---
    for (auto& pass : m_overlayPasses) {
        pass->execute(ctx);
    }

    // --- D) Cleanup ---
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl->glDrawBuffer(GL_BACK);
}

void RenderingSystem::shutdown()
{
    if (!m_isInitialized) return;
    qDebug() << "[LIFECYCLE] Shutting down all GPU resources...";
    if (m_targets.isEmpty()) return;

    // All engine GPU resources live on the engine context.
    QOpenGLContext* prevCtx = nullptr; QSurface* prevSurf = nullptr;
    if (!makeEngineCurrent(prevCtx, prevSurf)) return;
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(m_engineContext);
    if (!gl) { doneEngineCurrent(prevCtx, prevSurf); return; }

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

    if (m_fluid) m_fluid->shutdown(gl);
    if (m_smoke) m_smoke->shutdown(gl);
    if (m_mpm) m_mpm->shutdown(gl);
    if (m_fem) m_fem->shutdown(gl);
    if (m_frameFence) { gl->glDeleteSync(m_frameFence); m_frameFence = nullptr; }
    if (m_gpuQueriesInitialized) {
        gl->glDeleteQueries(kGpuStages * kGpuQueryRing, &m_gpuQueries[0][0]);
        m_gpuQueriesInitialized = false;
    }
    doneEngineCurrent(prevCtx, prevSurf);
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


bool RenderingSystem::hdrEnabled()
{
    // KRS_HDR=0 reverts to the legacy in-lighting Reinhard (bring-up aid).
    static const bool on = qEnvironmentVariable("KRS_HDR") != QLatin1String("0");
    return on;
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
    // Destroy old
    for (int i = 0; i < 2; ++i) {
        if (m_ppFBOs[i].fbo)         gl->glDeleteFramebuffers(1, &m_ppFBOs[i].fbo);
        if (m_ppFBOs[i].colorTexture) gl->glDeleteTextures(1, &m_ppFBOs[i].colorTexture);
        // If you previously added a depthTexture in PostProcessingFBO, delete here too.
    }
    if (w <= 0 || h <= 0) { m_ppFBOs[0] = {}; m_ppFBOs[1] = {}; return; }

    for (int i = 0; i < 2; ++i) {
        auto& pp = m_ppFBOs[i];
        pp.w = w; pp.h = h;

        gl->glGenFramebuffers(1, &pp.fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, pp.fbo);

        // Color
        gl->glGenTextures(1, &pp.colorTexture);
        gl->glBindTexture(GL_TEXTURE_2D, pp.colorTexture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pp.colorTexture, 0);

        // Depth (texture!) � use 32F so we can blit from a depth texture source
        GLuint depthTex = 0;
        gl->glGenTextures(1, &depthTex);
        gl->glBindTexture(GL_TEXTURE_2D, depthTex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);

        GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            qWarning() << "[PPFBO] Incomplete:" << QString("0x%1").arg(uint(status), 0, 16).toUpper();
        }
        else {
            qDebug() << "[PPFBO] OK fbo=" << pp.fbo << " size=(" << w << "x" << h << ")";
        }
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
