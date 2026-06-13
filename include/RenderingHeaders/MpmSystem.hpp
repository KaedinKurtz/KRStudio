#pragma once

#include <QtGui/qopengl.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

class QOpenGLFunctions_4_3_Core;
class RenderingSystem;

/**
 * @brief GPU MLS-MPM (Moving Least Squares Material Point Method, Hu et al.
 * 2018) — a unified continuum solver covering fluids, elastic solids,
 * granular sand and snow in ONE framework. Lagrangian particles carry mass,
 * velocity, the APIC affine matrix C and the deformation gradient F; a
 * background uniform grid handles momentum exchange and (implicitly via the
 * constitutive stress) incompressibility.
 *
 * Per substep: clear grid -> P2G (scatter mass + APIC momentum + stress) ->
 * grid update (velocity, gravity, wall BC) -> G2P (gather velocity + C,
 * advance x, update F, plastic return-map) . Runs on the engine GL context
 * inside renderAllViewports, mirroring SmokeSystem / FluidSystem.
 *
 * The grid scatter uses int32 fixed-point atomicAdd (GL 4.3 has no float
 * atomics) — the proven pattern from the PBF impulse/compaction buffers.
 */
class MpmSystem
{
public:
    static constexpr int kMaxParticles = 240000;
    // std430 particle stride: 12 vec4 (pos/mass, vel/vol, C cols x3, F cols x3,
    // plastic/thermo, material, color, therm2{k}). Keep in sync with the shaders.
    static constexpr int kFloatsPerParticle = 12 * 4;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);
    void update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                entt::registry& registry, float dt);

    void setPlaying(bool playing) { m_playing = playing; if (!playing) m_seeded = false; }
    void reset() { m_seeded = false; }

    bool active() const { return m_initialized && m_particleCount > 0; }
    int particleCount() const { return m_particleCount; }
    GLuint particleBuffer() const { return m_particleSSBO; }
    int gridN() const { return m_N; }
    glm::vec3 origin() const { return m_origin; }
    glm::vec3 extent() const { return m_size; }
    float particleRadius() const { return m_renderRadius; }

    // --- Visualization (Phase 3): recolor particle splats by a physics scalar.
    // The scalar is computed per-particle in the render shader (Default uses the
    // body albedo); ranges are configurable or one-shot auto-calibrated.
    enum class VizMode : int { Default = 0, Thermal = 1, VonMises = 2, Strain = 3 };
    struct Appearance {
        VizMode mode = VizMode::Default;
        bool autoRange = true;                 // recalibrate range on mode change
        glm::vec2 thermal{ 0.0f, 100.0f };     // °C
        glm::vec2 vonMises{ 0.0f, 1.0e5f };    // Pa (StVK von Mises proxy)
        glm::vec2 strain{ 0.0f, 0.25f };       // || Green-Lagrange strain ||
    };
    Appearance& appearance() { return m_appearance; }
    void setVizMode(VizMode m) { m_appearance.mode = m; m_calibratePending = m_appearance.autoRange; }
    glm::vec2 vizRange() const;                // active mode's [min, max]
    // Expand a mode's range to include r, so MPM particles and FEM bodies share one
    // dynamic range (FemSystem unions its field range here after MPM calibration).
    void unionVizRange(VizMode m, glm::vec2 r) {
        glm::vec2& d = (m == VizMode::Thermal) ? m_appearance.thermal
                     : (m == VizMode::Strain)  ? m_appearance.strain : m_appearance.vonMises;
        d.x = std::min(d.x, r.x); d.y = std::max(d.y, r.y);
    }

    // Live diagnostics (filled under KRS_MPM_SELFTEST / autoplay).
    struct Diag {
        double totalMass = 0.0;
        glm::dvec3 comVelocity{ 0.0 };
        glm::dvec3 comPosition{ 0.0 };
        float maxSpeed = 0.0f;
        int live = 0;
        double tempMean = 0.0;   // mass-weighted mean temperature
        float tempMin = 0.0f;
        float tempMax = 0.0f;
        int fluidCount = 0;      // particles currently of Fluid type (post-melt)
        float minY = 0.0f;       // lowest particle-centre y (floor-contact check)
    };
    Diag sample(QOpenGLFunctions_4_3_Core* gl);

    // Headless fidelity suite: asserts the solver against analytic ground
    // truth (mass conservation, free-fall kinematics, rest stability, sand
    // angle of repose). Returns true iff all tests pass. Runs on the engine
    // context; restores empty state afterwards so the live scene re-seeds.
    bool runSelfTests(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl);

