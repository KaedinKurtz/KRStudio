#include "FieldVisualizerPass.hpp"
#include "PrimitiveBuilders.hpp" // Your existing file!
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "GpuTypes.hpp"

#include <QOpenGLContext>
#include <QDebug>
#include <random>
#include <glm/gtc/type_ptr.hpp>

// --- Class Implementation ---
namespace {
    static void uploadGradientToCurrentProgram(QOpenGLFunctions_4_3_Core* gl,
        const std::vector<ColorStop>& grad)
    {
        GLint prog = 0;
        gl->glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (!prog) return;

        const GLint count = std::min<int>(grad.size(), 8);

        // --- START OF INSTRUMENTATION ---
        qDebug() << "[DEBUG] Uploading Gradient to Shader Program:" << prog;
        qDebug() << "  - Stop Count Sent to Shader:" << count;
        // --- END OF INSTRUMENTATION ---

        // ── scalar uniform ────────────────────────────────────────────────────
        GLint loc = gl->glGetUniformLocation(prog, "u_stopCount");
        if (loc != -1) {
            gl->glUniform1i(loc, count);
        }

        // --- More Instrumentation ---
        if (count > 0) {
            std::array<float, 8> pos{};
            std::array<glm::vec4, 8> col{};
            for (int i = 0; i < count; ++i) {
                pos[i] = grad[i].position;
                col[i] = grad[i].color;
            }

            // Print the arrays we are about to send
            for (int i = 0; i < count; ++i) {
                qDebug() << "  - Stop" << i << ": Pos =" << pos[i]
                    << "Color = (" << col[i].r << "," << col[i].g << "," << col[i].b << "," << col[i].a << ")";
            }

            // ── array uniforms: MUST use [0] in the query ─────────────────────────
            loc = gl->glGetUniformLocation(prog, "u_stopPos[0]");
            if (loc != -1) {
                gl->glUniform1fv(loc, count, pos.data());
            }

            loc = gl->glGetUniformLocation(prog, "u_stopColor[0]");
            if (loc != -1) {
                gl->glUniform4fv(loc, count, glm::value_ptr(col[0]));
            }
        }
    }
}

FieldVisualizerPass::~FieldVisualizerPass() {
    if (!m_arrowVAOs.empty()) {
        qWarning() << "FieldVisualizerPass destroyed, but" << m_arrowVAOs.size()
            << "VAOs still exist. This indicates a resource leak because"
            << "onContextDestroyed() was not called for all contexts.";
    }
}

void FieldVisualizerPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {

}

void FieldVisualizerPass::createResourcesForContext(QOpenGLFunctions_4_3_Core* gl) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    GLuint ubo = 0;
    GLuint ssbo = 0;

    gl->glGenBuffers(1, &ubo);
    gl->glGenBuffers(1, &ssbo);

    // Store the new buffer IDs in our maps
    m_effectorDataUBOs[ctx] = ubo;
    m_triangleDataSSBOs[ctx] = ssbo;
}

