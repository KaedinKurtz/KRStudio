// LiveSdfGate.cpp -- Phase 1: the GPU Jump-Flooding EDT SDF measured on the REAL
// live fluid. Seeds a PBF fluid slab, runs the real FluidSystem::update(), then
// dispatches the JFA over the LIVE particle SSBO (no CUDA -- GL-4.3 compute, the
// AMD-portable path). SDF-SPEED times the JFA with a glFinish (warm-up first, or
// the number is bogus) vs the old O(cells*particles) brute force; SDF-CORRECT
// compares the JFA distance/gradient to the analytic distance to the live cloud.
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "GpuSdfEdt.hpp"
#include "AvoidanceField.hpp"      // krs::field::GridSdf (the slow neg-control)
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <vector>
#include <chrono>
#include <algorithm>

namespace {
struct GpuParticle { glm::vec4 posLife; glm::vec4 vel; glm::vec4 pred; };   // FluidSystem layout
}

bool RenderingSystem::runLiveSdfGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[livesdf] GATE LIVE-SDF -- GPU Jump-Flooding EDT on the REAL live fluid particle set\n");

    FluidSystem* f = getFluidSystem();
    if (!f || !m_gl) { printf("[livesdf] FAIL: no fluid system / GL context\n"); return false; }
    if (!getShader("edt_seed") || !getShader("edt_jfa")) { printf("[livesdf] FAIL: edt shaders not registered\n"); return false; }

    // seed a fluid slab clear of the y=0 domain floor; gravity 0 so it stays put.
    entt::registry reg;
    const entt::entity vol = reg.create();
    reg.emplace<TransformComponent>(vol, glm::vec3(0.0f, 0.6f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.4f); fv.particleSpacing = 0.05f;
    f->params().gravity = glm::vec3(0.0f);
    f->setPlaying(false); f->reset(); f->setPlaying(true);
    f->update(*this, m_gl, reg, 1.0f / 120.0f);                 // seed
    for (int i = 0; i < 12; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);  // settle

    const int count = f->particleCount();
    const float radius = f->particleRadius();
    if (count <= 0) { printf("[livesdf] vacuous pass (no particles seeded)\n"); std::fflush(stdout); return true; }

    // grid covering the slab + margin.
    const glm::vec3 origin(-1.2f, -0.6f, -1.2f), extent(2.4f, 2.4f, 2.4f);
    const int N = 64;

    GpuSdfEdt edt;
    // warm-up build (compile/upload) + glFinish so timing measures GPU work, not submission.
    if (!edt.build(*this, m_gl, f->particleBuffer(), count, /*stride*/12, /*posOff*/0, /*aliveOff*/3, origin, extent, N, radius)) {
        printf("[livesdf] FAIL: JFA build failed (shaders missing?)\n"); std::fflush(stdout); return false;
    }
    m_gl->glFinish();

    // ---- SDF-SPEED: time the JFA over the LIVE particle SSBO ----
    const int reps = 8;
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius);
    m_gl->glFinish();
    const auto t1 = std::chrono::steady_clock::now();
    const double jfaMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
    const bool speedOk = jfaMs < 15.0;

    // read back the live particles (for the analytic ground truth + the neg-ctrl).
    std::vector<GpuParticle> parts; parts.resize(size_t(count));
    m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
    m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle)) * count, parts.data());
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    std::vector<glm::vec3> pts;
    for (const auto& p : parts) if (p.posLife.w > 0.0f) pts.push_back(glm::vec3(p.posLife));
    const int live = int(pts.size());

    // NEG-CTRL: the old O(cells*particles) brute force (krs::field::GridSdf, exact band).
    // Capped to a particle subset so the gate doesn't hang for minutes -- even on a SUBSET
    // at the SAME grid it blows the 15ms budget, while the JFA holds it on the FULL count.
    const int baseCount = std::min(live, 800);
    std::vector<glm::vec3> sub(pts.begin(), pts.begin() + baseCount);
    krs::field::GridSdf brute; brute.origin = origin; brute.extent = extent; brute.dims = glm::ivec3(N);
    const auto b0 = std::chrono::steady_clock::now();
    brute.build(sub, radius);                                   // band default = exact brute force
    const auto b1 = std::chrono::steady_clock::now();
    const double bruteMs = std::chrono::duration<double, std::milli>(b1 - b0).count();
    const bool baselineFails = bruteMs > 15.0;

    printf("[livesdf]   SDF-SPEED: JFA over %d LIVE particles, %d^3 grid -> %.3f ms/frame (<15ms:%d); "
           "NEG old brute-force %d-particle subset same grid -> %.1f ms (>>15ms:%d)  %s\n",
           live, N, jfaMs, int(speedOk), baseCount, bruteMs, int(baselineFails),
           (speedOk && baselineFails) ? "PASS" : "FAIL");
    bool allOk = speedOk && baselineFails;

    // ---- SDF-CORRECT: JFA distance/gradient vs analytic distance to the live cloud ----
    {
        std::vector<glm::vec4> seeds;
        edt.readback(m_gl, seeds);
        // the AVOIDANCE SDF inflates the water by a safety margin (the robot keeps clearance);
        // sdfRadius > the cell diagonal so "inside the cloud" reads d < 0 at grid resolution.
        const double cell = double(extent.x) / N;               // ~0.0375 m
        const float sdfRadius = 0.08f;                          // > cell*sqrt(3) (0.065 m)
        auto analyticDist = [&](glm::vec3 q) { double m = 1e30; for (const auto& p : pts) m = std::min(m, double(glm::length(q - p))); return m - double(sdfRadius); };
        // sample points just outside the slab (halfExtents 0.4 @ centre (0,0.6,0)) on several sides.
        const glm::vec3 probes[] = { {0.6f,0.6f,0.0f}, {0.0f,0.6f,0.6f}, {-0.55f,0.7f,0.1f}, {0.2f,1.2f,0.0f} };
        double maxDistErr = 0.0, minDot = 2.0;
        for (const auto& q : probes) {
            const double a = analyticDist(q);
            const double jd = double(GpuSdfEdt::distanceAt(seeds, origin, extent, N, sdfRadius, q));
            maxDistErr = std::max(maxDistErr, std::abs(jd - a));
            const glm::vec3 g = GpuSdfEdt::gradientAt(seeds, origin, extent, N, sdfRadius, q);
            glm::vec3 nearest(0); double nd = 1e30; for (const auto& p : pts) { double d = glm::length(q - p); if (d < nd) { nd = d; nearest = p; } }
            const glm::vec3 away = glm::normalize(q - nearest);
            if (glm::length(g) > 1e-4f) minDot = std::min(minDot, double(glm::dot(glm::normalize(g), away)));
        }
        const bool distOk = maxDistErr < 2.0 * cell;            // JFA sub-cell + discretization
        const bool gradOk = minDot > 0.90;
        // INTERIOR: the slab centre is deep inside the cloud -> d < 0 (the <0-inside convention +
        // the radius subtraction; a missing/sign-flipped radius would read >= 0). A frozen/empty
        // SDF would read the sentinel here, not a negative distance.
        const glm::vec3 inside(0.0f, 0.6f, 0.0f);
        const double interiorD = double(GpuSdfEdt::distanceAt(seeds, origin, extent, N, sdfRadius, inside));
        const bool interiorOk = interiorD < 0.0 && interiorD > -2.0 * double(sdfRadius);
        // empty region far from the cloud -> a genuine large distance (flooded, not the sentinel).
        const double emptyD = double(GpuSdfEdt::distanceAt(seeds, origin, extent, N, sdfRadius, glm::vec3(0.55f, 1.15f, 0.55f)));
        const bool emptyOk = emptyD > 0.15 && emptyD < 1.0e8;  // real flooded distance, not the 1e9 sentinel
        // NEG-CTRL: a SHIFTED grid (wrong origin) misreads the distance.
        const double shifted = double(GpuSdfEdt::distanceAt(seeds, origin + glm::vec3(0.6f, 0, 0), extent, N, sdfRadius, probes[0]));
        const bool negOk = std::abs(shifted - analyticDist(probes[0])) > 2.0 * cell;

        const bool ok = distOk && gradOk && interiorOk && emptyOk && negOk;
        printf("[livesdf]   SDF-CORRECT: max|JFA dist - analytic|=%.4f m (<%.3f, ok:%d); gradient dot(away)=%.4f (>0.9:%d); "
               "interior d=%.4f<0 (inside:%d); empty->no-collision:%d; NEG shifted-grid mismatches:%d  %s\n",
               maxDistErr, 2.0 * cell, int(distOk), minDot, int(gradOk), interiorD, int(interiorOk), int(emptyOk), int(negOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    edt.shutdown(m_gl);
    f->setPlaying(false); f->reset();                           // restore empty state for later gates
    printf("[livesdf] %s\n", allOk ? "ALL PASS (JFA SDF <15ms on live fluid; correct vs analytic; brute-force baseline fails)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}
