// Phase 5 — FEM system: async oracle driver + nodal->render-vertex interpolation
// + GPU upload for the unified visualization. Engine GL thread.
#include "FemSystem.hpp"
#include "FemComponents.hpp"
#include "SurrogateField.hpp"
#include "components.hpp"
#include "MpmSystem.hpp"
#include <cstdlib>

#include <QOpenGLFunctions_4_3_Core>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace krs::fem {
namespace {

glm::mat4 modelMatrix(const TransformComponent& xf) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xf.translation);
    m *= glm::mat4_cast(xf.rotation);
    m = glm::scale(m, xf.scale);
    return m;
}

// Sample a per-NODE field at a world point: trilinear inside a fully-solid cell,
// else nearest active node.
float sampleField(const VoxelFemModel& m, const std::vector<double>& field, const glm::dvec3& p) {
    if (field.empty()) return 0.0f;
    const glm::dvec3 rel = (p - m.origin) / m.h;
    const int i = std::clamp(int(std::floor(rel.x)), 0, m.nx - 1);
    const int j = std::clamp(int(std::floor(rel.y)), 0, m.ny - 1);
    const int k = std::clamp(int(std::floor(rel.z)), 0, m.nz - 1);
    int n[8]; bool allSolid = true;
    for (int a = 0; a < 8; ++a) {
        const int di = a & 1, dj = (a >> 1) & 1, dk = (a >> 2) & 1;
        n[a] = m.nodeId[m.gridIdx(i + di, j + dj, k + dk)];
        if (n[a] < 0) allSolid = false;
    }
    if (allSolid) {
        const double fx = std::clamp(rel.x - i, 0.0, 1.0), fy = std::clamp(rel.y - j, 0.0, 1.0), fz = std::clamp(rel.z - k, 0.0, 1.0);
        double v = 0.0;
        for (int a = 0; a < 8; ++a) {
            const int di = a & 1, dj = (a >> 1) & 1, dk = (a >> 2) & 1;
            const double w = (di ? fx : 1 - fx) * (dj ? fy : 1 - fy) * (dk ? fz : 1 - fz);
            v += w * field[n[a]];
        }
        return float(v);
    }
    const int nn = m.nearestNode(p);
    return nn >= 0 ? float(field[nn]) : 0.0f;
}

template <class T> std::vector<double> toD(const std::vector<T>& v) { return std::vector<double>(v.begin(), v.end()); }

} // namespace