void FieldVisualizerPass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // --- 1. Ensure all resources for the current context exist ---
    if (!m_effectorDataUBOs.contains(ctx)) {
        createResourcesForContext(gl);
    }
    if (!m_arrowVAOs.contains(ctx)) {
        createArrowPrimitiveForContext(ctx, gl);
    }

    // --- 2. Get all shaders for the current context ---
    Shader* instancedArrowShader = context.renderer.getShader("instanced_arrow");
    Shader* arrowFieldComputeShader = context.renderer.getShader("arrow_field_compute");
    Shader* particleUpdateComputeShader = context.renderer.getShader("particle_update_compute");
    Shader* particleRenderShader = context.renderer.getShader("particle_render");
    Shader* flowVectorComputeShader = context.renderer.getShader("flow_vector_compute");

    if (!instancedArrowShader || !arrowFieldComputeShader || !particleUpdateComputeShader ||
        !particleRenderShader || !flowVectorComputeShader) {
        qWarning("FieldVisualizerPass: Could not retrieve all required shaders for the current context.");
        return;
    }

    // --- 3. Get buffer IDs for the current context ---
    GLuint effectorUBO = m_effectorDataUBOs.value(ctx);
    GLuint triangleSSBO = m_triangleDataSSBOs.value(ctx);

    // --- 4. Gather & Upload Data (once per frame) ---
    gatherEffectorData(context);
    uploadEffectorData(gl, effectorUBO, triangleSSBO); // Pass the correct buffer IDs

    // --- 5. Main Visualizer Loop ---
    auto& registry = context.registry;
    auto visualizerView = registry.view<FieldVisualizerComponent, TransformComponent>();
    for (auto entity : visualizerView) {
        auto& vis = registry.get<FieldVisualizerComponent>(entity);
        if (!vis.isEnabled) continue;
        const auto& xf = registry.get<TransformComponent>(entity);

        // --- 6. Delegate to sub-routines, passing all necessary resources ---
        if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Particles) {
            renderParticles(context, vis, xf, particleRenderShader, particleUpdateComputeShader, effectorUBO, triangleSSBO);
        }
        else if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Flow) {
            renderFlow(context, vis, xf, instancedArrowShader, flowVectorComputeShader, effectorUBO, triangleSSBO);
        }
        else if (vis.displayMode == FieldVisualizerComponent::DisplayMode::Arrows) {
            renderArrows(context, vis, xf, instancedArrowShader, arrowFieldComputeShader, effectorUBO, triangleSSBO);
        }
        vis.isGpuDataDirty = false;
    }
}

void FieldVisualizerPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    // Clean up per-context arrow primitive
    if (m_arrowVAOs.contains(dyingContext)) {
        gl->glDeleteVertexArrays(1, &m_arrowVAOs[dyingContext]);
        gl->glDeleteBuffers(1, &m_arrowVBOs[dyingContext]);
        gl->glDeleteBuffers(1, &m_arrowEBOs[dyingContext]);
        m_arrowVAOs.remove(dyingContext);
        m_arrowVBOs.remove(dyingContext);
        m_arrowEBOs.remove(dyingContext);
        m_arrowIndexCounts.remove(dyingContext);
    }

    // IMPORTANT: Also clean up buffers owned by the components themselves
    // that were created in the dying context.
    // (This logic needs to be added to ensure no leaks)
}

// --- Private Helper Implementations ---

void FieldVisualizerPass::createArrowPrimitiveForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    qDebug() << "FieldVisualizerPass: Creating arrow primitive for context" << ctx;

    GLuint vao = 0, vbo = 0, ebo = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo);
    gl->glGenBuffers(1, &ebo);

    // Use your existing builder function!
    size_t indexCount = buildArrowMesh(gl, vao, vbo, ebo);

    m_arrowVAOs[ctx] = vao;
    m_arrowVBOs[ctx] = vbo;
    m_arrowEBOs[ctx] = ebo;
    m_arrowIndexCounts[ctx] = indexCount;
}

