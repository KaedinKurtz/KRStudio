#pragma once

#include "IFluidSolver.hpp"
#include "FluidCache.hpp"

#include <QtGui/qopengl.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

class QOpenGLFunctions_4_3_Core;
class RenderingSystem;

/// Physical fluid parameters, SI units where applicable.
struct FluidParams {
    float restDensity = 998.2f;    // kg/m^3 (water, 20 °C)
    float viscosity = 0.05f;       // XSPH blend coefficient (PBF backend; not a real unit)
    float dynamicViscosityPaS = 1.002e-3f; // Pa·s (DFSPH backend; water 20 °C)
    float surfaceTensionNpm = 0.0728f;     // N/m  (DFSPH backend; water-air)
    int solverIterations = 3;      // incompressibility enforcement (higher = stiffer water)
    float particleRadius = 0.025f; // m = REST SPACING d_rest (render radius too); h = 2x
    glm::vec3 gravity = { 0.0f, -9.81f, 0.0f }; // m/s^2
};

enum class FluidRenderMode {
    Particles,    // raw solver particles (debug view)
    WaterSurface, // screen-space fluid: filtered depth + refraction + IBL
};

/// Appearance of the rendered fluid. In WaterSurface mode every control maps
/// to a physical quantity in the screen-space composite:
///   color      -> per-channel Beer-Lambert absorption spectrum
///   turbidity  -> scattering (refraction fades to body color) + rough IBL
///   emissivity -> additive emission
///   foaminess  -> whitewater amount (diffuse particles)
///   ior        -> Fresnel F0 + refraction strength (water 1.333)
struct FluidAppearance {
    glm::vec3 color = { 0.10f, 0.40f, 0.75f }; // transmission colour
    float turbidity = 0.25f;       // 0 = clear .. 1 = murky
    float emissivity = 0.0f;       // additive glow
    float foaminess = 0.5f;        // speed-driven white-water amount
    float sizeScale = 1.0f;        // particle sprite size multiplier
    float ior = 1.333f;            // index of refraction
    float absorptionScale = 1.5f;  // Beer-Lambert strength (per metre)
    float refractScale = 0.08f;    // UV offset per metre of thickness
    int smoothIterations = 3;      // narrow-range filter iterations
    int surfaceQuality = 0;        // 0 Low (iGPU-safe) / 1 High (aniso splats, wide kernel)
    float foamScale = 1.0f;        // global whitewater emission gain
    float foamDecay = 1.0f;        // foam lifetime divisor (>1 = dies faster)
    FluidRenderMode renderMode = FluidRenderMode::WaterSurface;
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
    static constexpr int kMaxParticles = 200000;
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
        if (m_externalSolver) m_externalSolver->setPlaying(playing);
    }
    void reset()
    {
        m_particleCount = 0;
        m_volumesSeeded = false;
        m_emitAccumulator = 0.0f;
        if (m_externalSolver) m_externalSolver->reset();
    }

    int particleCount() const { return m_particleCount; }
    GLuint particleBuffer() const { return m_particleSSBO; }
    float particleRadius() const { return m_params.particleRadius; }

    // Whitewater (Ihmsen-style diffuse particles), maintained by compute
    // passes while the PBF backend plays; rendered by FluidSurfacePass.
    static constexpr int kMaxDiffuse = 100000;
    GLuint diffuseBuffer() const { return m_diffuseSSBO; }
    GLuint anisoBuffer() const { return m_anisoSSBO; }
    /// True once the anisotropy pass has run for the current particle set.
    bool anisoValid() const { return m_anisoValid; }

    /// CPU mirror of particle positions (xyz, w=life), refreshed periodically
    /// when KRS_BENCH or KRS_AUTOPLAY is set. Used by telemetry/benchmarks.
    const std::vector<glm::vec4>& sampledPositions() const { return m_positionMirror; }

    // --- Live-tunable parameters (panels write these) ---
    FluidParams& params() { return m_params; }
    const FluidParams& params() const { return m_params; }
    FluidAppearance& appearance() { return m_appearance; }
    const FluidAppearance& appearance() const { return m_appearance; }

    // --- Solver backend selection ---
    /// Requested tier ("Auto" by default); resolved against hardware caps.
    FluidBackend requestedBackend() const { return m_requestedBackend; }
    void setRequestedBackend(FluidBackend b) { m_requestedBackend = b; }
    /// The tier actually stepping this frame.
    FluidBackend activeBackend() const;
    /// Install an external solver (e.g. the DFSPH reference backend).
    void setExternalSolver(FluidBackend tier, std::unique_ptr<IFluidSolver> solver);
    IFluidSolver* externalSolver() const { return m_externalSolver.get(); }

    // --- Two-way coupling: fluid -> rigid impulses ---
    /// Called once per frame per collider entity the fluid pushed against,
    /// with the net impulse [N·s] the fluid exerted (reaction to the
    /// boundary displacement). Wired to SimulationController by the UI.
    using ImpulseSink = std::function<void(entt::entity, const glm::vec3&)>;
    void setImpulseSink(ImpulseSink sink) { m_impulseSink = std::move(sink); }

    // --- Houdini-style bake & scrub ---
    /// While recording, every rendered frame of the playing sim is written
    /// to the cache directory; scrubbing replays frames through the same
    /// particle SSBO the renderer consumes.
    FluidCache& cache() { return m_cache; }
    bool isRecording() const { return m_recording; }
    void setRecording(bool on);
    /// Thread-safe-enough deferred scrub: applied on the engine context at
    /// the next update() tick.
    void requestScrubFrame(int frame) { m_pendingScrubFrame = frame; }

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
    float m_h = 0.05f;              // smoothing radius = 2 * particleRadius (grid-coupled)
    float m_builtRadius = 0.0f;     // radius the grid was built for (rebuild on change)
    FluidParams m_params;
    FluidAppearance m_appearance;

    void rebuildGrid(QOpenGLFunctions_4_3_Core* gl);
    /// Ellipsoid fits for anisotropic splats (High surface quality).
    /// rebuildGridFirst re-inserts particles (scrub playback: grid is stale).
    void dispatchAniso(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                       bool rebuildGridFirst);

    // --- Backend selection ---
    FluidBackend m_requestedBackend = FluidBackend::Auto;
    FluidBackend m_externalTier = FluidBackend::Auto; // tier m_externalSolver implements
    std::unique_ptr<IFluidSolver> m_externalSolver;

    // Simulation domain (uniform grid bounds). Smaller than the old ±8x10:
    // cell count scales with 1/h³ and h halved for real-fluid resolution.
    glm::vec3 m_domainMin = { -5.0f, 0.0f, -5.0f };
    glm::vec3 m_domainMax = { 5.0f, 8.0f, 5.0f };
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
    GLuint m_diffuseSSBO = 0;   // {vec4 posLife; vec4 velType;} * kMaxDiffuse
    GLuint m_diffuseCounterSSBO = 0; // monotonic ring cursor
    uint32_t m_foamFrame = 0;

    // --- Two-way coupling ---
    GLuint m_impulseSSBO = 0;   // ivec4 per collider slot (fixed-point 1e7)
    GLuint m_anisoSSBO = 0;     // {vec4 quat; vec4 radiiN; vec4 center;} * kMaxParticles
    bool m_anisoValid = false;
    std::vector<entt::entity> m_boxEntities;    // collider slot -> entity
    std::vector<entt::entity> m_sphereEntities;
    ImpulseSink m_impulseSink;

    // --- Bake & scrub ---
    FluidCache m_cache;
    bool m_recording = false;
    int m_recordFrame = 0;
    double m_recordTime = 0.0;
    int m_pendingScrubFrame = -1;
    std::vector<float> m_recordScratch;
};
