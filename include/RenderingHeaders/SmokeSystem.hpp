#pragma once

#include <QtGui/qopengl.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

class QOpenGLFunctions_4_3_Core;
class RenderingSystem;

/// Live-tunable gas parameters (the Smoke panel writes these).
struct SmokeParams {
    float buoyancy = 2.2f;            // upward accel per unit (temperature - ambient)
    float densityWeight = 0.10f;      // downward pull per unit density (smoke has mass)
    float cooling = 0.7f;             // temperature decay (per second)
    float densityDissipation = 0.10f; // density fade (per second)
    float vorticity = 8.0f;           // vorticity-confinement strength (turbulent detail)
    float ambientTemperature = 0.0f;
    int   pressureIterations = 28;    // Jacobi sweeps
    float burnRate = 1.6f;            // fuel -> heat+soot conversion (per second), fire only
    glm::vec3 smokeColor = { 0.62f, 0.62f, 0.66f };
    float densityScale = 42.0f;       // optical density multiplier at render
    bool  fireEnabled = false;        // set by update() when a fuel emitter exists
};

/**
 * @brief Dense Eulerian (grid) gas solver for smoke and fire, on GL 4.3
 * compute. Mirrors FluidSystem's lifecycle: owned by RenderingSystem,
 * every GL call runs on the engine context inside renderAllViewports();
 * steps while "playing", reset on stop. Volumetric output (density +
 * temperature) is consumed by SmokePass.
 *
 * Pipeline per step (Fedkiw "stable fluids" + vorticity confinement):
 *   emit -> advect velocity -> curl -> confinement+buoyancy ->
 *   combust/cool/dissipate scalars -> divergence -> Jacobi pressure ->
 *   project (subtract gradient, free-slip walls) -> advect scalars.
 */
class SmokeSystem
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);
    void update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                entt::registry& registry, float dt);

    void setPlaying(bool playing) { m_playing = playing; if (!playing) m_needsZero = true; }
    void reset() { m_needsZero = true; m_domainReady = false; }

    /// True when there is gas worth rendering this frame.
    bool active() const { return m_initialized && m_hasEmitters; }
    int gridN() const { return m_N; }
    glm::vec3 origin() const { return m_origin; }
    glm::vec3 extent() const { return m_size; }
    /// Scalars volume (r=density, g=temperature, b=fuel) for the renderer.
    GLuint scalarsTexture() const { return m_scalars[m_sclCur]; }
    GLuint velocityTexture() const { return m_velocity[m_velCur]; }

    SmokeParams& params() { return m_params; }
    const SmokeParams& params() const { return m_params; }

private:
    void allocate(QOpenGLFunctions_4_3_Core* gl);
    void zeroAll(QOpenGLFunctions_4_3_Core* gl);
    void computeDomain(entt::registry& registry);

    int m_N = 96;                       // grid resolution per axis
    glm::vec3 m_origin{ -2.0f, 0.0f, -2.0f };
    glm::vec3 m_size{ 4.0f, 6.0f, 4.0f };

    GLuint m_velocity[2] = { 0, 0 };    // RGBA16F (xyz velocity)
    GLuint m_scalars[2] = { 0, 0 };     // RGBA16F (r density, g temperature, b fuel)
    GLuint m_pressure[2] = { 0, 0 };    // R32F
    GLuint m_divergence = 0;            // R32F
    GLuint m_curl = 0;                  // RGBA16F (xyz vorticity)
    int m_velCur = 0, m_sclCur = 0, m_prsCur = 0;

    bool m_initialized = false;
    bool m_playing = false;
    bool m_hasEmitters = false;
    bool m_needsZero = true;
    bool m_domainReady = false;

    SmokeParams m_params;
};
