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

#include "OpaquePass.hpp"
#include "GridPass.hpp"
#include "SplinePass.hpp"
#include "FieldVisualizerPass.hpp"
#include "SelectionGlowPass.hpp"
#include "CompositePass.hpp"

#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <stdexcept>

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
    //  “…/build/<config>/RoboticsSoftware.exe”  →  “…/shaders/”
    QDir exeDir{ QCoreApplication::applicationDirPath() };
    return exeDir.absoluteFilePath(QStringLiteral("../shaders/"));
#endif
}

RenderingSystem::RenderingSystem(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[LIFECYCLE] RenderingSystem created.";

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setInterval(16);               // as fast as vsync allows
    connect(&m_frameTimer, &QTimer::timeout, this, &RenderingSystem::onMasterUpdate);
}

RenderingSystem::~RenderingSystem() {
    qDebug() << "[LIFECYCLE] RenderingSystem destroyed.";
}

void RenderingSystem::onMasterUpdate()
{
    if (!m_scene) return;

    // 1. Calculate deltaTime ONCE at the start of the frame.
    float dt = m_clock.restart() * 0.001f;
    m_scene->getRegistry().ctx().get<SceneProperties>().deltaTime = dt;

    // 2. Update all scene logic using this single, correct deltaTime.
    updateCameraTransforms();                     // Copies Camera state to TransformComponent
    ViewportWidget::propagateTransforms(m_scene->getRegistry()); // Builds WorldTransformComponent
    updateSceneLogic(dt);                         // Updates splines, physics, animations, etc.


    // 3. Trigger a repaint for all tracked viewports.
    //    Each viewport's paintGL will now use the results of the logic
    //    we just calculated.
    for (auto* vp : m_targets.keys()) {
        if (vp) {
            vp->update();
        }
    }
}

//==============================================================================
// Public API & Lifecycle Management
//==============================================================================

