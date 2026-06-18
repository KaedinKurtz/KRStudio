// OrbProbeSystem.cpp -- Phase 4 runtime: the per-frame, GL-side step that fills every
// velocity-probe orb's measured velocity. The particle SSBO is only readable where the GL
// context is current (the render loop), so this runs there: read back the live fluid
// particles once, then for each OrbBindingComponent compute the average velocity of the
// particles inside its sphere (krs::orb::averageVelocityInSphere) and store it on the
// component. The orb node's compute() (on the eval thread) merely relays that value.
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "OrbProbe.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>

namespace {
struct GpuParticle { glm::vec4 posLife; glm::vec4 vel; glm::vec4 pred; };   // FluidSystem layout
}

void RenderingSystem::updateOrbProbes(entt::registry& registry)
{
    auto view = registry.view<OrbBindingComponent, TransformComponent>();
    if (view.begin() == view.end()) return;     // no orbs -> skip the readback entirely
    if (!m_gl) return;

    // gather the live fluid particles (position + velocity) once.
    std::vector<glm::vec3> pos, vel;
    if (FluidSystem* f = getFluidSystem()) {
        const int n = f->particleCount();
        if (n > 0) {
            std::vector<GpuParticle> parts; parts.resize(size_t(n));
            m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
            m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle)) * n, parts.data());
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            pos.reserve(size_t(n)); vel.reserve(size_t(n));
            for (const auto& p : parts) if (p.posLife.w > 0.0f) { pos.push_back(glm::vec3(p.posLife)); vel.push_back(glm::vec3(p.vel)); }
        }
    }

    // per orb: average velocity of the particles inside its sphere (radius = transform scale).
    for (auto e : view) {
        auto& ob = view.get<OrbBindingComponent>(e);
        const auto& xf = view.get<TransformComponent>(e);
        const float radius = std::max(xf.scale.x, 1e-4f);
        ob.radius = radius;
        const krs::orb::OrbVelocity r = krs::orb::averageVelocityInSphere(pos, vel, xf.translation, radius);
        ob.measuredVelocity = r.avg;
        ob.containedCount = r.count;

        // one-shot confirm that the runtime probe really samples the live fluid volume.
        static bool s_logged = false;
        if (!s_logged && r.count > 0) {
            qInfo() << "[orb] probe (node" << ob.nodeId << ") sampling" << r.count
                    << "fluid particles, |v| =" << glm::length(r.avg) << "m/s";
            s_logged = true;
        }
    }
}