private:
    void allocate(QOpenGLFunctions_4_3_Core* gl);
    void seedBodies(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry);
    void computeDomain(entt::registry& registry);
    void autoCalibrate(QOpenGLFunctions_4_3_Core* gl); // one-shot CPU min/max for the active viz mode
    void collectHeatSources(entt::registry& registry);  // HeatSourceComponent -> uniforms + emissive tint
    // One MLS-MPM substep (clear grid -> P2G -> grid -> G2P) on the bound
    // particle/grid buffers. Shared by update() and the self-test.
    void runSubstep(QOpenGLFunctions_4_3_Core* gl, class Shader* p2g, class Shader* grid,
                    class Shader* g2p, float sdt, const glm::vec3& gravity,
                    float dx, float invDx);
    // One thermal step (heat scatter -> normalize -> diffuse -> gather + phase
    // change), run once per frame. No-op if thermal shaders are missing.
    void runThermalStep(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl, float dtFrame);

    int m_N = 64;                       // grid cells per axis
    glm::vec3 m_origin{ -1.5f, 0.0f, -1.5f };
    glm::vec3 m_size{ 3.0f, 3.0f, 3.0f };

    GLuint m_particleSSBO = 0;          // kFloatsPerParticle * kMaxParticles
    GLuint m_gridIntSSBO = 0;           // int[cellCount*4] fixed-point momentum+mass
    GLuint m_gridVelSSBO = 0;           // vec4[cellCount] velocity + mass (float)
    GLuint m_gridThermSSBO = 0;         // int[cellCount*2] fixed-point (m*T, m)
    GLuint m_gridTempA = 0;             // float[cellCount] normalized temperature
    GLuint m_gridTempB = 0;             // float[cellCount] diffused temperature
    GLuint m_gridC = 0;                 // float[cellCount] node thermal mass C (J/K)
    GLuint m_gridK = 0;                 // float[cellCount] node conductivity k (W/m.K)
    GLuint m_heatAccumSSBO = 0;         // int[kMaxHeatSources] fixed-point sum(m*c_p) per source
    int m_particleCount = 0;

    // Thermodynamics (M4 / Phase 4.5): ambient field + Newton exchange + grid
    // Fourier conduction. m_conductionScale accelerates the physically-slow metal
    // conduction (alpha ~ 1e-5 m^2/s) to interactive rates; the per-cell stability
    // clamp keeps the explicit sweep bounded regardless. Env: KRS_MPM_COND_SCALE.
    float m_ambientT = 20.0f;           // °C ambient reservoir
    float m_conductivity = 0.5f;        // legacy diffusivity proxy (unused by Fourier path)
    float m_conductionScale = 2000.0f;  // dimensionless conduction speed multiplier S
    float m_heatExchange = 0.0f;        // 1/s exchange with ambient (0 = off)
    float m_fluidMeltK = 5.0e4f;        // bulk modulus assigned to melted fluid

    // Heat sources (Phase 3 / 4.5): HeatSourceComponent entities inject a
    // volumetric power (W) into the material in their radius (Neumann); the
    // smoke/flame grid is sampled too. m_heatSrc.w carries the nominal glow temp.
    static constexpr int kMaxHeatSources = 8;
    glm::vec4 m_heatSrc[kMaxHeatSources]{}; // xyz world pos, w nominal temperature (C)
    float m_heatRadius[kMaxHeatSources]{};  // m influence radius
    float m_heatPower[kMaxHeatSources]{};   // W volumetric heat-generation rate
    int m_heatCount = 0;

    bool m_initialized = false;
    bool m_playing = false;
    bool m_seeded = false;

    glm::vec3 m_gravity{ 0.0f, -9.81f, 0.0f };
    float m_maxWaveSpeed = 10.0f;       // max sqrt(stiffness/density), for CFL
    float m_floorFriction = 0.4f;       // Coulomb friction at the grid floor (Phase 5)
    float m_renderRadius = 0.02f;       // point-sprite radius (= dx; floor-contact offset)
    std::vector<float> m_seedScratch;   // CPU staging for seeding

    Appearance m_appearance;            // Phase 3 visualization mode + ranges
    bool m_calibratePending = false;    // run a range calibration next update
    unsigned m_vizFrame = 0;            // for periodic dynamic-range recalibration
};
