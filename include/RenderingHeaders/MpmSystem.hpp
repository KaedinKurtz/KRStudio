#pragma once

#include <QtGui/qopengl.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
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
    // std430 particle stride: 11 vec4 (pos/mass, vel/vol, C rows x3,
    // F rows x3, plastic/thermo, material). Keep in sync with the shaders.
    static constexpr int kFloatsPerParticle = 11 * 4;

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

    // Live diagnostics (filled under KRS_MPM_SELFTEST / autoplay).
    struct Diag {
        double totalMass = 0.0;
        glm::dvec3 comVelocity{ 0.0 };
        glm::dvec3 comPosition{ 0.0 };
        float maxSpeed = 0.0f;
        int live = 0;
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
    // One MLS-MPM substep (clear grid -> P2G -> grid -> G2P) on the bound
    // particle/grid buffers. Shared by update() and the self-test.
    void runSubstep(QOpenGLFunctions_4_3_Core* gl, class Shader* p2g, class Shader* grid,
                    class Shader* g2p, float sdt, const glm::vec3& gravity,
                    float dx, float invDx);

    int m_N = 64;                       // grid cells per axis
    glm::vec3 m_origin{ -1.5f, 0.0f, -1.5f };
    glm::vec3 m_size{ 3.0f, 3.0f, 3.0f };

    GLuint m_particleSSBO = 0;          // kFloatsPerParticle * kMaxParticles
    GLuint m_gridIntSSBO = 0;           // int[cellCount*4] fixed-point momentum+mass
    GLuint m_gridVelSSBO = 0;           // vec4[cellCount] velocity + mass (float)
    int m_particleCount = 0;

    bool m_initialized = false;
    bool m_playing = false;
    bool m_seeded = false;

    glm::vec3 m_gravity{ 0.0f, -9.81f, 0.0f };
    float m_maxWaveSpeed = 10.0f;       // max sqrt(stiffness/density), for CFL
    float m_renderRadius = 0.02f;       // point-sprite radius (~half spacing)
    std::vector<float> m_seedScratch;   // CPU staging for seeding
};