void RenderingSystem::initializeResourcesForContext(QOpenGLFunctions_4_3_Core* gl, Scene* scene) {
    // 2. ADD a check for the CURRENT context
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (m_initializedContexts.contains(ctx)) {
        return; // This context is already initialized, so we're done.
    }
    if (!gl) {
        qFatal("RenderingSystem::initializeResourcesForContext called with a null GL pointer.");
    }

    // 3. ADD a guard to only set the scene pointer once
    if (!m_scene) {
        m_scene = scene;
        m_clock.start();
        m_frameTimer.start();
    }
    qDebug() << "[INIT] Initializing resources for new context:" << ctx;

    // --- Load All Shaders into the PER-CONTEXT Shader Map ---
    qDebug() << "[INIT] Loading all shaders for context" << ctx;
    try {
        const QString shaderDir = shadersRootDir();

        // 4. ADD a line to get the specific shader map for this context
        auto& contextShaders = m_perContextShaders[ctx];

        // 5. CHANGE all 'm_shaders' to 'contextShaders'
        contextShaders["phong"] = Shader::build(gl, (shaderDir + "vertex_shader_vert.glsl").toStdString(), (shaderDir + "fragment_shader_frag.glsl").toStdString()).release();
        contextShaders["emissive_solid"] = Shader::build(gl, (shaderDir + "vertex_shader_vert.glsl").toStdString(), (shaderDir + "emissive_solid_frag.glsl").toStdString()).release();
        contextShaders["grid"] = Shader::build(gl, (shaderDir + "grid_vert.glsl").toStdString(), (shaderDir + "grid_frag.glsl").toStdString()).release();
        contextShaders["glow"] = Shader::build(gl, (shaderDir + "glow_line_vert.glsl").toStdString(), (shaderDir + "glow_line_frag.glsl").toStdString(), (shaderDir + "glow_line_geom.glsl").toStdString()).release();
        contextShaders["cap"] = Shader::build(gl, (shaderDir + "cap_vert.glsl").toStdString(), (shaderDir + "cap_frag.glsl").toStdString(), (shaderDir + "cap_geom.glsl").toStdString()).release();
        contextShaders["instanced_arrow"] = Shader::build(gl, (shaderDir + "instanced_arrow_vert.glsl").toStdString(), (shaderDir + "instanced_arrow_frag.glsl").toStdString()).release();
        contextShaders["arrow_field_compute"] = Shader::build(gl, { (shaderDir + "field_visualizer_comp.glsl").toStdString() }).release();
        contextShaders["particle_update_compute"] = Shader::build(gl, { (shaderDir + "particle_update_comp.glsl").toStdString() }).release();
        contextShaders["particle_render"] = Shader::build(gl, (shaderDir + "particle_render_vert.glsl").toStdString(), (shaderDir + "particle_render_frag.glsl").toStdString()).release();
        contextShaders["flow_vector_compute"] = Shader::build(gl, { (shaderDir + "flow_vector_update_comp.glsl").toStdString() }).release();
        contextShaders["blur"] = Shader::build(gl, (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "gaussian_blur_frag.glsl").toStdString()).release();
        contextShaders["composite"] = Shader::build(gl, (shaderDir + "post_process_vert.glsl").toStdString(), (shaderDir + "composite_frag.glsl").toStdString()).release();
    }
    catch (const std::runtime_error& e) {
        qFatal("[RenderingSystem] FATAL: Shader initialization failed: %s", e.what());
    }

    // 6. ADD a guard to only build the render pass pipeline once
    if (m_renderPasses.size() == 0) {
        qDebug() << "[INIT] Building the render pass pipeline...";
        m_renderPasses.push_back(std::make_unique<GridPass>());
        m_renderPasses.push_back(std::make_unique<OpaquePass>());
        m_renderPasses.push_back(std::make_unique<SplinePass>());
        m_renderPasses.push_back(std::make_unique<FieldVisualizerPass>());
        m_renderPasses.push_back(std::make_unique<SelectionGlowPass>());
        m_renderPasses.push_back(std::make_unique<CompositePass>());
    }

    // --- Initialize Each Pass ---
    qDebug() << "[INIT] Initializing all render passes for context" << ctx;
    for (const auto& pass : m_renderPasses) {
        pass->initialize(*this, gl);
    }

    // 7. Mark this specific context as initialized
    m_initializedContexts.insert(ctx);
    m_isInitialized = true; // Mark that the system is ready in at least one context
    qDebug() << "[INIT] RenderingSystem initialization complete for context" << ctx;
}
void RenderingSystem::renderView(ViewportWidget* viewport, QOpenGLFunctions_4_3_Core* gl, int vpW, int vpH, float frameDeltaTime) {

    // The per-frame logic (like deltaTime calculation and transform propagation)
    // is now handled outside this function, once per frame.

    ensureContextIsTracked(viewport);

    TargetFBOs& target = m_targets[viewport];
    if (target.mainFBO == 0 || vpW > target.w || vpH > target.h) {
        onViewportResized(viewport, gl, vpW, vpH);
    }

    m_currentCamera = viewport->getCameraEntity();
    const auto& camera = m_scene->getRegistry().get<CameraComponent>(m_currentCamera).camera;
    const float aspect = (vpH > 0) ? static_cast<float>(vpW) / vpH : 1.0f;

    // --- THE FIX ---
    // The RenderFrameContext now receives the correct, consistent deltaTime for this frame.
    // NOTE: You must add 'float deltaTime;' to the RenderFrameContext struct definition.
    RenderFrameContext frameContext = {
        gl,
        m_scene->getRegistry(),
        *this,
        camera,
        camera.getViewMatrix(),
        camera.getProjectionMatrix(aspect),
        target,
        vpW,
        vpH,
        frameDeltaTime, // Use the deltaTime passed into the function
        m_elapsedTime
    };

    // The rest of the function remains the same.
    gl->glBindFramebuffer(GL_FRAMEBUFFER, target.mainFBO);
    gl->glViewport(0, 0, target.w, target.h);

    const auto& props = frameContext.registry.ctx().get<SceneProperties>();
    gl->glClearColor(props.backgroundColor.r, props.backgroundColor.g, props.backgroundColor.b, props.backgroundColor.a);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    for (const auto& pass : m_renderPasses) {
        pass->execute(frameContext);
    }
}


void RenderingSystem::shutdown(QOpenGLFunctions_4_3_Core* gl) {
    if (!m_isInitialized || !gl) return;
    qDebug() << "[LIFECYCLE] Shutting down all GPU resources...";

    // --- 1. Delete all shaders from all per-context maps --- // MODIFIED SECTION
    for (auto& contextShaders : m_perContextShaders) {
        for (auto* shader : contextShaders) {
            shader->destroy(gl); // First, release the GPU resource
            delete shader;       // Then, delete the C++ object
        }
    }
    m_perContextShaders.clear();
    m_initializedContexts.clear(); // Also clear our set of initialized contexts
    // --- END MODIFIED SECTION ---

    // --- 2. Delete all per-viewport FBOs (No changes needed) ---
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
        const TargetFBOs& target = it.value();
        // This logic is already correct for cleaning up FBOs
        gl->glDeleteFramebuffers(1, &target.mainFBO);
        gl->glDeleteTextures(1, &target.mainColorTexture);
        gl->glDeleteTextures(1, &target.mainDepthTexture);
        gl->glDeleteFramebuffers(1, &target.glowFBO);
        gl->glDeleteTextures(1, &target.glowTexture);
        gl->glDeleteFramebuffers(2, target.pingpongFBO);
        gl->glDeleteTextures(2, target.pingpongTexture);
    }
    m_targets.clear();

    // --- 3. Delete per-entity GPU resources (No changes needed) ---
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view) {
        auto& res = view.get<RenderResourceComponent>(entity);
        // This logic already correctly iterates through all contexts
        for (auto const& [context, buffers] : res.perContext) {
            if (buffers.VAO) gl->glDeleteVertexArrays(1, &buffers.VAO);
            if (buffers.VBO) gl->glDeleteBuffers(1, &buffers.VBO);
            if (buffers.EBO) gl->glDeleteBuffers(1, &buffers.EBO);
        }
        res.perContext.clear();
    }

    m_isInitialized = false;
    qDebug() << "[LIFECYCLE] GPU resources shut down successfully.";
}

