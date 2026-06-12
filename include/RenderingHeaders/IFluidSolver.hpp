#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

class QOpenGLFunctions_4_3_Core;
class RenderingSystem;

/// Fluid solver tiers. "Auto" resolves to the best tier the hardware
/// supports: PhysX GPU PBD on CUDA machines (interactive, unitless
/// coefficients), GL-compute PBF everywhere else (interactive), and
/// SPlisHSPlasH DFSPH as the CPU reference tier (real SI units, slower).
enum class FluidBackend {
    Auto,
    PbfGpu,    // built-in GL compute Position-Based Fluids
    DfsphCpu,  // SPlisHSPlasH DFSPH (reference fidelity, real units)
    PhysxGpu,  // PhysX 5 GPU PBD (CUDA only; reserved)
};

const char* fluidBackendName(FluidBackend backend);

/**
 * @brief A pluggable fluid solver. The FluidSystem façade owns the GPU
 * particle buffer that rendering consumes; external backends fill it
 * (CPU solvers upload, GPU solvers write in place). All methods run on
 * the engine GL context.
 */
class IFluidSolver
{
public:
    virtual ~IFluidSolver() = default;

    virtual const char* name() const = 0;
    virtual bool available() const = 0;

    virtual void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) = 0;
    virtual void shutdown(QOpenGLFunctions_4_3_Core* gl) = 0;

    /// Advance the simulation and publish particle positions into the
    /// shared particle SSBO. Returns the live particle count.
    virtual int update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                       entt::registry& registry, float dt) = 0;

    virtual void setPlaying(bool playing) = 0;
    virtual void reset() = 0;

    /// Optional: copy current particle positions (xyz, w = life) for
    /// telemetry/benchmarks. CPU backends have these for free.
    virtual void samplePositions(std::vector<glm::vec4>& out) const { out.clear(); }
};