void FemSystem::update(entt::registry& reg, QOpenGLFunctions_4_3_Core* gl, MpmSystem* mpm, int vizMode) {
    if (!gl) return;
    // Prune solves whose entity was destroyed / lost its FemBodyComponent, freeing
    // their GL buffers (the component is already gone, so this map owns the handles).
    for (auto it = m_solves.begin(); it != m_solves.end(); ) {
        const entt::entity e = it->first;
        if (!reg.valid(e) || !reg.all_of<FemBodyComponent>(e)) {
            Solve& s = it->second;
            if (s.vao) gl->glDeleteVertexArrays(1, &s.vao);
            unsigned int bufs[3] = { s.vboPos, s.vboScalar, s.ebo };
            gl->glDeleteBuffers(3, bufs);
            it = m_solves.erase(it);
        } else ++it;
    }
    auto view = reg.view<FemBodyComponent, RenderableMeshComponent, TransformComponent, MaterialComponent>();
    for (auto e : view) {
        auto& fb = view.get<FemBodyComponent>(e);
        const auto& rm = view.get<RenderableMeshComponent>(e);
        const auto& xf = view.get<TransformComponent>(e);
        const auto& mc = view.get<MaterialComponent>(e);
        if (rm.vertices.empty() || rm.indices.empty()) continue;
        Solve& s = m_solves[e];

        // --- (re)launch on dirty ---
        if (fb.dirty && !s.running) {
            const glm::mat4 M = modelMatrix(xf);
            s.localVerts.clear(); s.worldVerts.clear(); s.indices = rm.indices;
            s.localVerts.reserve(rm.vertices.size()); s.worldVerts.reserve(rm.vertices.size());
            glm::dvec3 lo(1e300), hi(-1e300);
            for (const auto& v : rm.vertices) {
                s.localVerts.push_back(v.position);
                const glm::vec4 w = M * glm::vec4(v.position, 1.0f);
                const glm::dvec3 wp(w.x, w.y, w.z);
                s.worldVerts.push_back(wp);
                lo = glm::min(lo, wp); hi = glm::max(hi, wp);
            }
            const double longest = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-4 });
            const double h = longest / std::max(2, fb.resolution);
            s.model = FemSolver::voxelizeMesh(
                [&] { std::vector<glm::vec3> w; w.reserve(s.worldVerts.size()); for (auto& p : s.worldVerts) w.emplace_back(p); return w; }(),
                rm.indices, h);
            if (!s.model.valid()) { fb.dirty = false; continue; }

            FemMaterial mat{ mc.youngsModulus, mc.poissonRatio, mc.density, mc.thermalConductivity, mc.specificHeat };
            // Elastic BCs.
            ElasticBC ebc;
            const double ylo = s.model.origin.y, yhi = s.model.origin.y + s.model.ny * s.model.h;
            std::vector<int> topNodes;
            for (int n = 0; n < s.model.numNodes; ++n) {
                if (fb.fixBottom && s.model.nodePos[n].y < ylo + 0.5 * s.model.h) ebc.fixedNodes.push_back(n);
                if (s.model.nodePos[n].y > yhi - 0.5 * s.model.h) topNodes.push_back(n);
            }
            if (fb.useGravity) ebc.gravity = glm::dvec3(0.0, -9.81, 0.0);
            if (fb.appliedForceN != 0.0f && !topNodes.empty())
                for (int n : topNodes) ebc.nodalForces.push_back({ n, glm::dvec3(0.0, -double(fb.appliedForceN) / topNodes.size(), 0.0) });
            // Retain material + BCs + heat-source cells for the training-data export.
            s.material = mat; s.ebc = ebc; s.sourceCells.clear();
            for (auto he : reg.view<HeatSourceComponent, TransformComponent>()) {
                const auto& hs2 = reg.get<HeatSourceComponent>(he);
                if (!hs2.active || hs2.power <= 0.0f) continue;
                const glm::dvec3 hp2(reg.get<TransformComponent>(he).translation);
                for (int kk = 0; kk < s.model.nz; ++kk) for (int jj = 0; jj < s.model.ny; ++jj) for (int ii = 0; ii < s.model.nx; ++ii) {
                    const glm::dvec3 c = s.model.origin + (glm::dvec3(ii, jj, kk) + 0.5) * s.model.h;
                    if (glm::length(c - hp2) <= hs2.radius) s.sourceCells.push_back((kk * s.model.ny + jj) * s.model.nx + ii);
                }
            }
            s.elastic = FemSolver::solveElasticAsync(s.model, mat, ebc);

            // Thermal BCs: heat sources (W) lumped to nodes in radius + surface
            // convection to ambient (so the steady solve has a sink).
            s.hasThermal = fb.solveThermal;
            if (s.hasThermal) {
                ThermalBC tbc; tbc.ambientT = 20.0; tbc.convection = 0.5; // lumped W/K per surface node
                glm::dvec3 nlo(1e300), nhi(-1e300);
                for (int n = 0; n < s.model.numNodes; ++n) { nlo = glm::min(nlo, s.model.nodePos[n]); nhi = glm::max(nhi, s.model.nodePos[n]); }
                for (int n = 0; n < s.model.numNodes; ++n) {
                    const glm::dvec3& q = s.model.nodePos[n];
                    if (q.x < nlo.x + 0.5 * s.model.h || q.x > nhi.x - 0.5 * s.model.h ||
                        q.y < nlo.y + 0.5 * s.model.h || q.y > nhi.y - 0.5 * s.model.h ||
                        q.z < nlo.z + 0.5 * s.model.h || q.z > nhi.z - 0.5 * s.model.h)
                        tbc.surfaceNodes.push_back(n);
                }
                for (auto he : reg.view<HeatSourceComponent, TransformComponent>()) {
                    const auto& hs = reg.get<HeatSourceComponent>(he);
                    if (!hs.active || hs.power <= 0.0f) continue;
                    const glm::dvec3 hp(reg.get<TransformComponent>(he).translation);
                    std::vector<int> in;
                    for (int n = 0; n < s.model.numNodes; ++n) if (glm::length(s.model.nodePos[n] - hp) <= hs.radius) in.push_back(n);
                    for (int n : in) tbc.nodalSource.push_back({ n, double(hs.power) / in.size() });
                }
                s.thermal = FemSolver::solveThermalSteadyAsync(s.model, mat, tbc);
            }
            s.running = true; fb.dirty = false;
        }

        // --- poll + publish ---
        if (s.running) {
            const bool eReady = s.elastic.valid() && s.elastic.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            const bool tReady = !s.hasThermal || (s.thermal.valid() && s.thermal.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
            if (eReady && tReady) {
                ElasticResult er = s.elastic.get();
                ThermalResult tr = s.hasThermal ? s.thermal.get() : ThermalResult{};
                auto& res = reg.get_or_emplace<FemResultComponent>(e);
                const size_t nv = s.localVerts.size();
                for (int mode = 1; mode <= 3; ++mode) res.vertScalar[mode].assign(nv, 0.0f);
                const std::vector<double> vm = er.ok ? er.vonMises : std::vector<double>{};
                const std::vector<double> sn = er.ok ? er.strainNorm : std::vector<double>{};
                const std::vector<double> tp = tr.ok ? tr.temperature : std::vector<double>{};
                // F1: only modes the solver actually produced are real. Absent
                // fields are left at 0 and marked unavailable (FemVizPass skips
                // them) — NO fabricated 20 °C / zero placeholder is shown as data.
                res.hasMode[1] = !tp.empty();
                res.hasMode[2] = !vm.empty();
                res.hasMode[3] = !sn.empty();
                glm::vec2 rng[4]; for (int i = 0; i < 4; ++i) rng[i] = glm::vec2(1e30f, -1e30f);
                for (size_t v = 0; v < nv; ++v) {
                    const glm::dvec3& p = s.worldVerts[v];
                    const float t = tp.empty() ? 0.0f : sampleField(s.model, tp, p);
                    const float m2 = vm.empty() ? 0.0f : sampleField(s.model, vm, p);
                    const float st = sn.empty() ? 0.0f : sampleField(s.model, sn, p);
                    res.vertScalar[1][v] = t; res.vertScalar[2][v] = m2; res.vertScalar[3][v] = st;
                    rng[1] = glm::vec2(std::min(rng[1].x, t), std::max(rng[1].y, t));
                    rng[2] = glm::vec2(std::min(rng[2].x, m2), std::max(rng[2].y, m2));
                    rng[3] = glm::vec2(std::min(rng[3].x, st), std::max(rng[3].y, st));
                }
                for (int i = 1; i <= 3; ++i) { if (rng[i].y <= rng[i].x) rng[i].y = rng[i].x + 1e-4f; res.range[i] = rng[i]; }
                res.indexCount = int(s.indices.size());
                res.ready = true; res.buffersBuilt = false; res.uploadedMode = -1;
                // FEM is the data ORACLE: export (geometry+material+BC -> field) pairs
                // for surrogate training when KRS_FEM_EXPORT names an output directory.
                if (const char* dir = std::getenv("KRS_FEM_EXPORT"))
                    exportTrainingSample(dir, m_exportIndex++, s.model, s.material, s.ebc, s.sourceCells, er, tr);
                s.running = false;
            }
        }

        // --- GPU buffers + active-mode scalar upload + shared range union ---
        auto* resp = reg.try_get<FemResultComponent>(e);
        if (resp && resp->ready) {
            FemResultComponent& res = *resp;
            if (!res.buffersBuilt) {
                if (res.vao == 0) gl->glGenVertexArrays(1, &res.vao);
                if (res.vboPos == 0) gl->glGenBuffers(1, &res.vboPos);
                if (res.vboScalar == 0) gl->glGenBuffers(1, &res.vboScalar);
                if (res.ebo == 0) gl->glGenBuffers(1, &res.ebo);
                gl->glBindVertexArray(res.vao);
                gl->glBindBuffer(GL_ARRAY_BUFFER, res.vboPos);
                gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(glm::vec3) * s.localVerts.size()), s.localVerts.data(), GL_STATIC_DRAW);
                gl->glEnableVertexAttribArray(0);
                gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
                gl->glBindBuffer(GL_ARRAY_BUFFER, res.vboScalar);
                gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(float) * s.localVerts.size()), nullptr, GL_DYNAMIC_DRAW);
                gl->glEnableVertexAttribArray(1);
                gl->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
                gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.ebo);
                gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(sizeof(unsigned int) * s.indices.size()), s.indices.data(), GL_STATIC_DRAW);
                gl->glBindVertexArray(0);
                res.buffersBuilt = true;
                s.vao = res.vao; s.vboPos = res.vboPos; s.vboScalar = res.vboScalar; s.ebo = res.ebo;
            }
            const int mode = std::clamp(vizMode, 0, 3);
            if (mode >= 1 && res.uploadedMode != mode && !res.vertScalar[mode].empty()) {
                gl->glBindBuffer(GL_ARRAY_BUFFER, res.vboScalar);
                gl->glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(sizeof(float) * res.vertScalar[mode].size()), res.vertScalar[mode].data());
                gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
                res.uploadedMode = mode;
            }
            if (mpm && mode >= 1) mpm->unionVizRange(MpmSystem::VizMode(mode), res.range[mode]);
        }
    }
}

void FemSystem::shutdown(QOpenGLFunctions_4_3_Core* gl) {
    if (gl) for (auto& kv : m_solves) {
        Solve& s = kv.second;
        if (s.vao) gl->glDeleteVertexArrays(1, &s.vao);
        unsigned int bufs[3] = { s.vboPos, s.vboScalar, s.ebo };
        gl->glDeleteBuffers(3, bufs);
    }
    m_solves.clear();
}

} // namespace krs::fem
