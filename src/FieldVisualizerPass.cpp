#include "FieldVisualizerPass.hpp"
#include "PrimitiveBuilders.hpp" // Your existing file!
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include <QOpenGLContext>
#include <QDebug>
#include <random>

// --- Class Implementation ---

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

void FieldVisualizerPass::renderParticles(const RenderFrameContext & context, FieldVisualizerComponent & vis, const TransformComponent & xf,
    Shader * renderShader, Shader * computeShader, GLuint uboID, GLuint ssboID)
{
    auto* gl = context.gl;

    // The check for shader validity is now done in the execute() function before this is called.

    // --- 1. Resource Creation (if needed) ---
    // This logic is self-contained and correct as-is.
    auto& settings = vis.particleSettings;
    if (vis.particleBuffer[0] == 0 || vis.isGpuDataDirty) {
        // Clean up old buffers if they exist.
        if (vis.particleBuffer[0] != 0) {
            gl->glDeleteBuffers(2, vis.particleBuffer);
            gl->glDeleteVertexArrays(1, &vis.particleVAO);
        }

        // Create and populate the initial particle data on the CPU.
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

        // Generate and upload data to the GPU buffers.
        gl->glGenBuffers(2, vis.particleBuffer);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);
        gl->glGenVertexArrays(1, &vis.particleVAO);
    }

    // --- 2. Compute Pass (Particle Simulation) ---
    computeShader->use(gl); // USE PARAMETER
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID); // USE PARAMETER
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID); // USE PARAMETER
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]);

    // Use the computeShader parameter for all uniform setting
    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_deltaTime", context.deltaTime);
    computeShader->setFloat(gl, "u_time", context.elapsedTime);
    computeShader->setVec3(gl, "u_boundsMin", vis.bounds.min);
    computeShader->setVec3(gl, "u_boundsMax", vis.bounds.max);
    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));

    gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // --- 3. Render Pass (Drawing Particles) ---
    gl->glEnable(GL_PROGRAM_POINT_SIZE);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gl->glDepthMask(GL_FALSE);

    renderShader->use(gl); // USE PARAMETER
    renderShader->setMat4(gl, "u_view", context.view);
    renderShader->setMat4(gl, "u_projection", context.projection);

    gl->glBindVertexArray(vis.particleVAO);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.particleBuffer[1 - vis.currentReadBuffer]);

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, color));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, size));

    gl->glDrawArrays(GL_POINTS, 0, settings.particleCount);
    gl->glBindVertexArray(0);

    // --- 4. Restore GL State ---
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_PROGRAM_POINT_SIZE);

    // --- 5. Ping-Pong Buffers ---
    vis.currentReadBuffer = 1 - vis.currentReadBuffer;
}

void FieldVisualizerPass::renderFlow(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
    Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID)
{
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // The check for valid shaders is now done in execute() before this is called.

    // --- 1. Resource Creation (if needed) ---
    // This logic is self-contained and correct as-is.
    auto& settings = vis.flowSettings;
    if (vis.particleBuffer[0] == 0 || vis.isGpuDataDirty) {
        if (vis.particleBuffer[0] != 0) {
            gl->glDeleteBuffers(2, vis.particleBuffer);
            if (vis.gpuData.instanceDataSSBO) gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
        }

        // Create initial particle data on the CPU.
        std::vector<Particle> particles(settings.particleCount);
        // This logic remains the same. The user has indicated it's correct.
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


        // Create the double-buffered particle SSBOs.
        gl->glGenBuffers(2, vis.particleBuffer);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[0]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.particleBuffer[1]);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);

        // Create the output buffer for the arrow instance data.
        if (vis.gpuData.instanceDataSSBO == 0) gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, settings.particleCount * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);
    }

    // --- 2. Compute Pass (Simulate Particles & Generate Arrow Instances) ---
    computeShader->use(gl); // USE PARAMETER
    // Bind all necessary buffers for the compute shader.
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID); // USE PARAMETER
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID); // USE PARAMETER
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, vis.particleBuffer[vis.currentReadBuffer]);      // Read
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, vis.particleBuffer[1 - vis.currentReadBuffer]); // Write
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, vis.gpuData.instanceDataSSBO);                  // Instance Data Output

    // Set all uniforms for the compute shader.
    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_deltaTime", context.deltaTime);
    computeShader->setFloat(gl, "u_time", context.elapsedTime);
    // ... set all other flow settings uniforms on 'computeShader' ...
    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));

    // Dispatch the compute shader.
    gl->glDispatchCompute(settings.particleCount / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); // Ensure compute is done before rendering.

    // --- 3. Render Pass (Draw Instanced Arrows) ---
    renderShader->use(gl); // USE PARAMETER
    renderShader->setMat4(gl, "view", context.view);
    renderShader->setMat4(gl, "projection", context.projection);

    // Bind the VAO for the arrow primitive (specific to this context).
    gl->glBindVertexArray(m_arrowVAOs[ctx]);
    // Bind the instance data buffer we just generated.
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);

    // Set up the instanced vertex attributes.
    const GLsizei stride = sizeof(InstanceData);
    const GLsizei vec4Size = sizeof(glm::vec4);
    // modelMatrix (mat4) requires 4 attribute pointers.
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

    // Tell OpenGL that these attributes are per-instance, not per-vertex.
    gl->glVertexAttribDivisor(2, 1);
    gl->glVertexAttribDivisor(3, 1);
    gl->glVertexAttribDivisor(4, 1);
    gl->glVertexAttribDivisor(5, 1);
    gl->glVertexAttribDivisor(6, 1);

    // Draw the arrows.
    gl->glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_arrowIndexCounts[ctx]), GL_UNSIGNED_INT, 0, settings.particleCount);

    // --- 4. Cleanup and State Reset ---
    gl->glBindVertexArray(0);
    // Reset attribute divisors for next draw calls.
    gl->glVertexAttribDivisor(2, 0);
    gl->glVertexAttribDivisor(3, 0);
    gl->glVertexAttribDivisor(4, 0);
    gl->glVertexAttribDivisor(5, 0);
    gl->glVertexAttribDivisor(6, 0);

    // --- 5. Ping-Pong Buffers ---
    vis.currentReadBuffer = 1 - vis.currentReadBuffer;
}