void FieldVisualizerPass::gatherEffectorData(const RenderFrameContext& context) {
    // 1. Clear the member vectors from the previous frame's data.
    m_pointEffectors.clear();
    m_triangleEffectors.clear();
    m_directionalEffectors.clear();

    // 2. Get a reference to the scene registry from the context.
    auto& registry = context.registry;

    // 3. The rest of the logic is a direct copy, using the member vectors.
    // --- Point Effectors ---
    auto pointView = registry.view<PointEffectorComponent, TransformComponent>();
    for (auto entity : pointView) {
        auto& comp = pointView.get<PointEffectorComponent>(entity);
        auto& xf = pointView.get<TransformComponent>(entity);
        PointEffectorGpu effector;
        effector.position = glm::vec4(xf.translation, 1.0f);
        effector.normal = glm::vec4(0.0f);
        effector.strength = comp.strength;
        effector.radius = comp.radius;
        effector.falloffType = static_cast<int>(comp.falloff);
        m_pointEffectors.push_back(effector); // Use member variable
    }

    // --- Spline Effectors ---
    auto splineView = registry.view<SplineEffectorComponent, SplineComponent>();
    for (auto entity : splineView) {
        auto& comp = splineView.get<SplineEffectorComponent>(entity);
        const auto& spline = splineView.get<const SplineComponent>(entity); // View as const
        if (spline.cachedVertices.empty()) continue;
        for (size_t i = 0; i < spline.cachedVertices.size() - 1; ++i) {
            glm::vec3 tangent = glm::normalize(spline.cachedVertices[i + 1] - spline.cachedVertices[i]);
            glm::vec3 normal = glm::normalize(glm::cross(tangent, glm::vec3(0, 1, 0)));
            if (comp.direction == SplineEffectorComponent::ForceDirection::Tangent) {
                normal = tangent;
            }
            PointEffectorGpu effector;
            effector.position = glm::vec4(spline.cachedVertices[i], 1.0f);
            effector.normal = glm::vec4(normal, 0.0f);
            effector.strength = comp.strength;
            effector.radius = comp.radius;
            effector.falloffType = 1;
            m_pointEffectors.push_back(effector); // Use member variable
        }
    }

    // --- Mesh Effectors ---
    auto meshView = registry.view<MeshEffectorComponent, RenderableMeshComponent, TransformComponent>();
    for (auto entity : meshView) {
        auto& comp = meshView.get<MeshEffectorComponent>(entity);
        const auto& mesh = meshView.get<const RenderableMeshComponent>(entity);
        auto& xf = meshView.get<TransformComponent>(entity);
        glm::mat4 model = xf.getTransform();
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            TriangleGpu tri;
            tri.v0 = model * glm::vec4(mesh.vertices[mesh.indices[i]].position, 1.0f);
            tri.v1 = model * glm::vec4(mesh.vertices[mesh.indices[i + 1]].position, 1.0f);
            tri.v2 = model * glm::vec4(mesh.vertices[mesh.indices[i + 2]].position, 1.0f);
            tri.v0.w = comp.strength;
            tri.normal.w = comp.distance;
            m_triangleEffectors.push_back(tri); // Use member variable
        }
    }

    // --- Directional Effectors ---
    auto dirView = registry.view<DirectionalEffectorComponent>();
    for (auto entity : dirView) {
        auto& comp = dirView.get<DirectionalEffectorComponent>(entity);
        DirectionalEffectorGpu effector;
        effector.direction = glm::vec4(glm::normalize(comp.direction), 0.0f);
        effector.strength = comp.strength;
        m_directionalEffectors.push_back(effector); // Use member variable
    }
}

void FieldVisualizerPass::uploadEffectorData(QOpenGLFunctions_4_3_Core* gl, GLuint uboID, GLuint ssboID) {
    // --- Upload Point and Directional Effectors to the UBO ---
    gl->glBindBuffer(GL_UNIFORM_BUFFER, uboID); // USE PARAMETER
    // Orphan the buffer for performance (a common strategy for DYNAMIC_DRAW buffers)
    gl->glBufferData(GL_UNIFORM_BUFFER, 256 * sizeof(PointEffectorGpu) + 16 * sizeof(DirectionalEffectorGpu), nullptr, GL_DYNAMIC_DRAW);

    // Upload point effectors if any exist.
    if (!m_pointEffectors.empty()) {
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, std::min(size_t(256), m_pointEffectors.size()) * sizeof(PointEffectorGpu), m_pointEffectors.data());
    }
    // Upload directional effectors if any exist.
    if (!m_directionalEffectors.empty()) {
        size_t directionalOffset = 256 * sizeof(PointEffectorGpu);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, directionalOffset, std::min(size_t(16), m_directionalEffectors.size()) * sizeof(DirectionalEffectorGpu), m_directionalEffectors.data());
    }
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0); // Unbind

    // --- Upload Triangle Effectors to the SSBO ---
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboID); // USE PARAMETER
    // Orphan and upload triangle data.
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, m_triangleEffectors.size() * sizeof(TriangleGpu), m_triangleEffectors.empty() ? nullptr : m_triangleEffectors.data(), GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0); // Unbind
}