//==============================================================================
// Lifecycle Management Slots & Helpers
//==============================================================================

void RenderingSystem::ensureContextIsTracked(QOpenGLWidget* vp) {
    if (!vp || !vp->context()) return;
    QOpenGLContext* ctx = vp->context();
    if (m_trackedContexts.find(ctx) == m_trackedContexts.end()) {
        m_trackedContexts.insert(ctx);
        connect(ctx, &QOpenGLContext::aboutToBeDestroyed, this, &RenderingSystem::onContextAboutToBeDestroyed);
        qDebug() << "[LIFECYCLE] Now tracking new context:" << ctx;
    }
}

void RenderingSystem::onContextAboutToBeDestroyed() {
    QOpenGLContext* dyingContext = qobject_cast<QOpenGLContext*>(sender());
    if (!dyingContext || m_trackedContexts.find(dyingContext) == m_trackedContexts.end()) {
        return;
    }

    qDebug() << "[LIFECYCLE] Context" << dyingContext << "is being destroyed. Cleaning up associated resources.";

    // --- Find a survivor context to perform GPU deletions ---
    QOpenGLWidget* survivorWidget = nullptr;
    for (auto* widget : m_targets.keys()) {
        if (widget->context() && widget->context() != dyingContext) {
            survivorWidget = widget;
            break;
        }
    }

    if (survivorWidget) {
        survivorWidget->makeCurrent();
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(survivorWidget->context());
        if (gl) {
            // --- 1. Delegate cleanup to each render pass ---
            for (const auto& pass : m_renderPasses) {
                pass->onContextDestroyed(dyingContext, gl);
            }

            // --- 2. Clean up per-entity resources for the dying context ---
            auto& registry = m_scene->getRegistry();
            auto view = registry.view<RenderResourceComponent>();
            for (auto entity : view) {
                auto& res = view.get<RenderResourceComponent>(entity);
                if (res.perContext.count(dyingContext)) {
                    const auto& buffers = res.perContext.at(dyingContext);
                    if (buffers.VAO) gl->glDeleteVertexArrays(1, &buffers.VAO);
                    if (buffers.VBO) gl->glDeleteBuffers(1, &buffers.VBO);
                    if (buffers.EBO) gl->glDeleteBuffers(1, &buffers.EBO);
                }
            }

            // --- 3. Clean up FBOs that belonged to widgets with the dying context ---
            for (auto it = m_targets.begin(); it != m_targets.end();) {
                if (it.key()->context() == dyingContext) {
                    TargetFBOs& target = it.value();
                    gl->glDeleteFramebuffers(1, &target.mainFBO);
                    // ... delete all other textures and FBOs for this target ...
                    it = m_targets.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
        survivorWidget->doneCurrent();
    }

    // --- 4. Always clean up CPU-side maps ---
    m_trackedContexts.remove(dyingContext);
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<RenderResourceComponent>();
    for (auto entity : view) {
        view.get<RenderResourceComponent>(entity).perContext.erase(dyingContext);
    }
    qDebug() << "[LIFECYCLE] Finished cleanup for context" << dyingContext;
}

bool RenderingSystem::isContextInitialized(QOpenGLContext* ctx) const
{
    return m_initializedContexts.contains(ctx);
}

void RenderingSystem::onViewportResized(ViewportWidget* vp, QOpenGLFunctions_4_3_Core* gl, int fbW, int fbH) {
    if (!vp || !vp->context()) return;

    // Call the helper to do the actual FBO recreation.
    initOrResizeFBOsForTarget(gl, m_targets[vp], fbW, fbH);

    // This is where you could notify passes if they needed to react to a resize.
    // For now, it's not needed, but the hook is here for the future.
    /*
    RenderFrameContext resizeContext = { ... };
    for (const auto& pass : m_renderPasses) {
        pass->onResize(resizeContext);
    }
    */
}

void RenderingSystem::initOrResizeFBOsForTarget(QOpenGLFunctions_4_3_Core* gl, TargetFBOs& target, int width, int height) {
    // Delete old resources if they exist to prevent leaks.
    if (target.mainFBO != 0) {
        gl->glDeleteFramebuffers(1, &target.mainFBO);
        gl->glDeleteTextures(1, &target.mainColorTexture);
        gl->glDeleteTextures(1, &target.mainDepthTexture);
        gl->glDeleteFramebuffers(1, &target.glowFBO);
        gl->glDeleteTextures(1, &target.glowTexture);
        gl->glDeleteFramebuffers(2, target.pingpongFBO);
        gl->glDeleteTextures(2, target.pingpongTexture);
    }

    target.w = width;
    target.h = height;

    // --- Main Scene FBO (Color + Depth/Stencil) ---
    gl->glGenFramebuffers(1, &target.mainFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, target.mainFBO);

    gl->glGenTextures(1, &target.mainColorTexture);
    gl->glBindTexture(GL_TEXTURE_2D, target.mainColorTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.mainColorTexture, 0);

    gl->glGenTextures(1, &target.mainDepthTexture);
    gl->glBindTexture(GL_TEXTURE_2D, target.mainDepthTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, target.mainDepthTexture, 0);

    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Main FBO is not complete!";

    // --- Glow FBO ---
    gl->glGenFramebuffers(1, &target.glowFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, target.glowFBO);
    gl->glGenTextures(1, &target.glowTexture);
    gl->glBindTexture(GL_TEXTURE_2D, target.glowTexture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.glowTexture, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Glow FBO is not complete!";

    // --- Ping-Pong FBOs for Blurring ---
    gl->glGenFramebuffers(2, target.pingpongFBO);
    gl->glGenTextures(2, target.pingpongTexture);
    for (unsigned int i = 0; i < 2; i++) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, target.pingpongFBO[i]);
        gl->glBindTexture(GL_TEXTURE_2D, target.pingpongTexture[i]);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.pingpongTexture[i], 0);
        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "Pingpong FBO " << i << " is not complete!";
    }

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind FBO when done.
}


//==============================================================================
// Scene Logic Updaters
//==============================================================================

void RenderingSystem::updateCameraTransforms() {
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<CameraComponent, TransformComponent>();
    for (auto e : view) {
        auto& cam = view.get<CameraComponent>(e).camera;
        auto& xf = view.get<TransformComponent>(e);
        xf.translation = cam.getPosition();
        glm::vec3 fwd = glm::normalize(cam.getFocalPoint() - cam.getPosition());
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::cross(right, fwd);
        xf.rotation = glm::quat_cast(glm::mat3(right, up, -fwd));
    }
}

void RenderingSystem::updateSceneLogic(float deltaTime) {
    m_elapsedTime += deltaTime;
    auto& registry = m_scene->getRegistry();

    updateSplineCaches();

    // Update dirty spline caches
    auto splineView = registry.view<SplineComponent>();
    for (auto entity : splineView) {
        auto& spline = splineView.get<SplineComponent>(entity);
        if (spline.isDirty) {
            SplineUtils::updateCache(spline);
        }
    }

    // Update pulsing spline animations
    auto pulseView = registry.view<PulsingSplineTag, SplineComponent>();
    if (!pulseView.size_hint() == 0) {
        float brightness = (sin(m_elapsedTime * 3.0f) + 1.0f) / 2.0f;
        float finalBrightness = 0.1f + 0.9f * brightness;
        for (auto entity : pulseView) {
            pulseView.get<SplineComponent>(entity).glowColour.a = 1.0f * finalBrightness;
        }
    }

    auto ledView = registry.view<RecordLedTag, MaterialComponent,
        PulsingLightComponent>();
    if (!ledView.size_hint() == 0) {
        for (auto e : ledView) {
            auto& mat = ledView.get<MaterialComponent>(e);
            const auto& plc = ledView.get<PulsingLightComponent>(e);

            // Square-wave: 0 or 1 based on sin()
            float t = (sin(m_elapsedTime * plc.speed) + 1.0f) * 0.5f;     // 0→1
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
        return m_perContextShaders.value(ctx).value(QString::fromStdString(name), nullptr);
    }
    return nullptr;
}

void RenderingSystem::updateSplineCaches() {
    auto& registry = m_scene->getRegistry();
    // Get a view of all entities that have a SplineComponent.
    // We get a mutable view because we need to modify the component's cached data.
    auto view = registry.view<SplineComponent>();

    for (auto entity : view) {
        auto& spline = view.get<SplineComponent>(entity);
        // Only perform the expensive calculation if the control points have changed.
        if (spline.isDirty) {
            // Call our centralized utility to do the math and update the cache.
            SplineUtils::updateCache(spline);
            // The updateCache function also sets spline.isDirty = false;
        }
    }
}