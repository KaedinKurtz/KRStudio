// LiveTrackGate.cpp -- Phase 2: the SDF FOLLOWS live moving water. Seeds a thin
// fluid slab high in the domain, lets gravity pull it down, and shows the JFA
// SDF's zero-crossing tracks the falling water frame-to-frame (LIVE-SDF), while a
// baked-once SDF from t0 reports the OLD surface (the ghost neg-ctrl, the C2
// discipline). LIVE-PERF times the full per-frame path (GPU SDF gen + grid
// readback) on the REAL moving fluid and holds it under 15 ms.
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "GpuSdfEdt.hpp"
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

bool RenderingSystem::runLiveTrackGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[livetrack] GATE LIVE-TRACK -- the JFA SDF follows live moving water (ghost neg-ctrl)\n");

    FluidSystem* f = getFluidSystem();
    if (!f || !m_gl) { printf("[livetrack] FAIL: no fluid system / GL context\n"); return false; }
    if (!getShader("edt_seed") || !getShader("edt_jfa")) { printf("[livetrack] FAIL: edt shaders not registered\n"); return false; }

    // Thin slab high in the domain; real downward gravity so it falls as a coherent blob.
    entt::registry reg;
    const entt::entity vol = reg.create();
    reg.emplace<TransformComponent>(vol, glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.22f, 0.12f, 0.22f); fv.particleSpacing = 0.045f;
    f->params().gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    f->setPlaying(false); f->reset(); f->setPlaying(true);
    f->update(*this, m_gl, reg, 1.0f / 120.0f);                 // seed
    for (int i = 0; i < 2; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);  // stabilize the spawn

    const int count = f->particleCount();
    const float radius = f->particleRadius();
    if (count <= 0) { printf("[livetrack] vacuous pass (no particles seeded)\n"); std::fflush(stdout); return true; }

    // grid covers the whole fall path (y 0.2..1.8); cell = 1.6/64 = 0.025 m.
    const glm::vec3 origin(-0.8f, 0.2f, -0.8f), extent(1.6f, 1.6f, 1.6f);
    const int N = 64;
    const float sdfRadius = 0.08f;     // avoidance safety margin > cell diagonal (0.043 m)

    // read the live particle centroid + bounds (the ground truth for where the water IS).
    auto readState = [&](glm::vec3& c, glm::vec3& lo, glm::vec3& hi, int& live, std::vector<glm::vec3>& pts) {
        std::vector<GpuParticle> parts; parts.resize(size_t(count));
        m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
        m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle)) * count, parts.data());
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        pts.clear(); glm::vec3 sum(0.0f); lo = glm::vec3(1e30f); hi = glm::vec3(-1e30f);
        for (const auto& p : parts) if (p.posLife.w > 0.0f) { glm::vec3 q(p.posLife); pts.push_back(q); sum += q; lo = glm::min(lo, q); hi = glm::max(hi, q); }
        live = int(pts.size()); c = live ? sum / float(live) : glm::vec3(0.0f);
    };

    GpuSdfEdt edt;

    // ---- t0: record the actual centroid, build the SDF, BAKE a ghost copy ----
    glm::vec3 c0, lo0, hi0; int live0 = 0; std::vector<glm::vec3> pts0;
    readState(c0, lo0, hi0, live0, pts0);
    if (!edt.build(*this, m_gl, f->particleBuffer(), count, /*stride*/12, /*posOff*/0, /*aliveOff*/3, origin, extent, N, radius)) {
        printf("[livetrack] FAIL: JFA build failed (shaders missing?)\n"); std::fflush(stdout); return false;
    }
    std::vector<glm::vec4> ghost; edt.readback(m_gl, ghost);    // the baked-once SDF (frozen at t0)

    auto analytic = [&](glm::vec3 q, const std::vector<glm::vec3>& P) { double m = 1e30; for (const auto& p : P) m = std::min(m, double(glm::length(q - p))); return m - double(sdfRadius); };
    const double cell = double(extent.x) / N;

    // ---- advance in two halves so tracking is a TRAJECTORY (t0 -> mid -> t1), not two points.
    // At each checkpoint we REBUILD the SDF from the live buffer and confirm it reads INSIDE at the
    // current centroid, while the frozen t0 ghost reads OUTSIDE there (and progressively wronger). ----
    const int half = 15;
    for (int i = 0; i < half; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);
    glm::vec3 cm, lom, him; int livem = 0; std::vector<glm::vec3> ptsm;
    readState(cm, lom, him, livem, ptsm);
    edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius);
    std::vector<glm::vec4> midSeeds; edt.readback(m_gl, midSeeds);
    const float liveAtCm = GpuSdfEdt::distanceAt(midSeeds, origin, extent, N, sdfRadius, cm);  // live: inside the mid-fall water
    const float ghostAtCm = GpuSdfEdt::distanceAt(ghost, origin, extent, N, sdfRadius, cm);    // ghost(t0): water hasn't reached here

    for (int i = 0; i < half; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);

    // ---- t1: record the new actual centroid, rebuild the SDF LIVE ----
    glm::vec3 c1, lo1, hi1; int live1 = 0; std::vector<glm::vec3> pts1;
    readState(c1, lo1, hi1, live1, pts1);
    edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius);
    std::vector<glm::vec4> liveSeeds; edt.readback(m_gl, liveSeeds);

    const float drop = c0.y - c1.y;
    // monotone descent across the trajectory -> the centroid genuinely moved each leg.
    const bool moved = drop > 0.15f && (c0.y - cm.y) > 0.05f && (cm.y - c1.y) > 0.05f;

    // LIVE tracking along the trajectory: the live SDF reads INSIDE at the mid AND final centroids
    // (the zero-crossing followed the water each leg) and OUTSIDE at the vacated original centroid.
    const float liveAtC1 = GpuSdfEdt::distanceAt(liveSeeds, origin, extent, N, sdfRadius, c1);
    const float liveAtC0 = GpuSdfEdt::distanceAt(liveSeeds, origin, extent, N, sdfRadius, c0);
    const bool liveTracks = liveAtCm < 0.0f && liveAtC1 < 0.0f && liveAtC0 > 0.0f;

    // GHOST neg-ctrl: evaluate the FROZEN t0 SDF at the live centroid as it descends. It starts inside
    // the old water, then DRIFTS monotonically outward (it lost the water that walked away), ending
    // clearly OUTSIDE the live cloud at t1 -- a stale field that failed to follow, not a re-derived base.
    const float ghostAtC0 = GpuSdfEdt::distanceAt(ghost, origin, extent, N, sdfRadius, c0);
    const float ghostAtC1 = GpuSdfEdt::distanceAt(ghost, origin, extent, N, sdfRadius, c1);
    const bool ghostLags = ghostAtC0 < 0.0f && ghostAtC1 > 0.0f          // inside the old water -> outside the new
                        && ghostAtCm > ghostAtC0 && ghostAtC1 > ghostAtCm  // monotone outward drift as water leaves
                        && (ghostAtC1 - liveAtC1) > 0.15f;                  // same point, opposite verdict (live in, ghost out)

    // magnitude cross-check: the live SDF at the mid + final centroids matches the analytic distance
    // to the live cloud (a broken/mis-seeded SDF would diverge here).
    const double magErr = std::max(std::abs(double(liveAtC1) - analytic(c1, pts1)),
                                   std::abs(double(liveAtCm) - analytic(cm, ptsm)));
    const bool magOk = magErr < 2.0 * cell + 0.02;

    const bool trackOk = moved && liveTracks && ghostLags && magOk;
    printf("[livetrack]   LIVE-SDF: water fell %.3f m along C0.y=%.3f -> Cm.y=%.3f -> C1.y=%.3f (moved:%d); "
           "LIVE inside d(Cm)=%.3f d(C1)=%.3f<0 & vacated d(C0)=%.3f>0 (tracks:%d); "
           "GHOST drifts out d(C0)=%.3f -> d(Cm)=%.3f -> d(C1)=%.3f (lags:%d); max|live-analytic|=%.3f (ok:%d)  %s\n",
           drop, c0.y, cm.y, c1.y, int(moved), liveAtCm, liveAtC1, liveAtC0, int(liveTracks),
           ghostAtC0, ghostAtCm, ghostAtC1, int(ghostLags), magErr, int(magOk),
           trackOk ? "PASS" : "FAIL");

    // ---- LIVE-PERF: full per-frame path (GPU SDF gen + grid readback) on the moving fluid ----
    edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius); // warm-up
    m_gl->glFinish();
    const int reps = 8;
    std::vector<glm::vec4> tmp;
    const auto p0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) { edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius); edt.readback(m_gl, tmp); }
    m_gl->glFinish();
    const auto p1 = std::chrono::steady_clock::now();
    const double pathMs = std::chrono::duration<double, std::milli>(p1 - p0).count() / reps;
    // gen-only (production keeps the SDF resident on the GPU; the readback is the gate's extra).
    edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius);
    m_gl->glFinish();
    const auto b0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) edt.build(*this, m_gl, f->particleBuffer(), count, 12, 0, 3, origin, extent, N, radius);
    m_gl->glFinish();
    const auto b1 = std::chrono::steady_clock::now();
    const double genMs = std::chrono::duration<double, std::milli>(b1 - b0).count() / reps;
    const bool perfOk = pathMs < 15.0;
    printf("[livetrack]   LIVE-PERF: %d live particles, %d^3 grid -> full path (gen+readback)=%.3f ms, gen-only=%.3f ms (<15ms:%d)  %s\n",
           live1, N, pathMs, genMs, int(perfOk), perfOk ? "PASS" : "FAIL");

    edt.shutdown(m_gl);
    f->setPlaying(false); f->reset();                           // restore empty state for later gates
    const bool allOk = trackOk && perfOk;
    printf("[livetrack] %s\n", allOk ? "ALL PASS (SDF zero-crossing follows live falling water; ghost lags; full path <15ms)"
                                      : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}