void FieldVisualizerPass::renderParticles(const RenderFrameContext& context,
    FieldVisualizerComponent& vis,
    const TransformComponent& xf,
    Shader* renderShader,
    Shader* computeShader,
    GLuint                     uboID,
    GLuint                     ssboID)
{
    auto* gl = context.gl;
    auto& settings = vis.particleSettings;

    /* ───────────────────────  create or rebuild GPU resources  ─────────────────────── */
    const bool needInit = (vis.particleBuffer[0] == 0) || vis.isGpuDataDirty;
    if (needInit)
    {
        /* clean up old buffers / VAO if they exist */
        if (vis.particleBuffer[0] != 0)
        {
            gl->glDeleteBuffers(2, vis.particleBuffer);
            gl->glDeleteVertexArrays(1, &vis.particleVAO);
        }

        /* CPU-side bootstrap */
        std::vector<Particle> particles(settings.particleCount);
        std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        const glm::vec3 boundsSize = vis.bounds.max - vis.bounds.min;

        for (int i = 0; i < settings.particleCount; ++i)
        {
            particles[i].position = glm::vec4(vis.bounds.min + dist(rng) * boundsSize, 1.0f);
            particles[i].velocity = glm::vec4(0.0f);
            particles[i].color = glm::vec4(1.0f);
            particles[i].age = dist(rng) * settings.lifetime;
            particles[i].lifetime = settings.lifetime;
            particles[i].size = settings.baseSize;
        }

        /* ping-pong SSBO pair */
        gl->glGenBuffers(2, vis.particleBuffer);

        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);            // read
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
            particles.size() * sizeof(Particle),
            particles.data(),
            GL_DYNAMIC_DRAW);

        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);            // write
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
            particles.size() * sizeof(Particle),
            nullptr,
            GL_DYNAMIC_DRAW);

        vis.currentReadBuffer = 0;

        /* VAO (attribute layout stored once) */
        gl->glBindVertexArray(vis.particleVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, vis.particleBuffer[1 - vis.currentReadBuffer]);

        /* ↺ refresh attribute bindings (needed after any slider tweak) */
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Particle),
            (void*)offsetof(Particle, position));
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Particle),
            (void*)offsetof(Particle, color));
        gl->glEnableVertexAttribArray(2);
        gl->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Particle),
            (void*)offsetof(Particle, size));

        gl->glBindVertexArray(0);
    }

    /* ─────────────────────────  compute pass  ───────────────────────── */
    computeShader->use(gl);

    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]);

    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_deltaTime", context.deltaTime);
    computeShader->setFloat(gl, "u_time", context.elapsedTime);
    computeShader->setVec3(gl, "u_boundsMin", vis.bounds.min);
    computeShader->setVec3(gl, "u_boundsMax", vis.bounds.max);

    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));

    computeShader->setFloat(gl, "u_lifetime", settings.lifetime);
    computeShader->setFloat(gl, "u_baseSpeed", settings.baseSpeed);
    computeShader->setFloat(gl, "u_speedIntensityMultiplier", settings.speedIntensityMultiplier);
    computeShader->setFloat(gl, "u_baseSize", settings.baseSize);
    computeShader->setFloat(gl, "u_peakSizeMultiplier", settings.peakSizeMultiplier);
    computeShader->setFloat(gl, "u_minSize", settings.minSize);
    computeShader->setFloat(gl, "u_randomWalkStrength", settings.randomWalkStrength);
    computeShader->setInt(gl, "u_coloringMode", static_cast<int>(settings.coloringMode));

    if (settings.coloringMode == FieldVisualizerComponent::ColoringMode::Intensity)
        uploadGradientToCurrentProgram(gl, settings.intensityGradient);
    else if (settings.coloringMode == FieldVisualizerComponent::ColoringMode::Lifetime)
        uploadGradientToCurrentProgram(gl, settings.lifetimeGradient);

    gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    /* ─────────────────────────  render pass  ───────────────────────── */
    gl->glEnable(GL_PROGRAM_POINT_SIZE);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gl->glDepthMask(GL_FALSE);

    renderShader->use(gl);
    renderShader->setMat4(gl, "u_view", context.view);
    renderShader->setMat4(gl, "u_projection", context.projection);

    gl->glBindVertexArray(vis.particleVAO);

    /* ↺ re-point VAO attribute bindings to *current write buffer* */
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.particleBuffer[1 - vis.currentReadBuffer]);
    gl->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Particle),
        reinterpret_cast<void*>(offsetof(Particle, position)));
    gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Particle),
        reinterpret_cast<void*>(offsetof(Particle, color)));
    gl->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Particle),
        reinterpret_cast<void*>(offsetof(Particle, size)));

    gl->glDrawArrays(GL_POINTS, 0, settings.particleCount);

    gl->glBindVertexArray(0);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_PROGRAM_POINT_SIZE);

    /* swap read/write for next frame */
    vis.currentReadBuffer = 1 - vis.currentReadBuffer;
}

