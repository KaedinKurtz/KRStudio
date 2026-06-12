#pragma once

#include "IFluidSolver.hpp"

#include <memory>

/**
 * @brief SPlisHSPlasH DFSPH backend — the reference-fidelity CPU tier.
 *
 * Divergence-free SPH (Bender & Koschier) with REAL SI units end to end:
 * rest density [kg/m³], dynamic viscosity [Pa·s] (Weiler et al. 2018
 * implicit solver, fed kinematic ν = μ/ρ₀), surface tension [N/m]
 * (Akinci et al. 2013), adaptive CFL timestepping capped at the master
 * 1/240 step. Boundaries are Akinci2012 boundary particles sampled from
 * the ground plane and every Box collider in the scene; fluid volumes and
 * emitters come from the same components the PBF backend uses.
 *
 * Runs synchronously inside the engine frame with a substep budget: when
 * the CPU can't keep up, simulation time dilates instead of stalling the
 * UI (the bake-and-scrub workflow is the answer for big particle counts).
 * Particle positions are uploaded into the FluidSystem's shared SSBO, so
 * rendering is identical across backends.
 */
class DfsphBackend : public IFluidSolver
{
public:
    DfsphBackend();
    ~DfsphBackend() override;

    const char* name() const override { return "DFSPH (SPlisHSPlasH)"; }
    bool available() const override;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void shutdown(QOpenGLFunctions_4_3_Core* gl) override;

    int update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
               entt::registry& registry, float dt) override;

    void setPlaying(bool playing) override;
    void reset() override;
    void samplePositions(std::vector<glm::vec4>& out) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
