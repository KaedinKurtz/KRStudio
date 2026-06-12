#pragma once

#include <QtGui/qopengl.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

class QOpenGLFunctions_4_3_Core;
class RenderingSystem;

/// Physical fluid parameters, SI units where applicable.
struct FluidParams {
    float restDensity = 1000.0f;   // kg/m^3 (water)
    float viscosity = 0.05f;       // XSPH blend coefficient (0 = inviscid, ~0.5 = honey-like)
    int solverIterations = 3;      // incompressibility enforcement (higher = stiffer water)
    float particleRadius = 0.035f; // m (render + collision radius)
    glm::vec3 gravity = { 0.0f, -9.81f, 0.0f }; // m/s^2
};

/// Artistic appearance of the rendered fluid ("user" mode).
struct FluidAppearance {
    glm::vec3 color = { 0.10f, 0.40f, 0.75f }; // base water colour
    float turbidity = 0.4f;   // 0 = clear/glossy .. 1 = murky/flat
    float emissivity = 0.0f;  // additive glow
    float foaminess = 0.5f;   // speed-driven white-water amount
    float sizeScale = 1.0f;   // particle sprite size multiplier
};

/**
 * @brief GPU Position-Based Fluids (Macklin & Müller 2013) on compute shaders.
 *
 * Owned by the RenderingSystem; every GL call runs on the engine context
 * inside renderAllViewports(). The simulation lifecycle mirrors the rigid
 * body SimulationController: while "playing" it emits from
 * FluidEmitterComponent entities and steps the solver; on stop the particle
 * pool resets and FluidVolumeComponent boxes are re-seeded on next play.
 *
 * Collision: implicit ground plane (y=0) plus oriented boxes and spheres
 * gathered each frame from entities carrying Box/SphereCollider — so a
 * kinematic glass can slosh the water inside it.
 */
class FluidSystem
{
public:
    static constexpr int kMaxParticles = 100000;
    static constexpr int kMaxBoxes = 32;
    static constexpr int kMaxSpheres = 32;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);

    /// Called once per engine frame (engine context current).
    void update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                entt::registry& registry, float dt);

    static constexpr int kMaxSdfColliders = 4;

    void setPlaying(bool playing)
    {
        if (playing && !m_playing) m_sdfsBaked = false; // re-bake on each play
        m_playing = playing;
    }
    void reset() { m_particleCount = 0; m_volumesSeeded = false; m_emitAccumulator = 0.0f; }

    int particleCount() const { return m_particleCount; }
    GLuint particleBuffer() const { return m_particleSSBO; }
    float particleRadius() const { return m_params.particleRadius; }

    /// CPU mirror of particle positions (xyz, w=life), refreshed periodically
    /// when KRS_BENCH or KRS_AUTOPLAY is set. Used by telemetry/benchmarks.
    const std::vector<glm::vec4>& sampledPositions() const { return m_positionMirror; }

    // --- Live-tunable parameters (panels write these) ---
    FluidParams& params() { return m_params; }
    const FluidParams& params() const { return m_params; }
    FluidAppearance& appearance() { return m_appearance; }
    const FluidAppearance& appearance() const { return m_appearance; }

private:
    struct SdfCollider {
        GLuint texture = 0;       // R32F 3D texture of signed distances (world units)
        glm::vec3 aabbMin{ 0 };
        glm::vec3 aabbMax{ 0 };
        // CPU copy kept for telemetry/penetration checks under test hooks
        std::vector<float> cpuField;
        glm::ivec3 dims{ 0 };
    };

    void bakeSdfColliders(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry);
    void seedVolumes(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry);
    void emitFromEmitters(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry, float dt);
    void uploadColliders(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry);
    void appendParticles(QOpenGLFunctions_4_3_Core* gl, const std::vector<glm::vec4>& posLife,
                         const std::vector<glm::vec4>& vel);

    bool m_initialized = false;
    bool m_playing = false;
    bool m_volumesSeeded = false;
    int m_particleCount = 0;
    float m_emitAccumulator = 0.0f;

    // --- Tunables (PBF) ---
    float m_h = 0.10f;              // smoothing radius (m); fixed (grid-coupled)
    FluidParams m_params;
    FluidAppearance m_appearance;

    // Simulation domain (uniform grid bounds)
    glm::vec3 m_domainMin = { -8.0f, 0.0f, -8.0f };
    glm::vec3 m_domainMax = { 8.0f, 10.0f, 8.0f };
    glm::ivec3 m_gridDim = { 0, 0, 0 };

    std::vector<glm::vec4> m_positionMirror;

    // --- SDF colliders (baked from meshes at Play via OpenVDB) ---
    bool m_sdfsBaked = false;
    std::vector<SdfCollider> m_sdfColliders;

    // --- GPU resources (engine context) ---
    GLuint m_particleSSBO = 0;  // {vec4 posLife; vec4 vel; vec4 pred;} * kMaxParticles
    GLuint m_gridHeadSSBO = 0;  // int per cell
    GLuint m_gridNextSSBO = 0;  // int per particle
    GLuint m_colliderUBO = 0;   // boxes + spheres, std140
};