void FieldVisualizerPass::renderFlow(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf, Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID)
{
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto& settings = vis.flowSettings;
    if (vis.particleBuffer[0] == 0 || vis.isGpuDataDirty) {
        if (vis.particleBuffer[0] != 0) {
            gl->glDeleteBuffers(2, vis.particleBuffer);
            if (vis.gpuData.instanceDataSSBO) gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
        }
        std::vector<Particle> particles(settings.particleCount);
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> distrib(0.0f, 1.0f);
        glm::vec3 boundsSize = vis.bounds.max - vis.bounds.min;
        for (int i = 0; i < settings.particleCount; ++i) {
            particles[i].position = glm::vec4(vis.bounds.min + distrib(rng) * boundsSize, 1.0f);
            particles[i].velocity = glm::vec4(0.0f);
            particles[i].color = glm::vec4(1.0f);
            particles[i].age = distrib(rng) * settings.lifetime;
            particles[i].lifetime = settings.lifetime;
            particles[i].size = settings.baseSize;
        }
        gl->glGenBuffers(2, vis.particleBuffer);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);
        if (vis.gpuData.instanceDataSSBO == 0) gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, settings.particleCount * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);
    }

    computeShader->use(gl);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, vis.gpuData.instanceDataSSBO);

    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_deltaTime", context.deltaTime);
    computeShader->setFloat(gl, "u_time", context.elapsedTime);
    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));

    computeShader->setFloat(gl, "u_lifetime", settings.lifetime);
    computeShader->setFloat(gl, "u_baseSpeed", settings.baseSpeed);
    computeShader->setFloat(gl, "u_velocityMultiplier", settings.speedIntensityMultiplier); // Corrected name
    computeShader->setFloat(gl, "u_flowScale", settings.baseSize); // Corrected name
    computeShader->setFloat(gl, "u_headScale", settings.headScale);
    computeShader->setFloat(gl, "u_peakSizeMultiplier", settings.peakSizeMultiplier);
    computeShader->setFloat(gl, "u_minSize", settings.minSize);
    computeShader->setFloat(gl, "u_fadeInPercent", settings.growthPercentage); // Corrected name
    computeShader->setFloat(gl, "u_fadeOutPercent", settings.shrinkPercentage); // Corrected name
    computeShader->setFloat(gl, "u_randomWalkStrength", settings.randomWalkStrength);
    computeShader->setInt(gl, "u_coloringMode", static_cast<int>(settings.coloringMode));

    if (settings.coloringMode == FieldVisualizerComponent::ColoringMode::Intensity) {
        uploadGradientToCurrentProgram(gl, settings.intensityGradient);
    }
    else if (settings.coloringMode == FieldVisualizerComponent::ColoringMode::Lifetime) {
        uploadGradientToCurrentProgram(gl, settings.lifetimeGradient);
    }

    gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    renderShader->use(gl);
    renderShader->setMat4(gl, "view", context.view);
    renderShader->setMat4(gl, "projection", context.projection);
    gl->glBindVertexArray(m_arrowVAOs[ctx]);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);
    const GLsizei stride = sizeof(InstanceData);
    const GLsizei vec4Size = sizeof(glm::vec4);
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 0 * vec4Size));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 1 * vec4Size));
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 2 * vec4Size));
    gl->glEnableVertexAttribArray(5);
    gl->glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 3 * vec4Size));
    gl->glEnableVertexAttribArray(6);
    gl->glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, color));
    gl->glVertexAttribDivisor(2, 1);
    gl->glVertexAttribDivisor(3, 1);
    gl->glVertexAttribDivisor(4, 1);
    gl->glVertexAttribDivisor(5, 1);
    gl->glVertexAttribDivisor(6, 1);
    gl->glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_arrowIndexCounts[ctx]), GL_UNSIGNED_INT, 0, settings.particleCount);
    gl->glBindVertexArray(0);
    gl->glVertexAttribDivisor(2, 0);
    gl->glVertexAttribDivisor(3, 0);
    gl->glVertexAttribDivisor(4, 0);
    gl->glVertexAttribDivisor(5, 0);
    gl->glVertexAttribDivisor(6, 0);

    vis.currentReadBuffer = 1 - vis.currentReadBuffer;
}

