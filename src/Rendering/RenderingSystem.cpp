#include "RenderingSystem.hpp"
#include <cmath>
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
#include "JointAxisPass.hpp"
#include "GhostRobotPass.hpp"
#include "FluidPass.hpp"
#include "MpmPass.hpp"
#include "FluidSurfacePass.hpp"
#include "CollisionDebugPass.hpp"
#include "SelectionHighlightPass.hpp"
#include "SelectionService.hpp"   // krs::sel selection-highlight gates
#include "RobotBuilder.hpp"        // krs::rbuild robot-builder gates (Phase 0 recon)
#include "RaycastService.hpp"      // krs::pick nanort BVH ray/mesh picking gate
#include "UvAtlasService.hpp"       // krs::uv xatlas per-body UV unwrap gate
#include "RobotBuilderScene.hpp"   // krs::rbuild demo graph + body->entity render bridge + gate
#include "SelfCollisionMatrix.hpp" // krs::plan self-collision matrix gates
#include "RobotConfig.hpp"         // krs::rcfg property hot-swap + provenance gates
#include "ImuExtrinsics.hpp"       // krs::imu blind IMU extrinsic recovery gates
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
#include "MotionPlanner.hpp"      // OMPL sprint Phase 1 motion-planning gate (krs::plan)
#include "RobotModel.hpp"         // OMPL sprint Phase 4 robot entity + kinematic chain (krs::robot)
#include "PropertyCatalog.hpp"    // avoidance-field Phase 1 ECS->catalog + Object/Property nodes (krs::twin)
#include "AvoidanceField.hpp"     // avoidance-field Phase 2+ emitter / field law / SDF (krs::field)
#include "NodeEditorGate.hpp"     // node-editor front-end gates (krs::nodes INPUT-BIND / TYPE / TIME)
#include "NodeEditQueue.hpp"      // force immediate UI-edit mode for the headless gates
#include "ConnectControlGate.hpp" // node-editor GATE CONNECT-AND-CONTROL
#include "OrbProbe.hpp"           // krs::orb::runOrbOwnershipGate (GATE ORB-OWNERSHIP)
#include "SensorGates.hpp"        // synthetic-sensor pipeline gates (krs::sensor GATE 0 ...)
#include "GraspGates.hpp"         // rigid-body grasp-planning gates (krs::grasp GATE IMPORT ...)
#include "FidelityGates.hpp"      // physics-fidelity validation gates (krs::fidelity HARNESS-SELFTEST ...)
#include "GateOutcome.hpp"        // tri-state PASS/FAIL/SKIP for the overnight-bench dashboard