void FieldVisualizerPass::renderArrows(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
    Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID)
{
    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // The check for valid shaders is now done in execute() before this is called.

    // --- 1. Resource Creation (if settings have changed) ---
    auto& settings = vis.arrowSettings;
    if (vis.isGpuDataDirty) {
        // Clean up old buffers before creating new ones.
        if (vis.gpuData.samplePointsSSBO) gl->glDeleteBuffers(1, &vis.gpuData.samplePointsSSBO);
        if (vis.gpuData.instanceDataSSBO) gl->glDeleteBuffers(1, &vis.gpuData.instanceDataSSBO);
        if (vis.gpuData.commandUBO) gl->glDeleteBuffers(1, &vis.gpuData.commandUBO);

        // Calculate the grid of points where the field will be sampled.
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
            // Upload sample points to a new SSBO.
            gl->glGenBuffers(1, &vis.gpuData.samplePointsSSBO);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.samplePointsSSBO);
            gl->glBufferData(GL_SHADER_STORAGE_BUFFER, samplePoints.size() * sizeof(glm::vec4), samplePoints.data(), GL_STATIC_DRAW);

            // Create the output buffer for instance data (to be filled by the compute shader).
            gl->glGenBuffers(1, &vis.gpuData.instanceDataSSBO);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, vis.gpuData.instanceDataSSBO);
            gl->glBufferData(GL_SHADER_STORAGE_BUFFER, vis.gpuData.numSamplePoints * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

            // Create the indirect draw command buffer.
            // The compute shader will update the instanceCount member of this command.
            DrawElementsIndirectCommand cmd = { (GLuint)m_arrowIndexCounts[ctx], 0, 0, 0, 0 };
            gl->glGenBuffers(1, &vis.gpuData.commandUBO);
            gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
            gl->glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), &cmd, GL_DYNAMIC_DRAW);
        }
    }

    // If there are no points to sample, there's nothing more to do.
    if (vis.gpuData.numSamplePoints == 0) return;

    // --- 2. Compute Pass (Generate Arrow Instance Data) ---
    // Before running, reset the instance count in the command buffer to zero.
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);
    GLuint zero = 0;
    gl->glBufferSubData(GL_DRAW_INDIRECT_BUFFER, sizeof(GLuint), sizeof(GLuint), &zero);

    computeShader->use(gl); // USE PARAMETER
    // Bind all buffers for the compute shader.
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vis.gpuData.samplePointsSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vis.gpuData.instanceDataSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, vis.gpuData.commandUBO);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, uboID);      // USE PARAMETER
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboID); // USE PARAMETER

    // Set uniforms.
    computeShader->setMat4(gl, "u_visualizerModelMatrix", xf.getTransform());
    computeShader->setFloat(gl, "u_vectorScale", settings.vectorScale);
    computeShader->setFloat(gl, "u_arrowHeadScale", settings.headScale);
    computeShader->setFloat(gl, "u_cullingThreshold", settings.cullingThreshold);
    computeShader->setInt(gl, "u_pointEffectorCount", static_cast<int>(m_pointEffectors.size()));
    computeShader->setInt(gl, "u_directionalEffectorCount", static_cast<int>(m_directionalEffectors.size()));
    computeShader->setInt(gl, "u_triangleEffectorCount", static_cast<int>(m_triangleEffectors.size()));

    // Dispatch the compute shader.
    gl->glDispatchCompute((GLuint)vis.gpuData.numSamplePoints / 256 + 1, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

    // --- 3. Render Pass (Indirectly Draw Instanced Arrows) ---
    renderShader->use(gl); // USE PARAMETER
    renderShader->setMat4(gl, "view", context.view);
    renderShader->setMat4(gl, "projection", context.projection);

    gl->glBindVertexArray(m_arrowVAOs[ctx]);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vis.gpuData.instanceDataSSBO);
    const GLsizei stride = sizeof(InstanceData);
    const GLsizei vec4Size = sizeof(glm::vec4);
    // modelMatrix (mat4) requires 4 attribute pointers.
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

    gl->glVertexAttribDivisor(2, 1); // etc.
    gl->glVertexAttribDivisor(3, 1);
    gl->glVertexAttribDivisor(4, 1);
    gl->glVertexAttribDivisor(5, 1);
    gl->glVertexAttribDivisor(6, 1);

    // Bind the command buffer for the indirect draw.
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vis.gpuData.commandUBO);

    // The magic happens here: glDrawElementsIndirect reads all its parameters
    // (instance count, index count, etc.) directly from the command buffer
    // that the compute shader just filled.
    gl->glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0);

    // --- 4. Cleanup and State Reset ---
    gl->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    gl->glBindVertexArray(0);

    // Reset the attribute divisors back to 0 (per-vertex) for the next pass.
    gl->glVertexAttribDivisor(2, 0);
    gl->glVertexAttribDivisor(3, 0);
    gl->glVertexAttribDivisor(4, 0);
    gl->glVertexAttribDivisor(5, 0);
    gl->glVertexAttribDivisor(6, 0);
}