void FieldVisualizerPass::renderArrows(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
    Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID)
{
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    auto& settings = vis.arrowSettings;
    if (vis.isGpuDataDirty) {
        if (vis.gpuData.samplePointsSSBO) gl->glDeleteBuffers(1, &vis.gpuData.samplePointsSSBO);
        if (vis.gpuData.instanceDataSSBO) gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
        if (vis.gpuData.commandUBO) gl->glDeleteBuffers(1, &vis.gpuData.commandUBO);

        std::vector<glm::vec4> samplePoints;
        vis.gpuData.numSamplePoints = settings.density.x * settings.density.y * settings.density.z;

        if (vis.gpuData.numSamplePoints > 0) {
            samplePoints.reserve(vis.gpuData.numSamplePoints);
            glm::vec3 boundsSize = vis.bounds.max - vis.bounds.min;
            for (int x = 0; x < settings.density.x; ++x) {
                for (int y = 0; y < settings.density.y; ++y) {
                    for (int z = 0; z < settings.density.z; ++z) {
                        glm::vec3 t = {
                            (settings.density.x > 1) ? static_cast<float>(x) / (settings.density.x - 1) : 0.5f,
                            (settings.density.y > 1) ? static_cast<float>(y) / (settings.density.y - 1) : 0.5f,
                            (settings.density.z > 1) ? static_cast<float>(z) / (settings.density.z - 1) : 0.5f,
                        };
                        samplePoints.emplace_back(glm::vec4(vis.bounds.min + t * boundsSize, 1.0f));
                    }
                }
            }
            gl->glGenBuffers(1, &vis.gpuData.samplePointsSSBO);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.samplePointsSSBO);
            gl->glBufferData(GL_SHADER_STORAGE_BUFFER, samplePoints.size() * sizeof(glm::vec4), samplePoints.data(), GL_STATIC_DRAW);

            gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
            gl->glBufferData(GL_SHADER_STORAGE_BUFFER, vis.gpuData.numSamplePoints * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

            DrawElementsIndirectCommand cmd = { (GLuint)m_arrowIndexCounts[ctx], 0, 0, 0, 0 };
            gl->glGenBuffers(1, &vis.gpuData.commandUBO);
            gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
            gl->glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), &cmd, GL_DYNAMIC_DRAW);
        }
    }

    if (vis.gpuData.numSamplePoints == 0) return;

    // --- Compute Pass ---
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
    GLuint zero = 0;
    gl->glBufferSubData(GL_DRAW_INDIRECT_BUFFER, sizeof(GLuint), sizeof(GLuint), &zero);
    computeShader->use(gl);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vis.gpuData.samplePointsSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vis.gpuData.instanceDataSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, vis.gpuData.commandUBO);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID);
    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_vectorScale", settings.vectorScale);
    computeShader->setFloat(gl, "u_arrowHeadScale", settings.headScale);
    computeShader->setFloat(gl, "u_cullingThreshold", settings.cullingThreshold);
    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));
    gl->glDispatchCompute((GLuint)vis.gpuData.numSamplePoints / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_ALL_BARRIER_BITS);

    {
        // 1. Read the DrawElementsIndirectCommand struct back from the command UBO.
        DrawElementsIndirectCommand cmd_readback = { 0 };
        gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
        gl->glGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &cmd_readback);
        gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

        // 2. Print the contents of the draw command.
        qDebug() << "[DEBUG] Indirect Draw Command Readback:";
        // --- ADD THIS LINE ---
        qDebug() << "  - Index Count per Instance:" << cmd_readback.count;
        qDebug() << "  - Instance Count Generated:" << cmd_readback.instanceCount;

        // 3. If any instances were generated, read and print the data for the first one.
        if (cmd_readback.instanceCount > 0)
        {
            InstanceData firstInstance_readback;
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
            gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(InstanceData), &firstInstance_readback);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            // Print the components of the first arrow's transformation matrix.
            const glm::mat4& m = firstInstance_readback.modelMatrix;
            qDebug() << "  - First Instance Matrix:";
            qDebug() << "    " << m[0][0] << m[1][0] << m[2][0] << m[3][0];
            qDebug() << "    " << m[0][1] << m[1][1] << m[2][1] << m[3][1];
            qDebug() << "    " << m[0][2] << m[1][2] << m[2][2] << m[3][2];
            qDebug() << "    " << m[0][3] << m[1][3] << m[2][3] << m[3][3];
            qDebug() << "  - First Instance Color/Intensity:" << firstInstance_readback.color.x;
        }
    }

    // --- Render Pass ---
    renderShader->use(gl);
    renderShader->setMat4(gl, "view", context.view);
    renderShader->setMat4(gl, "projection", context.projection);

    // --- START OF NEW INSTRUMENTATION ---
    qDebug() << "[DEBUG] Active Render Shader Program ID:" << renderShader->ID;
    GLint location = gl->glGetUniformLocation(renderShader->ID, "u_stopCount");
    qDebug() << "[DEBUG] Location of 'u_stopCount' uniform:" << location;
    // --- END OF NEW INSTRUMENTATION ---


    // Set the uniforms that the vertex shader will use to calculate the color
    renderShader->setInt(gl, "u_coloringMode", static_cast<int>(vis.arrowSettings.coloringMode));
    uploadGradientToCurrentProgram(gl, vis.arrowSettings.intensityGradient);
    // --- END OF THE FIX ---

    gl->glBindVertexArray(m_arrowVAOs[ctx]);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);
    const GLsizei stride = sizeof(InstanceData);
    const GLsizei vec4Size = sizeof(glm::vec4);

    // modelMatrix (mat4) at locations 2, 3, 4, 5 (This part is correct)
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 0 * vec4Size));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 1 * vec4Size));
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 2 * vec4Size));
    gl->glEnableVertexAttribArray(5);
    gl->glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, modelMatrix) + 3 * vec4Size));

    // --- FIX: Correctly set up attribute pointers for intensity and age ---
    // The shader expects two separate floats at locations 6 and 7.
    // We tell OpenGL to read them from the 'color' vec4 in our InstanceData struct.
    gl->glEnableVertexAttribArray(6); // aIntensity (float) is at the start of the color member
    gl->glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, color));

    gl->glEnableVertexAttribArray(7); // aAgeNorm (float) is offset by one float from the start of the color member
    gl->glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(InstanceData, color) + sizeof(float)));

    // Set all instance attributes to advance once per instance
    gl->glVertexAttribDivisor(2, 1);
    gl->glVertexAttribDivisor(3, 1);
    gl->glVertexAttribDivisor(4, 1);
    gl->glVertexAttribDivisor(5, 1);
    gl->glVertexAttribDivisor(6, 1);
    gl->glVertexAttribDivisor(7, 1);

    // Perform the indirect draw call
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
    gl->glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0);

    // Cleanup
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    gl->glBindVertexArray(0);

    // Reset attribute divisors for the next draw call
    for (int i = 2; i <= 7; ++i) {
        gl->glVertexAttribDivisor(i, 0);
        gl->glDisableVertexAttribArray(i);
    }
}