#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_5_Core>
#include <QDebug>
#include <stdexcept>
#include <cstdlib>   // std::_Exit (headless gates)
#include "LtcMatrices.hpp"   // g_ltc_1 / g_ltc_2 LTC lookup tables (rectangle area lights)
#include <QCoreApplication>
#include <QFile>
#include <QRandomGenerator>
#include <QDateTime>
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

        // Phase 7 ghost validity robot: translucent tinted overlay of the commanded (pre-clamp) pose.
        loadAndStoreShader("ghost",
            (shaderDir + "ghost_vert.glsl").toStdString(),
            (shaderDir + "ghost_frag.glsl").toStdString());

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
        // Live-fluid SDF (GPU Jump-Flooding EDT): seed particles -> grid, then JFA flood.
        loadAndStoreShader("edt_seed", std::vector<std::string>{ (shaderDir + "edt_seed_comp.glsl").toStdString() });
        loadAndStoreShader("edt_jfa", std::vector<std::string>{ (shaderDir + "edt_jfa_comp.glsl").toStdString() });
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
        // Flat grey-room background (Robot View): same vert as the skybox, flat-colour frag.
        loadAndStoreShader("room", (shaderDir + "skybox_vert.glsl").toStdString(), (shaderDir + "room_frag.glsl").toStdString());
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

    // Default Roughness (~0.8 = matte). Was 0.5 (semi-glossy), which let the bright
    // key light cast a broad WHITE specular sheen that lifted G/B and desaturated the
    // albedo (the "red isn't red" wash). A matte default spreads/dims that specular so
    // the multiplicative diffuse colour dominates -> saturated surface colours.
    m_defaultRoughness = std::make_shared<Texture2D>();
    glm::u8vec4 grey(205, 205, 205, 255);
    m_defaultRoughness->generate(1, 1, GL_RGBA8, GL_RGBA, &grey[0]);

    // Default Emissive (Black - not emissive)
    m_defaultEmissive = std::make_shared<Texture2D>();
    m_defaultEmissive->generate(1, 1, GL_RGBA8, GL_RGBA, &black[0]);


    // --- 2) Build IBL resources ---
    // Skipped by the robot-only viewport's RenderingSystem (it borrows the main
    // renderer's baked maps): a 2nd equirect->cubemap bake in a shared GL context
    // renders an all-black cubemap, which corrupted the environment (black robot +
    // black skybox gaps). One bake, shared via adoptEnvironmentFrom().
    if (!m_skipEnvBake)
    {
        QString assetDir = QCoreApplication::applicationDirPath() + QLatin1String("/assets/");

        // Pick an environment HDR: random per boot among the env*.hdr that are
        // actually deployed next to the exe. KRS_ENV=<file> forces a specific one
        // for deterministic tests (e.g. KRS_ENV=env3.hdr).
        QString chosenHdr;
        if (qEnvironmentVariableIsSet("KRS_ENV")) {
            chosenHdr = qEnvironmentVariable("KRS_ENV");
        } else {
            // All three HDRs are good (the earlier "env.hdr bakes black" was a red herring:
            // the real cause was the per-context VAO bug corrupting the equirect->cubemap
            // bake -- fixed in GLUtils::getUnitCubeVAO).
            const QStringList candidates = { QStringLiteral("env.hdr"),
                                             QStringLiteral("env2.hdr"),
                                             QStringLiteral("env3.hdr") };
            QStringList present;
            for (const QString& c : candidates)
                if (QFile::exists(assetDir + c)) present.push_back(c);
            if (present.isEmpty()) present.push_back(QStringLiteral("env.hdr"));
            // Seed a LOCAL generator from the wall clock so the pick varies every
            // boot and can never be pinned by global-RNG state. (Note: if fewer
            // than the full env*.hdr set is deployed next to the exe, 'present'
            // collapses and the pick is necessarily fixed — deploy all three.)
            QRandomGenerator localRng(static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() & 0xffffffffULL));
            chosenHdr = present.at(localRng.bounded(present.size()));
        }
        if (!QFile::exists(assetDir + chosenHdr)) chosenHdr = QStringLiteral("env.hdr");
        qInfo() << "[IBL] Using environment HDR:" << chosenHdr;
        std::string hdrPath = (assetDir + chosenHdr).toStdString();

        auto hdrTex = std::make_shared<Texture2D>();
        if (!hdrTex->loadHDR(hdrPath)) {
            qWarning() << "[IBL] Failed to load HDR env:" << QString::fromStdString(hdrPath);
        }
        else {
            qDebug() << "[IBL] HDR loaded successfully.";

            // Derive the directional "sun" (key light) from the skybox: brightest texel ->
            // direction, surrounding region -> colour/temperature. The IBL already provides the
            // sky-based AMBIENT; this makes the sun match the visible sun in the environment.
            {
                float sd[3], sc[3];
                if (Texture2D::analyzeHdrSun(hdrPath, sd, sc)) {
                    m_sunDirection = glm::vec3(sd[0], sd[1], sd[2]);
                    m_sunColor     = glm::vec3(sc[0], sc[1], sc[2]);
                    qInfo() << "[IBL] sun from skybox -> dir" << sd[0] << sd[1] << sd[2]
                            << " colour" << sc[0] << sc[1] << sc[2];
                }
            }

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

    // --- IRRADIANCE-CORRECT analytic gate (headless) -------------------------------
    // KRS_IRRADIANCE_SELFTEST: convolve a CONSTANT environment of radiance L=1 and
    // assert the baked diffuse irradiance == PI*L == PI (the closed-form irradiance of
    // a uniform hemisphere: E = integral_hemisphere L*cos(theta) dw = PI*L for constant L).
    // The corrected cosine-weighted estimator returns PI; the OLD mis-normalized
    // convolution returns ~PI/2 (the neg-control proving it was wrong).
    if (qEnvironmentVariableIsSet("KRS_IRRADIANCE_SELFTEST")) {
        const QString sdir = shadersRootDir();
        const std::string vsCube = (sdir + "equirect_to_cubemap_vert.glsl").toStdString();
        const std::string fsCube = (sdir + "equirect_to_cubemap_frag.glsl").toStdString();
        const std::string fsIrr  = (sdir + "irradiance_convolution_frag.glsl").toStdString();
        const float L = 1.0f;
        float result = -1.0f;
        unsigned char whitePix[4] = { 255, 255, 255, 255 };   // uniform L=1 equirect source
        auto constTex = std::make_shared<Texture2D>();
        constTex->generate(1, 1, GL_RGBA8, GL_RGBA, whitePix);
        auto cube = Cubemap::fromEquirectangular(*constTex, m_gl, vsCube, fsCube, 64);
        auto irr  = cube     ? Cubemap::convolveIrradiance(*cube, m_gl, vsCube, fsIrr, 32)       : nullptr;
        if (irr) {
            float px[3] = { 0.f, 0.f, 0.f };
            GLuint tfbo = 0;
            m_gl->glGenFramebuffers(1, &tfbo);
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, tfbo);
            m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_CUBE_MAP_POSITIVE_X, irr->getID(), 0);
            if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
                m_gl->glReadPixels(16, 16, 1, 1, GL_RGB, GL_FLOAT, px);
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_gl->glDeleteFramebuffers(1, &tfbo);
            result = px[0];
        }
        const float expected = 3.14159265f * L;   // PI * L
        float err = result - expected; if (err < 0.f) err = -err;
        const bool pass = (result > 0.f) && (err < 0.08f);
        qInfo().noquote() << QString("[IRRADIANCE-CORRECT GATE] uniform env L=%1 -> baked irradiance=%2  expected PI*L=%3  err=%4  %5")
            .arg(L, 0, 'f', 3).arg(result, 0, 'f', 5).arg(expected, 0, 'f', 5).arg(err, 0, 'f', 5).arg(pass ? "PASS" : "FAIL");
        std::_Exit(pass ? 0 : 1);
    }

    // --- LTC lookup tables (rectangle area lights) -------------------------------------
    // Data tables (not GPU-baked), so they load regardless of the environment / skip-bake
    // path. Must use the FLOAT upload (GL_RGBA32F) — the 8-bit generate() would clamp the
    // m11 coefficients (which exceed 1.0) and ruin the transform. The robot-only viewport
    // adopts these via adoptEnvironmentFrom(), but creating them here too is cheap + safe.
    m_ltc1 = std::make_shared<Texture2D>();
    m_ltc1->generateFloat(64, 64, GL_RGBA32F, GL_RGBA, g_ltc_1);
    m_ltc2 = std::make_shared<Texture2D>();
    m_ltc2->generateFloat(64, 64, GL_RGBA32F, GL_RGBA, g_ltc_2);
    qDebug() << "[IBL] LTC LUTs ready. ids:" << m_ltc1->getID() << m_ltc2->getID();

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
    // Sub-feature selection highlight (hover/selected disk+arrow indicators,
    // derived from the gated krs::sel backend). Same HDR/color space as the
    // collision overlay (runs before the tonemap).
    m_overlayPasses.push_back(std::make_unique<SelectionHighlightPass>());
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
    // Ghost validity robot: post-tonemap (display space), depth-tested against the real scene (the
    // G-buffer depth was blitted into the final FBO), depth-write off + alpha blend. Drawn BEFORE the
    // always-on-top axis/gizmo overlays so those still win. Invisible unless a joint is clamped.
    m_overlayPasses.push_back(std::make_unique<GhostRobotPass>());
    // Joint-axis indicator bars: always-on-top, post-tonemap (no exposure pre-divide
    // needed). Before the gizmo so the gizmo stays on absolute top.
    m_overlayPasses.push_back(std::make_unique<JointAxisPass>());
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

    // Live-fluid SDF sprint Phase 1: GPU Jump-Flooding EDT SDF on the REAL live fluid (speed + correct).
    if (qEnvironmentVariableIntValue("KRS_LIVESDF_SELFTEST") != 0) {
        std::printf("\n================= KRS_LIVESDF_SELFTEST =================\n");
        const bool ok = runLiveSdfGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Live-fluid SDF sprint Phase 2: the JFA SDF FOLLOWS live moving water (tracking + ghost neg-ctrl + perf).
    if (qEnvironmentVariableIntValue("KRS_LIVETRACK_SELFTEST") != 0) {
        std::printf("\n================= KRS_LIVETRACK_SELFTEST =================\n");
        const bool ok = runLiveTrackGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Live-fluid SDF sprint Phase 3: the revived field visualizer arrow field encodes the REAL effector field.
    if (qEnvironmentVariableIntValue("KRS_FIELDVIS_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIELDVIS_SELFTEST =================\n");
        const bool ok = runFieldVisualizerGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Live-fluid SDF sprint Phase 4: the velocity-probe orb (volume velocity query + orb<->node lifecycle).
    if (qEnvironmentVariableIntValue("KRS_ORB_SELFTEST") != 0) {
        std::printf("\n================= KRS_ORB_SELFTEST =================\n");
        const bool ok = runOrbVelocityGate() && runOrbLifecycleGate();
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

    // Phase 7 GATE GHOST-VALIDITY: qCommandRaw/validity tracking + ghost FK diverges iff clamped.
    if (qEnvironmentVariableIntValue("KRS_GHOST_SELFTEST") != 0) {
        std::printf("\n================= KRS_GHOST_SELFTEST =================\n");
        const bool ok = krs::robot::runGhostValidityGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 6 GATE 6: occluded-body x-ray cycle (pickMeshAll surfaces all N; pickCycled walks depth).
    if (qEnvironmentVariableIntValue("KRS_XRAY_SELFTEST") != 0) {
        std::printf("\n================= KRS_XRAY_SELFTEST =================\n");
        const bool ok = krs::pick::runXRayGate6();
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

    // SELECTION-HIGHLIGHTS sprint: the VISUAL half (hover/select highlight + indicator
    // render geometry + multi-select), inspectable-at-rest identity/geometry gated.
    if (qEnvironmentVariableIntValue("KRS_SELHL_SELFTEST") != 0) {
        std::printf("\n================= KRS_SELHL_SELFTEST =================\n");
        const bool ok = krs::sel::runHighlightMatchesGate()
                      & krs::sel::runIndicatorGeometryGate()
                      & krs::sel::runMultiSelectGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT BUILDER Phase 0: OCCT assembly-parse recon against the real FANUC STEP.
    if (qEnvironmentVariableIntValue("KRS_PARSERECON_SELFTEST") != 0) {
        std::printf("\n================= KRS_PARSERECON_SELFTEST =================\n");
        const bool ok = krs::rbuild::runParseReconGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // BLIND IMU EXTRINSIC RECOVERY: synthetic IMU + information barrier + blind recovery.
    if (qEnvironmentVariableIntValue("KRS_IMU_SELFTEST") != 0) {
        std::printf("\n================= KRS_IMU_SELFTEST =================\n");
        const bool ok = krs::imu::runImuModelGate()
                      & krs::imu::runInfoBarrierGate()
                      & krs::imu::runExcitationObservGate()
                      & krs::imu::runBlindRecoveryGate()
                      & krs::imu::runHundredsOfTrialsGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT CONFIG Phase 1: self-collision matrix generation + feeds-planner.
    if (qEnvironmentVariableIntValue("KRS_SELFCOL_SELFTEST") != 0) {
        std::printf("\n================= KRS_SELFCOL_SELFTEST =================\n");
        const bool ok = krs::plan::runSelfCollisionMatrixGate()
                      & krs::plan::runSelfCollisionFeedsPlannerGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT CONFIG Phases 2-3: property hot-swap + provenance + edit-op-invoked.
    if (qEnvironmentVariableIntValue("KRS_PROPCFG_SELFTEST") != 0) {
        std::printf("\n================= KRS_PROPCFG_SELFTEST =================\n");
        const bool ok = krs::rcfg::runPropertyHotswapGate()
                      & krs::rcfg::runPropertyProvenanceGate()
                      & krs::rbuild::runEditOpInvokedGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT BUILDER Phase 1: real FANUC assembly -> bodies -> inferred chain (operator-confirm demo).
    if (qEnvironmentVariableIntValue("KRS_AUTOPARSE_SELFTEST") != 0) {
        std::printf("\n================= KRS_AUTOPARSE_SELFTEST =================\n");
        const bool ok = krs::rbuild::runAutoParseReport();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT BUILDER Phases 1-4: auto-parse chain / joint-edit / tag-ownership / subtree-detach.
    if (qEnvironmentVariableIntValue("KRS_ROBOTBUILD_SELFTEST") != 0) {
        std::printf("\n================= KRS_ROBOTBUILD_SELFTEST =================\n");
        const bool ok = krs::rbuild::runAutoParseChainGate()
                      & krs::rbuild::runBaseAxisVerticalGate()
                      & krs::rbuild::runMateSnapGate()
                      & krs::rbuild::runSplitMergeGate()
                      & krs::rbuild::runConnectedComponentsGate()
                      & krs::rbuild::runBoreAnchorGate()
                      & krs::rbuild::runUrdfExportGate()
                      & krs::robot::runManipOpsGate()
                      & krs::robot::runIkPoseGate()
                      & krs::robot::runCutKeepsDrivableGate()
                      & krs::robot::runJointAuthoringSuite()
                      & krs::rbuild::runJointEditGate()
                      & krs::rbuild::runTagOwnershipGate()
                      & krs::rbuild::runMateSelftest()
                      & krs::rbuild::runSubtreeDetachGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // IK-POSE gate in isolation (fast iteration): 6-DoF pose IK + rotate-reorients-in-place.
    if (qEnvironmentVariableIntValue("KRS_IKPOSE_SELFTEST") != 0) {
        std::printf("\n================= KRS_IKPOSE_SELFTEST =================\n");
        const bool ok = krs::robot::runIkPoseGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // MATE-CONNECTOR gate in isolation (fast iteration): persistent body-local mates.
    if (qEnvironmentVariableIntValue("KRS_MATE_SELFTEST") != 0) {
        std::printf("\n================= KRS_MATE_SELFTEST =================\n");
        const bool ok = krs::rbuild::runMateSelftest();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // RAYCAST/PICK gate in isolation (fast iteration): nanort BVH ray/mesh picking.
    if (qEnvironmentVariableIntValue("KRS_PICK_SELFTEST") != 0) {
        std::printf("\n================= KRS_PICK_SELFTEST =================\n");
        const bool ok = krs::pick::runRaycastGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // XATLAS UV-ATLAS gate in isolation (fast iteration): xatlas per-body UV unwrap. (KRS_UV_SELFTEST is
    // already taken by the pre-existing world-scale UV suite, so this uses a distinct env var.)
    if (qEnvironmentVariableIntValue("KRS_XATLAS_SELFTEST") != 0) {
        std::printf("\n================= KRS_XATLAS_SELFTEST =================\n");
        const bool ok = krs::uv::runUvAtlasGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // JOINT-AUTHORING SUITE in isolation (fast iteration): the big body-frame real-path suite alone.
    if (qEnvironmentVariableIntValue("KRS_JOINT_SUITE") != 0) {
        std::printf("\n================= KRS_JOINT_SUITE =================\n");
        const bool ok = krs::robot::runJointAuthoringSuite();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // ROBOT-BUILDER UI sprint: the body->entity RENDER BRIDGE (demo graph bodies
    // become rendered scene entities) + (extended in later phases) the panel / viewport
    // / grab gates. The on-screen pixels are OPERATOR-VISUAL-CONFIRM; these gate the
    // code ADJACENT to the pixels.
    if (qEnvironmentVariableIntValue("KRS_RBUILDUI_SELFTEST") != 0) {
        std::printf("\n================= KRS_RBUILDUI_SELFTEST =================\n");
        const bool ok = krs::rbuild::runRobotBuilderBridgeGate()
                      & krs::rbuild::runRobotBuilderPanelGate()
                      & krs::rbuild::runRobotViewportGate()
                      & krs::rbuild::runRobotSubtreeGrabGate();
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
    // Phase 1 FOUNDATION GATE WIDGET-INPUT: a node's own spin-box value feeds compute when unconnected.
    if (qEnvironmentVariableIntValue("KRS_WIDGETINPUT_SELFTEST") != 0) {
        std::printf("\n================= KRS_WIDGETINPUT_SELFTEST =================\n");
        const bool ok = krs::nodes::runWidgetInputGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Phase 1 FOUNDATION GATE COMBO-INPUT: a node's enum combo selection is read by compute.
    if (qEnvironmentVariableIntValue("KRS_COMBOINPUT_SELFTEST") != 0) {
        std::printf("\n================= KRS_COMBOINPUT_SELFTEST =================\n");
        const bool ok = krs::nodes::runComboInputGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Phase 2 GATE TRIGGER-EDGE: the Button node's brief trigger pulse + edge semantics.
    if (qEnvironmentVariableIntValue("KRS_TRIGGEREDGE_SELFTEST") != 0) {
        std::printf("\n================= KRS_TRIGGEREDGE_SELFTEST =================\n");
        const bool ok = krs::nodes::runTriggerEdgeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Phase 3 GATE IK-SAMPLE/IK-VALID: the IK Target node samples on trigger + solves IK.
    if (qEnvironmentVariableIntValue("KRS_IKSAMPLE_SELFTEST") != 0) {
        std::printf("\n================= KRS_IKSAMPLE_SELFTEST =================\n");
        const bool ok = krs::nodes::runIkSampleGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Phase 4 GATE OMPL: the OMPL Planner node's two-stage PLAN/EXECUTE + in-node params + plan validity.
    if (qEnvironmentVariableIntValue("KRS_OMPL_SELFTEST") != 0) {
        std::printf("\n================= KRS_OMPL_SELFTEST =================\n");
        const bool ok = krs::nodes::runOmplPlannerGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE ORB-OWNERSHIP: the velocity-probe orb node reconciles wire/gizmo/widget radius (no gizmo clobber).
    if (qEnvironmentVariableIntValue("KRS_ORBOWN_SELFTEST") != 0) {
        std::printf("\n================= KRS_ORBOWN_SELFTEST =================\n");
        const bool ok = krs::orb::runOrbOwnershipGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE COMBO-POPUP: a node enum combo (mounted via NodeDelegate) opens a routable QMenu on showPopup().
    if (qEnvironmentVariableIntValue("KRS_COMBOPOPUP_SELFTEST") != 0) {
        std::printf("\n================= KRS_COMBOPOPUP_SELFTEST =================\n");
        const bool ok = krs::nodes::runComboPopupGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Sprint A control-flow gates: WHEN (condition edge -> trigger), IF (route), WHILE (bounded iteration + cap).
    if (qEnvironmentVariableIntValue("KRS_WHEN_SELFTEST") != 0) {
        std::printf("\n================= KRS_WHEN_SELFTEST =================\n");
        const bool ok = krs::nodes::runWhenGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_IF_SELFTEST") != 0) {
        std::printf("\n================= KRS_IF_SELFTEST =================\n");
        const bool ok = krs::nodes::runIfGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_WHILE_SELFTEST") != 0) {
        std::printf("\n================= KRS_WHILE_SELFTEST =================\n");
        const bool ok = krs::nodes::runWhileGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Type-system consolidation GATE TYPE-CONNECT: the unified vector types connect + compute.
    if (qEnvironmentVariableIntValue("KRS_TYPECONNECT_SELFTEST") != 0) {
        std::printf("\n================= KRS_TYPECONNECT_SELFTEST =================\n");
        const bool ok = krs::nodes::runTypeConnectGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Part B math backbone: Transform compose + linear-algebra correctness.
    if (qEnvironmentVariableIntValue("KRS_TFORM_SELFTEST") != 0) {
        std::printf("\n================= KRS_TFORM_SELFTEST =================\n");
        const bool ok = krs::nodes::runTransformComposeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_LINALG_SELFTEST") != 0) {
        std::printf("\n================= KRS_LINALG_SELFTEST =================\n");
        const bool ok = krs::nodes::runLinalgCorrectGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Part C Robot definer: publishes Tier-1 props + the chain is plannable.
    if (qEnvironmentVariableIntValue("KRS_ROBOTPUB_SELFTEST") != 0) {
        std::printf("\n================= KRS_ROBOTPUB_SELFTEST =================\n");
        const bool ok = krs::nodes::runRobotPublishesGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_ROBOTPLAN_SELFTEST") != 0) {
        std::printf("\n================= KRS_ROBOTPLAN_SELFTEST =================\n");
        const bool ok = krs::nodes::runRobotChainPlannableGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // Part D IK: solves over a Robot+Transform, and feeds the OMPL goal.
    if (qEnvironmentVariableIntValue("KRS_IKSOLVE_SELFTEST") != 0) {
        std::printf("\n================= KRS_IKSOLVE_SELFTEST =================\n");
        const bool ok = krs::nodes::runIkSolvesGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_IKFEEDS_SELFTEST") != 0) {
        std::printf("\n================= KRS_IKFEEDS_SELFTEST =================\n");
        const bool ok = krs::nodes::runIkFeedsOmplGate();
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
    // Recon/profile harness for the node-UI+perf sprint (geometry dump + eval cascade cost).
    if (qEnvironmentVariableIntValue("KRS_NODEPROF_SELFTEST") != 0) {
        std::printf("\n================= KRS_NODEPROF_SELFTEST =================\n");
        const bool ok = krs::nodes::runNodeProfileDiag();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE FRAME-GFX: every node type's NodeGraphicsObject has caption + geometry + boundary ports.
    if (qEnvironmentVariableIntValue("KRS_FRAMEGFX_SELFTEST") != 0) {
        std::printf("\n================= KRS_FRAMEGFX_SELFTEST =================\n");
        const bool ok = krs::nodes::runFrameGfxGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE PERF: quiet eval bounded + linear; old per-eval scene cascade blows up.
    if (qEnvironmentVariableIntValue("KRS_PERF_SELFTEST") != 0) {
        std::printf("\n================= KRS_PERF_SELFTEST =================\n");
        const bool ok = krs::nodes::runPerfGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE RATE: eval rate configurable; UI repaint capped independently.
    if (qEnvironmentVariableIntValue("KRS_RATE_SELFTEST") != 0) {
        std::printf("\n================= KRS_RATE_SELFTEST =================\n");
        const bool ok = krs::nodes::runRateGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE HOVER-INTEGRITY: frame background + exec control survive a synthetic hover-enter/leave.
    if (qEnvironmentVariableIntValue("KRS_HOVER_SELFTEST") != 0) {
        std::printf("\n================= KRS_HOVER_SELFTEST =================\n");
        const bool ok = krs::nodes::runHoverIntegrityGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE ZOOM-VISIBLE: every node frame visible at both terminal-zoom bounds (no offscreen-pixmap overflow).
    if (qEnvironmentVariableIntValue("KRS_ZOOM_SELFTEST") != 0) {
        std::printf("\n================= KRS_ZOOM_SELFTEST =================\n");
        const bool ok = krs::nodes::runZoomVisibilityGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }
    // GATE STATIC-CONST: constant math nodes have a value field that sets the emitted constant.
    if (qEnvironmentVariableIntValue("KRS_STATIC_SELFTEST") != 0) {
        std::printf("\n================= KRS_STATIC_SELFTEST =================\n");
        const bool ok = krs::nodes::runStaticConstGate();
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

    // Phase 0 (sensor pipeline) GATE 0: statistical harness self-test + wrong-statistic neg-ctrl + profile round-trip.
    if (qEnvironmentVariableIntValue("KRS_SENSOR0_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR0_SELFTEST =================\n");
        const bool ok = krs::sensor::runStatsHarnessGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 (sensor pipeline) GATE INTRINSICS: K + Brown-Conrady round-trip + pinhole neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_INTRINSICS_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_INTRINSICS_SELFTEST =================\n");
        const bool ok = krs::sensor::runRgbIntrinsicsGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 1 (sensor pipeline) GATE NOISE-STATS: shot+read signal dependence + fixed-Gaussian neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_NOISE_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_NOISE_SELFTEST =================\n");
        const bool ok = krs::sensor::runRgbNoiseStatsGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 2 (sensor pipeline) GATE DEPTH-STRUCT: quadratic Z^2 range + material holes + flying pixels + min-Z.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_DEPTH_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_DEPTH_SELFTEST =================\n");
        const bool ok = krs::sensor::runDepthStructGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 3 (sensor pipeline) GATE IMU-ALLAN: stateful bias -- white slope + instability floor + drift.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_IMU_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_IMU_SELFTEST =================\n");
        const bool ok = krs::sensor::runImuAllanGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 4 (sensor pipeline) GATE L2-UNCERTAINTY: recon belief field -- sigma contrast + hole co-location + calibration.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_L2_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_L2_SELFTEST =================\n");
        const bool ok = krs::sensor::runL2UncertaintyGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 5 (sensor pipeline) GATE COMPOSE: L1 true / L2 belief / L3 live -- shared correlation + toggles + determinism.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_COMPOSE_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_COMPOSE_SELFTEST =================\n");
        const bool ok = krs::sensor::runComposeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 6 (sensor pipeline) GATE E2E: one scene -> RGB+depth+IMU streams, each passing its gate in-context.
    if (qEnvironmentVariableIntValue("KRS_SENSOR_E2E_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_E2E_SELFTEST =================\n");
        const bool ok = krs::sensor::runE2EGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Phase 6 (sensor pipeline) GATE REAL-TRANSFER: transfer harness vs a SECOND SYNTHETIC instance (NOT real hardware).
    if (qEnvironmentVariableIntValue("KRS_SENSOR_TRANSFER_SELFTEST") != 0) {
        std::printf("\n================= KRS_SENSOR_TRANSFER_SELFTEST =================\n");
        const bool ok = krs::sensor::runRealTransferGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 0 GATE IMPORT: YCB load + real-meter scale + mass/inertia + NaN; x1000 neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_GRASP_IMPORT_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_IMPORT_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspImportGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 1 GATE COACD: concavity-preserving collider (ball rests inside bowl) vs convex-hull filler.
    if (qEnvironmentVariableIntValue("KRS_GRASP_COACD_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_COACD_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspCoacdGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 2 GATE SUCCESS-CRITERION: good grasp passes / bad fails (locked) + softened-world anti-cheat.
    if (qEnvironmentVariableIntValue("KRS_GRASP_SUCCESS_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_SUCCESS_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspSuccessGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 3 GATE PLANNER: antipodal heuristic raises the success rate baseline->tuned; random neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_GRASP_PLANNER_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_PLANNER_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspPlannerGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 4 GATE FAILURE-CATALOG: classify 100% of tuned-planner failures; incomplete-taxonomy neg-ctrl.
    if (qEnvironmentVariableIntValue("KRS_GRASP_FAILCAT_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_FAILCAT_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspFailureCatalogGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline GATE COACD-REAL: CoACD preserves grasp-relevant concavities that V-HACD FILLS (discriminating).
    if (qEnvironmentVariableIntValue("KRS_GRASP_COACDREAL_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_COACDREAL_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspCoacdRealGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline GATE REMEASURE: V-HACD vs CoACD success rate, same grasps + LOCKED criterion (apples-to-apples).
    if (qEnvironmentVariableIntValue("KRS_GRASP_REMEASURE_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_REMEASURE_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspRemeasureGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 1 (V2) GATE HEURISTIC-V2: improved planner vs V1 on YCB; targeted failure modes drop.
    if (qEnvironmentVariableIntValue("KRS_GRASP_HEURV2_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_HEURV2_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspHeuristicV2Gate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 2 GATE FILTER: GSO validity+graspability filter (standalone -- the large-dataset gates
    // load hundreds of meshes, too slow for the overnight bench).
    if (qEnvironmentVariableIntValue("KRS_GRASP_FILTER_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_FILTER_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspFilterGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Grasp pipeline Phase 3+4 GATE GENERALIZE+TAXONOMY-SCALE: fixed Heuristic-V2 over the valid GSO library
    // (standalone -- grasps hundreds of objects).
    if (qEnvironmentVariableIntValue("KRS_GRASP_GENERALIZE_SELFTEST") != 0) {
        std::printf("\n================= KRS_GRASP_GENERALIZE_SELFTEST =================\n");
        const bool ok = krs::grasp::runGraspGeneralizeGate();
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 1);
    }

    // Physics-fidelity validation harness: each gate is a canonical experiment vs its known analytic answer.
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelitySelftestGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_CONTACT_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_CONTACT_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityContactGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_FRICTION_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_FRICTION_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityFrictionGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_UNBOUNDED_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_UNBOUNDED_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityUnboundedGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_FLUID_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_FLUID_SELFTEST =================\n");
        DfsphBackend fluidProbe;                       // CPU SPH fidelity probe (no GL needed)
        const bool ok = fluidProbe.runFluidFidelity();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_CANTILEVER_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_CANTILEVER_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityCantileverGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_THERMAL_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_THERMAL_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityThermalGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (m_mpm && qEnvironmentVariableIntValue("KRS_FIDELITY_REPOSE_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_REPOSE_SELFTEST =================\n");
        const bool ok = m_mpm->runReposeFidelity(*this, m_gl);
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_FIDELITY_SCORECARD_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIDELITY_SCORECARD_SELFTEST =================\n");
        const bool ok = krs::fidelity::runFidelityScorecardGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // OMPL sprint Phase 1: motion planning + collision checking (PLAN-COLLISION-FREE/
    // LIMITS/CONNECTIVITY/DETERMINISM + fuzz/profile). Pure CPU (Eigen + OMPL), no GL.
    if (qEnvironmentVariableIntValue("KRS_PLANNING_SELFTEST") != 0) {
        std::printf("\n================= KRS_PLANNING_SELFTEST =================\n");
        const bool ok = krs::plan::runPlanningGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // OMPL sprint Phase 2: execute the planned path through the computed-torque
    // controller; tracking/collision-free/limits, soft-PD lag neg-control. Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_EXECUTE_SELFTEST") != 0) {
        std::printf("\n================= KRS_EXECUTE_SELFTEST =================\n");
        const bool ok = krs::plan::runExecuteGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // OMPL sprint Phase 3: sub-feature selection backend (ray -> exact B-Rep face
    // params + indicator geometry; small-bore disambiguation). OCCT, no GL needed.
    if (qEnvironmentVariableIntValue("KRS_SUBFEAT_SELFTEST") != 0) {
        std::printf("\n================= KRS_SUBFEAT_SELFTEST =================\n");
        const bool ok = krs::cad::runSubFeatSelectionGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // OMPL sprint Phase 4: robot entity + kinematic chain data model (owned-DOF
    // chain, joint-from-feature, typed mount port, lossless export). Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_ROBOTCHAIN_SELFTEST") != 0) {
        std::printf("\n================= KRS_ROBOTCHAIN_SELFTEST =================\n");
        const bool ok = krs::robot::runRobotChainGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Robot-first-class foundation: the LiveRobot owner (q = single source of truth),
    // instantiate-from-schema factory, Robot-FK viz, command-bus routing. Headless.
    if (qEnvironmentVariableIntValue("KRS_ROBOTOWNER_SELFTEST") != 0) {
        std::printf("\n================= KRS_ROBOTOWNER_SELFTEST =================\n");
        const bool ok = krs::robot::runRobotOwnerGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // OMPL sprint Phase 5: E2E -- robot defined via chain -> planned -> executed;
    // every stage's number asserted; severing any stage localizes the break. Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_E2E_SELFTEST") != 0) {
        std::printf("\n================= KRS_E2E_SELFTEST =================\n");
        const bool ok = krs::plan::runE2EGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Avoidance-field Phase 1: ECS->catalog publishing + Object/Property nodes +
    // stale-aware frequency. Pure CPU, no broker.
    if (qEnvironmentVariableIntValue("KRS_QUATOUT_SELFTEST") != 0) {
        std::printf("\n================= KRS_QUATOUT_SELFTEST =================\n");
        const bool ok = krs::twin::runQuaternionOutputGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }
    if (qEnvironmentVariableIntValue("KRS_TWIN_SELFTEST") != 0) {
        std::printf("\n================= KRS_TWIN_SELFTEST =================\n");
        const bool ok = krs::twin::runTwinGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Avoidance-field Phase 2: emitter node (avoidance-field + substance emission by type). Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_EMITTER_SELFTEST") != 0) {
        std::printf("\n================= KRS_EMITTER_SELFTEST =================\n");
        const bool ok = krs::field::runEmitterGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Avoidance-field Phase 3: dynamics-driven field-amplitude law (the can't-fake ordering). Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_FIELDLAW_SELFTEST") != 0) {
        std::printf("\n================= KRS_FIELDLAW_SELFTEST =================\n");
        const bool ok = krs::field::runFieldLawGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Avoidance-field Phase 4: particle grid-SDF (distance + gradient + dynamics scaling + perf). Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_SDF_SELFTEST") != 0) {
        std::printf("\n================= KRS_SDF_SELFTEST =================\n");
        const bool ok = krs::field::runSdfGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    // Avoidance-field Phase 4.5: SDF uncertainty + reaction tempering + temporal coherence. Pure CPU.
    if (qEnvironmentVariableIntValue("KRS_UNCERTAINTY_SELFTEST") != 0) {
        std::printf("\n================= KRS_UNCERTAINTY_SELFTEST =================\n");
        const bool ok = krs::field::runUncertaintyGate();
        std::fflush(stdout); std::_Exit(ok ? 0 : 1);
    }

    if (qEnvironmentVariableIntValue("KRS_OVERNIGHT_BENCH") != 0) {
        std::printf("\n================= KRS_OVERNIGHT_BENCH =================\n");
        // ok is a krs::gate::GateOutcome: each gate's bool implicitly converts, and a gate that
        // calls krs::gate::skip() (missing environmental prerequisite) reports SKIP, not FAIL.
        struct GateRes { const char* name; krs::gate::GateOutcome ok; };
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
            { "GATE LIVE-SDF (GPU Jump-Flooding EDT on the REAL live fluid: <15ms vs brute-force baseline + distance/gradient vs analytic; shifted-grid neg-ctrl)", runLiveSdfGate() },
            { "GATE LIVE-TRACK (JFA SDF follows live falling water frame-to-frame: zero-crossing tracks vs baked-once ghost neg-ctrl; full gen+readback path <15ms)", runLiveTrackGate() },
            { "GATE VISUALIZER-DATA (revived arrow field: real arrow_field_compute vectors == analytic effector field at each arrow; stale-field neg-ctrl mismatches)", runFieldVisualizerGate() },
            { "GATE ORB-VELOCITY (volume containment velocity query: synthetic exact + global-avg neg-ctrl + REAL fluid off-stream->0)", runOrbVelocityGate() },
            { "GATE ORB-LIFECYCLE (orb<->node binding: N orbs N colours; node-delete removes orb; orb-delete exposes node; leak neg-ctrl)", runOrbLifecycleGate() },
            { "GATE ORB-OWNERSHIP (probe radius reconciles wire/gizmo/widget; gizmo resize persists; old clobber-every-tick neg-ctrl)", krs::orb::runOrbOwnershipGate() },
            { "GATE 1.2 fluid<->rigid Newton 3rd (impulse==momentum + inert-box neg-ctrl)", runFluidRigidImpulseGate() },
            { "GATE 1.3 artic<->collision (collision xform tracks live FK + stale neg-ctrl)", krs::dyn::runArticCollisionGate1_3() },
            { "GATE 1.4 MPM<->thermal energy conservation (Fourier + energy-leak neg-ctrl)", m_mpm ? m_mpm->runThermalGate1_4(*this, m_gl) : true },
            { "GATE 1.5 FEM static equilibrium (net reaction == applied load + neg-ctrls)", krs::fem::FemSolver::runEquilibriumGate1_5() },
            { "GATE 2 canonical chain cmd->FK->push->cube->fluid (severed-stage localization)", runCanonicalChainGate2() },
            { "GATE 3.1 raycast ray-triangle pick >=99% (AABB-only neg-ctrl)", krs::pick::runRaycastGate3_1() },
            { "GATE 6 x-ray cycle (pickMeshAll surfaces all occluded bodies; pickCycled walks depth+wraps; nearest-pick-stuck neg-ctrl)", krs::pick::runXRayGate6() },
            { "GATE GHOST-VALIDITY (qCommandRaw + jointValid tracked; ghost FK diverges iff a joint clamps; ghost-from-clamped-q neg-ctrl shows nothing)", krs::robot::runGhostValidityGate() },
            { "GATE F B-Rep selector (ray-pick -> analytic axis/radius <1e-9 + mesh-fit neg-ctrl)", krs::cad::runBRepSelectorGateF() },
            { "GATE SUBFEAT (selection backend: ray->exact B-Rep params <1e-9 + indicator-geometry on-surface + small-bore disambiguation; miss & centroid neg-ctrls)", krs::cad::runSubFeatSelectionGate() },
            { "GATE HIGHLIGHT-MATCHES (stored hover/selected feature == ray-resolved feature; neighbour & dominant-face highlight neg-ctrls)", krs::sel::runHighlightMatchesGate() },
            { "GATE INDICATOR-GEOMETRY (rendered disk/arrow == analytic feature <1e-4, derived from backend; wrong-feature & axis/radius-mismatch neg-ctrls)", krs::sel::runIndicatorGeometryGate() },
            { "GATE MULTI-SELECT (small-bore-on-large-part resolves to bore; set accumulates; dominant-resolver & non-accumulating-commit neg-ctrls)", krs::sel::runMultiSelectGate() },
            { "GATE PARSE-RECON (OCCT STEPCAF recovers FANUC part tree + placements; mates absent -> infer from geometry; real assembly)", krs::rbuild::runParseReconGate() },
            { "GATE AUTO-PARSE-CHAIN (inferred joint axes == interface geometry; FK==parsed placements; ambiguous/offset/planar NOT faked; wrong-axis neg-ctrl)", krs::rbuild::runAutoParseChainGate() },
            { "GATE BASE-AXIS-VERTICAL (J0 base-turntable axis = vertical part-Z, not the horizontal flange decoy; horizontal-coaxial-pair neg-ctrl)", krs::rbuild::runBaseAxisVerticalGate() },
            { "GATE MATE-SNAP (concentric transform aligns child bore to parent axis; subtreeOf collects sub-assembly; off-axis-before neg-ctrl)", krs::rbuild::runMateSnapGate() },
            { "GATE SPLIT-MERGE (cut joint -> base+branch trees; re-mate merges; DOF/body/FK round-trip; bad-index neg-ctrl)", krs::rbuild::runSplitMergeGate() },
            { "GATE CONNECTED-COMPONENTS (robot = derived component; serial->1 / disjoint->2; chooseBase deterministic; chainOrderFrom re-roots)", krs::rbuild::runConnectedComponentsGate() },
            { "GATE BORE-ANCHOR (lowered revolute rotates about the bore axis/axisPos, not the child link CAD origin; offset-bore neg-ctrl)", krs::rbuild::runBoreAnchorGate() },
            { "GATE URDF-EXPORT (base-pick + tree-search -> URDF; links/joints/types/limits; re-rootable; bad-base-empty neg-ctrl)", krs::rbuild::runUrdfExportGate() },
            { "GATE MANIP-OPS (rigid translate moves all links; IK converges+clamps; split->2 robots; merge->1; unreachable-IK neg-ctrl)", krs::robot::runManipOpsGate() },
            { "GATE IK-POSE (6-DoF pose IK reaches pos+orient; rotate reorients the EE in place not a ghost circle; position-only neg-ctrls)", krs::robot::runIkPoseGate() },
            { "GATE CUT-KEEPS-DRIVABLE (cut a joint -> two components; both keep names+ids; both drivable by name; cut DOF gone; nothing destroyed)", krs::robot::runCutKeepsDrivableGate() },
            { "JOINT-AUTHORING SUITE (body-frame real-path: gizmo-route/IK-drag/define-snap/no-self-joint/persistence/cut-count/merge/coaxial/joint-server/FIFO/concentric-both)", krs::robot::runJointAuthoringSuite() },
            { "GATE JOINT-EDIT (manual joint from selected bores matches analytic frame; chain re-derives DOF; degenerate-pair neg-ctrl)", krs::rbuild::runJointEditGate() },
            { "GATE TAG-OWNERSHIP (member body tagged + free-move-locked; non-member free; always-allow neg-ctrl breaks single-owner)", krs::rbuild::runTagOwnershipGate() },
            { "GATE SUBTREE-DETACH (mid-joint delete detaches subtree intact; tag tracks membership; re-mate restores; destroy & stale-tag neg-ctrls)", krs::rbuild::runSubtreeDetachGate() },
            { "GATE MATE-CONNECTOR (body-local mates survive dynamic move/far-snap/save-load-open/delete; faceKey re-anchors; world-anchor stale neg-ctrl)", krs::rbuild::runMateSelftest() },
            { "GATE RAYCAST (nanort BVH ray/mesh pick hits the right face; honors tMin/tMax; misses cleanly; oblique rays; rim-snap nearest)", krs::pick::runRaycastGate() },
            { "GATE UV-ATLAS (xatlas unwrap preserves triangle topology + triFace map; UVs in [0,1]; charts packed; deterministic; degenerate rejected)", krs::uv::runUvAtlasGate() },
            { "GATE SELFCOLLISION-MATRIX (classify pairs vs brute-force GT; SOMETIMES pairs never disabled; ALWAYS/NEVER disabled; density-monotone)", krs::plan::runSelfCollisionMatrixGate() },
            { "GATE SELFCOLLISION-FEEDS-PLANNER (validity skips disabled but catches a kept pair's collision; buggy-disable-real-pair neg-ctrl misses it)", krs::plan::runSelfCollisionFeedsPlannerGate() },
            { "GATE PROPERTY-HOTSWAP (limit edit propagates LIVE to the planner's limits; stale-cache neg-ctrl mis-judges)", krs::rcfg::runPropertyHotswapGate() },
            { "GATE PROPERTY-PROVENANCE (axes geometry-derived, limits user-supplied; fabricated-value & user-claimed-axis neg-ctrls flagged)", krs::rcfg::runPropertyProvenanceGate() },
            { "GATE EDIT-OP-INVOKED (panel controls invoke proven delete/define ops + chain re-derives; no-op & wrong-op neg-ctrls)", krs::rbuild::runEditOpInvokedGate() },
            { "GATE IMU-MODEL-CORRECT (noiseless readings match closed-form; lever-arm centripetal; leverless-model neg-ctrl)", krs::imu::runImuModelGate() },
            { "GATE INFORMATION-BARRIER (blind recovery matches sealed truth; zeroed/no-motion garbage input FAILS -> uses physics not leaked truth)", krs::imu::runInfoBarrierGate() },
            { "GATE EXCITATION-OBSERVABILITY (all 6 mount DOF observable under excitation; degenerate non-rotating leaves position under-observable)", krs::imu::runExcitationObservGate() },
            { "GATE BLIND-RECOVERY+NOISE-ROBUST (recovered mount matches sealed truth <tol; centred under noise; identity & no-bias neg-ctrls)", krs::imu::runBlindRecoveryGate() },
            { "GATE IMU-HUNDREDS-OF-TRIALS (hundreds of random link/pose trials match sealed truth; success rate + honest failure characterisation)", krs::imu::runHundredsOfTrialsGate() },
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
            { "GATE PLAN (OMPL RRTConnect/RRTstar over SerialChain: collision-free/limits/connectivity/determinism + straight-line & boxed-in neg-ctrls)", krs::plan::runPlanningGate() },
            { "GATE EXECUTE (planned path run through computed-torque under gravity: tracks/collision-free/limits; soft-PD lag + colliding-ref + 3x-fast neg-ctrls)", krs::plan::runExecuteGate() },
            { "GATE ROBOT-CHAIN (entity owns links+joints+base+mount: owned-DOF chain/joint-from-feature/typed-mount-port/lossless-export; non-member & non-coaxial & mismatched-type & corrupt-export neg-ctrls)", krs::robot::runRobotChainGate() },
            { "GATE E2E (robot defined-via-chain -> planned -> executed; every stage asserted; severing define/plan/execute localizes the break)", krs::plan::runE2EGate() },
            { "GATE TWIN (ECS->catalog introspection + Object/Property nodes value-fidelity + stale-aware frequency; non-existent-obj & phantom-prop & disconnected & frozen-Hz neg-ctrls)", krs::twin::runTwinGate() },
            { "GATE QUATERNION-OUTPUT (rigid-body Property node quaternion output matches orientation; bakes into a Transform losslessly; stale-quat neg-ctrl)", krs::twin::runQuaternionOutputGate() },
            { "GATE EMITTER (avoidance-field emission magnitude/sign via FieldSolver + substance origin/rate/follow + type-switch; zero-amp & disconnected & invalid-type neg-ctrls)", krs::field::runEmitterGate() },
            { "GATE FIELD-LAW (dynamics-driven amplitude ordering accel>const>decel>static + authorable + law->emitter pipe; geometry-only-fails-ordering & unconnected-weight neg-ctrls)", krs::field::runFieldLawGate() },
            { "GATE SDF (particle grid-SDF distance vs analytic + away-gradient + dynamics-scaling + interactive-perf; empty-SDF & flat-gradient & geometry-only neg-ctrls)", krs::field::runSdfGate() },
            { "GATE UNCERTAINTY (variance drops with observation + reaction tempered by uncertainty + temporally-stable gradient; blind-model & no-temper & raw-jitter neg-ctrls)", krs::field::runUncertaintyGate() },
            { "GATE C-track (computed torque tracks moving setpoint; soft PD lags)", krs::ctrl::runControllerTrackGate() },
            { "GATE C-knob (goal-knob node drives live joint, FK <1e-4)", krs::ctrl::runControllerKnobGate() },
            { "GATE C-glass (glass robot tracks planned config, not live)", krs::ctrl::runControllerGlassGate() },
            { "GATE NODE-E2E (canvas program drives live robot; severing localizes)", krs::nodes::runNodeE2EGate() },
            { "GATE INPUT-BIND (mounted per-input widget drives node output, N-of-M)", krs::nodes::runInputBindGate() },
            { "GATE WIDGET-INPUT (typed spin-box value feeds compute when unconnected: 3+4->7, wire 10->13; old spin-box-ignored neg-ctrl)", krs::nodes::runWidgetInputGate() },
            { "GATE COMBO-INPUT (enum combo selection read by compute: Add/Sub/Mul switches math_op; old combo-ignored neg-ctrl)", krs::nodes::runComboInputGate() },
            { "GATE COMBO-POPUP (node enum combo mounts a ProxyComboBox -> showPopup opens a routable QMenu + selection drives the index; old bare-combo neg-ctrl)", krs::nodes::runComboPopupGate() },
            { "GATE TRIGGER-EDGE (Button brief pulse: rising-on-press/falling-on-release/dual; level + wrong-edge neg-ctrl)", krs::nodes::runTriggerEdgeGate() },
            { "GATE IK-SAMPLE (IK Target samples-on-trigger + holds; FK(goal)==target, unreachable graceful; continuous-track + wrong-soln neg-ctrls)", krs::nodes::runIkSampleGate() },
            { "GATE OMPL (two-stage PLAN freezes path w/o moving + EXECUTE drives to goal + no-plan graceful; in-node planner/iters/waypoints params change the plan; planned path collision-free, straight-line-through-box collides)", krs::nodes::runOmplPlannerGate() },
            { "GATE WHEN-FIRES-ON-CONDITION-EDGE (When fires once on the condition's false->true edge; level + always-fire neg-ctrls; Compare drives the condition)", krs::nodes::runWhenGate() },
            { "GATE IF-ROUTES (If routes a trigger to True/False by the condition, exactly one branch; both-fire + inverted-condition neg-ctrls)", krs::nodes::runIfGate() },
            { "GATE WHILE-ITERATES-AND-TERMINATES (While fires the exact count then terminates; for-N exact; the mandatory max-iter cap catches an infinite loop; no-cap + wrong-count neg-ctrls)", krs::nodes::runWhileGate() },
            { "GATE TYPE (compatible ports connect, incompatible blocked)", krs::nodes::runTypeGate() },
            { "GATE TYPE-CONNECT (consolidated vectors connect+compute: Compose(Vec3f)->Dot(glm::vec3)=32; old ids rejected; vector->Scalar/Boolean still rejected)", krs::nodes::runTypeConnectGate() },
            { "GATE TRANSFORM-COMPOSE (quat-native compose matches closed form; inverse==identity; wrong-order neg-ctrl)", krs::nodes::runTransformComposeGate() },
            { "GATE LINALG-CORRECT (dot=32/cross/scale/magnitude/transpose/inverse vs closed form; sum-not-MAC dot neg-ctrl)", krs::nodes::runLinalgCorrectGate() },
            { "GATE ROBOT-PUBLISHES (Robot node Tier-1 kinematic props match state, readable via Object->Property; base not a DOF; phantom-prop & sensed-Tier-2 neg-ctrls)", krs::nodes::runRobotPublishesGate() },
            { "GATE ROBOT-CHAIN-IS-PLANNABLE (OMPL plans over the Robot ref's owned joints; base + non-member excluded; buggy-include neg-ctrl)", krs::nodes::runRobotChainPlannableGate() },
            { "GATE IK-SOLVES (IK over Robot ref + Target Transform reaches the EE target; unreachable graceful; wrong-soln neg-ctrl)", krs::nodes::runIkSolvesGate() },
            { "GATE IK-FEEDS-OMPL (Transform->IK->goal->OMPL path; joint_config connects, type-mismatch rejected)", krs::nodes::runIkFeedsOmplGate() },
            { "GATE TIME (live time source drives a sine over wall-clock)", krs::nodes::runTimeGate() },
            { "GATE CONNECT-AND-CONTROL (wired program, widget value, live time -> live robot)", krs::nodes::runConnectControlGate() },
            { "GATE FRAME (every registered node type exposes ports via the real QtNodes model, N of M)", krs::nodes::runFrameGate() },
            { "GATE VIS (readout/gauge displayed value matches input; digits/decimals; disconnected inert)", krs::nodes::runVisGate() },
            { "GATE DEMO-GRAPH (editing the canvas sine changes the live robot; boot graph IS the driver)", krs::nodes::runDemoGraphGate() },
            { "GATE OWNERSHIP (node command sole joint driver, FK<1e-4; no graph->rest; switch-robust)", krs::nodes::runOwnershipGate() },
            { "GATE DRIVE-BY-NAME (joint addressable by name+nodeId via JointNameRegistry; follows name across rename; node drives the named DOF)", krs::nodes::runDriveByNameGate() },
            { "GATE PID (PID node closes a plant onto a step vs independent reference; P-only retains offset)", krs::nodes::runPidGate() },
            { "GATE FILTER (Kalman/low-pass/moving-average each vs independent reference + neg-ctrl)", krs::nodes::runFilterGate() },
            { "GATE THREAD (async UI edits keep tick rate; old synchronous path stalls it)", krs::nodes::runThreadGate() },
            { "GATE DRAGDROP (catalog drop instances the correct typed node with ports+widgets)", krs::nodes::runDragDropGate() },
            { "GATE FRAME-GFX (every type's NodeGraphicsObject has caption+geometry+boundary ports)", krs::nodes::runFrameGfxGate() },
            { "GATE PERF (quiet eval bounded+linear; old per-eval scene cascade blows up)", krs::nodes::runPerfGate() },
            { "GATE RATE (eval rate configurable; UI repaint capped independently)", krs::nodes::runRateGate() },
            { "GATE HOVER-INTEGRITY (frame bg + exec control survive hover-enter/leave; no WA_Translucent)", krs::nodes::runHoverIntegrityGate() },
            { "GATE ZOOM-VISIBLE (every node NoCache+no-effect; frame paints at 0.3x/2x terminal zoom)", krs::nodes::runZoomVisibilityGate() },
            { "GATE STATIC-CONST (constant nodes' value field sets the emitted constant; matrix deferred)", krs::nodes::runStaticConstGate() },
            { "SENSOR GATE 0 (stats harness self-test + wrong-statistic neg-ctrl + profile round-trip)", krs::sensor::runStatsHarnessGate() },
            { "SENSOR GATE INTRINSICS (K + Brown-Conrady round-trip <0.5px; pinhole neg-ctrl fails at edges)", krs::sensor::runRgbIntrinsicsGate() },
            { "SENSOR GATE NOISE-STATS (shot+read variance scales with signal; fixed-Gaussian neg-ctrl flat)", krs::sensor::runRgbNoiseStatsGate() },
            { "SENSOR GATE DEPTH-STRUCT (quadratic Z^2 range + material holes + flying pixels + min-Z; clean+Gaussian neg-ctrl)", krs::sensor::runDepthStructGate() },
            { "SENSOR GATE IMU-ALLAN (stateful: white slope + bias-instability floor + integrated drift; per-sample-Gaussian neg-ctrl)", krs::sensor::runImuAllanGate() },
            { "SENSOR GATE L2-UNCERTAINTY (recon sigma contrast + hole co-location + calibration; uniform-sigma neg-ctrl)", krs::sensor::runL2UncertaintyGate() },
            { "SENSOR GATE COMPOSE (L1 true / L2 belief / L3 live; shared correlation + toggles + determinism; independent-draw neg-ctrl)", krs::sensor::runComposeGate() },
            { "SENSOR GATE E2E (one scene -> RGB+depth+IMU in-context signatures + conservation + determinism)", krs::sensor::runE2EGate() },
            { "SENSOR GATE REAL-TRANSFER (harness vs 2nd SYNTHETIC instance; self-consistent + discriminating; NOT real-hardware validated)", krs::sensor::runRealTransferGate() },
            { "GRASP GATE IMPORT (YCB load + real-meter scale + mass/inertia + NaN; x1000 mm-as-meters neg-ctrl)", krs::grasp::runGraspImportGate() },
            { "GRASP GATE COACD (bowl cavity survives: ball rests inside V-HACD/trimesh; convex-hull filler neg-ctrl FAILS)", krs::grasp::runGraspCoacdGate() },
            { "GRASP GATE SUCCESS-CRITERION (good grasp passes / bad fails under LOCKED physics; softened-world anti-cheat caught by guard)", krs::grasp::runGraspSuccessGate() },
            { "GRASP GATE PLANNER (antipodal heuristic success rate random<baseline<tuned under LOCKED physics; random neg-ctrl)", krs::grasp::runGraspPlannerGate() },
            { "GRASP GATE FAILURE-CATALOG (100% of tuned-planner failures classified into a taxonomy; incomplete-taxonomy neg-ctrl)", krs::grasp::runGraspFailureCatalogGate() },
            { "GRASP GATE COACD-REAL (CoACD preserves grasp-relevant concavities V-HACD FILLS; discriminating handle/interior test)", krs::grasp::runGraspCoacdRealGate() },
            { "GRASP GATE REMEASURE (V-HACD vs CoACD success rate, same grasps + LOCKED criterion, apples-to-apples)", krs::grasp::runGraspRemeasureGate() },
            { "GRASP GATE HEURISTIC-V2 (improved planner +above-CoM: V2 strictly beats V1 on YCB under the COMPLIANT gripper; targeted modes drop)", krs::grasp::runGraspHeuristicV2Gate() },
            { "GATE H live SERIAL articulation (H1/H2 vs oracle)", krs::dyn::runArticulationLiveGate() },
            { "GATE D FANUC SERIAL demo stability (D1-D4)",        krs::dyn::runDemoGateD() },
            { "GATE V solid->link assignment (V1 + V-assign)",     krs::dyn::runVisibleArticGateV() },
            { "GATE V.6 FANUC boot path moves (shared helper)",    krs::dyn::runFanucBootGateV6() },
        };
        int fails = 0, skips = 0;
        std::printf("\n--------------- OVERNIGHT BENCH DASHBOARD ---------------\n");
        for (const auto& x : g) {
            const char* tag = x.ok.skipped ? "SKIP" : (x.ok.pass ? "PASS" : "FAIL");
            std::printf("  [%s] %s\n", tag, x.name);
            if (x.ok.skipped) ++skips; else if (!x.ok.pass) ++fails;
        }
        const int n = int(sizeof(g) / sizeof(g[0]));
        std::printf("  ----- %d / %d gate groups PASS  (%d skipped [env prereq absent], %d failed; rigid KRS_BENCH 7/7 runs separately) -----\n",
                    n - fails - skips, n, skips, fails);
        std::fflush(stdout);
        std::_Exit(fails);   // SKIP does not fail the bench; only real FAILs set the exit code
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
    // Phase 4: fill velocity-probe orbs from the live fluid (GL current here; no-op if no orbs).
    updateOrbProbes(m_scene->getRegistry());
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
    // Use the configurable background color (Settings: scene/backgroundColor);
    // visible only where the skybox/IBL does not cover the frame.
    const glm::vec4 bg = m_scene ? m_scene->getRegistry().ctx().get<SceneProperties>().backgroundColor
                                 : glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);
    m_gl->glClearColor(bg.r, bg.g, bg.b, bg.a);
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

    // --- C1) Skybox, or flat grey room (Robot View) ---
    {
        // KRS_NOSKYBOX: debug toggle to force the grey room on EVERY viewport (used to
        // bisect whether on-screen black artifacts are the skybox vs scene geometry).
        const bool drawSky = m_drawSkybox && !qEnvironmentVariableIsSet("KRS_NOSKYBOX");
        auto envCubemap = getEnvCubemap();
        Shader* skyboxShader = drawSky ? getShader("skybox") : nullptr;
        if (drawSky && skyboxShader && envCubemap) {
            m_gl->glDepthFunc(GL_LEQUAL);

            skyboxShader->use(m_gl);
            skyboxShader->setMat4(m_gl, "view", ctx.view);
            skyboxShader->setMat4(m_gl, "projection", ctx.projection);

            m_gl->glActiveTexture(GL_TEXTURE0);
            m_gl->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap->getID());
            skyboxShader->setInt(m_gl, "skybox", 0);
            // Scale the visible sky into nits so it sits in the physically-based world and
            // survives the EV exposure (tied to the ambient/IBL luminance control).
            skyboxShader->setFloat(m_gl, "u_skyNits", getIblIntensity());

            m_gl->glBindVertexArray(GLUtils::getUnitCubeVAO(m_gl));
            m_gl->glDrawArrays(GL_TRIANGLES, 0, 36);
            m_gl->glBindVertexArray(0);

            m_gl->glDepthFunc(GL_LESS);
        } else if (!drawSky) {
            // Grey room: fill the far plane with a flat, exposure-compensated grey
            // (no horizon), so the robot composites over a clean studio background.
            if (Shader* roomShader = getShader("room")) {
                m_gl->glDepthFunc(GL_LEQUAL);
                roomShader->use(m_gl);
                roomShader->setMat4(m_gl, "view", ctx.view);
                roomShader->setMat4(m_gl, "projection", ctx.projection);
                roomShader->setVec3(m_gl, "u_roomColor", m_roomColor);
                roomShader->setFloat(m_gl, "u_invExposure", 1.0f / exposureMultiplier());
                m_gl->glBindVertexArray(GLUtils::getUnitCubeVAO(m_gl));
                m_gl->glDrawArrays(GL_TRIANGLES, 0, 36);
                m_gl->glBindVertexArray(0);
                m_gl->glDepthFunc(GL_LESS);
            }
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

float RenderingSystem::exposureMultiplier() const
{
    float ev = m_exposureEV;
    if (qEnvironmentVariableIsSet("KRS_EV")) ev = qEnvironmentVariable("KRS_EV").toFloat();
    return (1.0f / (1.2f * std::exp2(ev))) * m_tonemapExposure;
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
