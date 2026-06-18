// OrbProbeGate.cpp -- Phase 4: the velocity-probe orb. Two gates:
//   ORB-VELOCITY  -- the VOLUME containment velocity query (krs::orb::averageVelocityInSphere)
//                    on a controlled synthetic set (exact ground truth + global-average neg-ctrl)
//                    AND on the REAL live fluid (off-stream -> 0 proves it is a volume query, not a
//                    global average).
//   ORB-LIFECYCLE -- the orb<->node binding on an entt::registry: spawn N -> N orbs with N distinct
//                    colours matching their nodes, delete-node removes the orb, delete-orb exposes the
//                    node to remove (bidirectional), and a non-propagating removal leaves an orphan
//                    (the real failing model the assertion catches).
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "OrbProbe.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
struct GpuParticle { glm::vec4 posLife; glm::vec4 vel; glm::vec4 pred; };   // FluidSystem layout
}

bool RenderingSystem::runOrbVelocityGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[orbvel] GATE ORB-VELOCITY -- volume containment velocity query (synthetic exact + REAL fluid)\n");
    bool allOk = true;

    // ---- Part A: controlled synthetic set -- exact ground truth + global-average neg-ctrl ----
    {
        const glm::vec3 center(0.0f, 0.0f, 0.0f);
        const float radius = 0.5f;
        const glm::vec3 vIn(1.0f, 2.0f, 3.0f);     // every particle INSIDE the sphere moves at vIn
        const glm::vec3 vOut(9.0f, -9.0f, 9.0f);   // every particle OUTSIDE moves at vOut (very different)
        std::vector<glm::vec3> pos, vel;
        int nIn = 0, nOut = 0;
        // inside: a 5x5x5 lattice spanning [-0.2,0.2]^3 -> every point (incl. corners at 0.35 m) is
        // UNAMBIGUOUSLY within r=0.5, so the contained count must be exactly all 125.
        for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) for (int k = 0; k < 5; ++k) {
            glm::vec3 p = (glm::vec3(i, j, k) / 4.0f - 0.5f) * 0.4f;   // spans [-0.2,0.2]^3
            pos.push_back(center + p); vel.push_back(vIn); ++nIn;
        }
        // outside: a shell 3-4 m away (well beyond the sphere).
        for (int i = 0; i < 60; ++i) {
            float a = float(i) * 0.31f;
            pos.push_back(center + glm::vec3(3.0f + 0.5f * std::sin(a * 1.7f), std::sin(a), std::cos(a)));
            vel.push_back(vOut); ++nOut;
        }
        const krs::orb::OrbVelocity onStream = krs::orb::averageVelocityInSphere(pos, vel, center, radius);
        // NEG-CTRL: the GLOBAL average (ignore containment) -- the real "just average everything" model.
        glm::vec3 g(0.0f); for (auto& v : vel) g += v; g /= float(vel.size());

        const double avgErr = glm::length(onStream.avg - vIn);
        const double globalErr = glm::length(g - vIn);
        const bool countOk = onStream.count == nIn;             // exactly the inside particles, none of the outside
        const bool avgOk = avgErr < 1e-4;                       // matches the inside velocity exactly
        const bool globalDiffers = globalErr > 1.0;            // the global average is a DIFFERENT (wrong) answer

        // off-stream: move the sphere far from every particle -> nothing contained.
        const krs::orb::OrbVelocity off = krs::orb::averageVelocityInSphere(pos, vel, glm::vec3(20.0f, 20.0f, 20.0f), radius);
        const bool offOk = off.count == 0 && glm::length(off.avg) < 1e-9;
        // empty input -> zero.
        const krs::orb::OrbVelocity empty = krs::orb::averageVelocityInSphere({}, {}, center, radius);
        const bool emptyOk = empty.count == 0 && glm::length(empty.avg) < 1e-9;

        const bool ok = countOk && avgOk && globalDiffers && offOk && emptyOk;
        printf("[orbvel]   SYNTHETIC: inside %d/%d, avg=(%.3f,%.3f,%.3f) err=%.5f (==vIn:%d); GLOBAL avg err=%.3f (differs:%d); "
               "off-stream count=%d |v|=%.5f (->0:%d); empty ->0:%d  %s\n",
               onStream.count, nIn, onStream.avg.x, onStream.avg.y, onStream.avg.z, avgErr, int(avgOk),
               globalErr, int(globalDiffers), off.count, glm::length(off.avg), int(offOk), int(emptyOk),
               ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- Part B: the REAL live fluid -- volume query on the actual particle SSBO ----
    FluidSystem* f = getFluidSystem();
    if (f && m_gl) {
        entt::registry reg;
        const entt::entity vol = reg.create();
        reg.emplace<TransformComponent>(vol, glm::vec3(0.0f, 0.6f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.35f); fv.particleSpacing = 0.05f;
        f->params().gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        f->setPlaying(false); f->reset(); f->setPlaying(true);
        f->update(*this, m_gl, reg, 1.0f / 120.0f);
        for (int i = 0; i < 8; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);   // get the particles moving

        const int count = f->particleCount();
        std::vector<GpuParticle> parts; parts.resize(size_t(count));
        m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
        m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle)) * count, parts.data());
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        std::vector<glm::vec3> pos, vel; glm::vec3 c(0.0f);
        for (const auto& p : parts) if (p.posLife.w > 0.0f) { pos.push_back(glm::vec3(p.posLife)); vel.push_back(glm::vec3(p.vel)); c += glm::vec3(p.posLife); }
        const int live = int(pos.size());
        if (live > 0) c /= float(live);

        const float orbR = 0.2f;
        const krs::orb::OrbVelocity onStream = krs::orb::averageVelocityInSphere(pos, vel, c, orbR);
        // independent manual contained-average (a separate loop) -- consistency cross-check.
        glm::vec3 mSum(0.0f); int mN = 0;
        for (int i = 0; i < live; ++i) { glm::vec3 d = pos[i] - c; if (glm::dot(d, d) <= orbR * orbR) { mSum += vel[i]; ++mN; } }
        const glm::vec3 manual = mN ? mSum / float(mN) : glm::vec3(0.0f);
        const bool consistent = onStream.count == mN && glm::length(onStream.avg - manual) < 1e-5f;
        // off-stream on REAL data: an orb far from the cloud contains nothing.
        const krs::orb::OrbVelocity off = krs::orb::averageVelocityInSphere(pos, vel, c + glm::vec3(3.0f, 0.0f, 0.0f), orbR);
        const bool onOk = onStream.count > 0;
        const bool offOk = off.count == 0 && glm::length(off.avg) < 1e-9;

        const bool ok = onOk && offOk && consistent;
        printf("[orbvel]   REAL FLUID: %d live particles; orb@centroid contains %d, avg=(%.3f,%.3f,%.3f) (count>0:%d, consistent:%d); "
               "orb off-stream contains %d (->0:%d)  %s\n",
               live, onStream.count, onStream.avg.x, onStream.avg.y, onStream.avg.z, int(onOk), int(consistent),
               off.count, int(offOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
        f->setPlaying(false); f->reset();
    } else {
        printf("[orbvel]   REAL FLUID: skipped (no fluid system / GL)\n");
    }

    printf("[orbvel] %s\n", allOk ? "ALL PASS (volume velocity query: synthetic exact, global-avg neg-ctrl, real-fluid off-stream->0)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

bool RenderingSystem::runOrbLifecycleGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[orblife] GATE ORB-LIFECYCLE -- orb<->node binding (N orbs N colours; bidirectional delete)\n");
    bool allOk = true;
    entt::registry reg;

    // ---- N nodes -> N orbs with N distinct colours matching their nodes ----
    struct Spec { std::uint64_t node; glm::vec3 color; glm::vec3 center; };
    const Spec specs[] = {
        { 10, {1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f} },
        { 20, {0.0f, 1.0f, 0.0f}, {1.0f, 0.5f, 0.0f} },
        { 30, {0.0f, 0.0f, 1.0f}, {2.0f, 0.5f, 0.0f} },
        { 40, {1.0f, 1.0f, 0.0f}, {3.0f, 0.5f, 0.0f} },
        { 50, {1.0f, 0.0f, 1.0f}, {4.0f, 0.5f, 0.0f} },
    };
    const int N = int(sizeof(specs) / sizeof(specs[0]));
    for (const auto& s : specs) {
        entt::entity e = reg.create();
        krs::orb::decorateProbeOrb(reg, e, s.node, s.color, s.center, 0.25f);
    }
    // exactly N orbs, each findable by node id, colour matches, distinct colours, NO collider, IS glass.
    bool spawnOk = krs::orb::orbCount(reg) == N;
    bool colorOk = true, distinctOk = true, noColliderOk = true, glassOk = true;
    for (int i = 0; i < N; ++i) {
        entt::entity e = krs::orb::findOrbForNode(reg, specs[i].node);
        if (e == entt::null) { spawnOk = false; continue; }
        const auto& ob = reg.get<OrbBindingComponent>(e);
        if (glm::length(ob.color - specs[i].color) > 1e-6f) colorOk = false;     // orb colour == node colour
        if (reg.all_of<AutoCollisionComponent>(e)) noColliderOk = false;          // probe volume, not rigid
        if (!reg.all_of<GlassComponent>(e)) glassOk = false;
        for (int j = i + 1; j < N; ++j) if (glm::length(specs[i].color - specs[j].color) < 1e-6f) distinctOk = false;
    }
    const bool createOk = spawnOk && colorOk && distinctOk && noColliderOk && glassOk;
    printf("[orblife]   SPAWN: %d nodes -> %d orbs (match:%d); colours match nodes:%d; distinct:%d; no collider:%d; glass:%d  %s\n",
           N, krs::orb::orbCount(reg), int(spawnOk), int(colorOk), int(distinctOk), int(noColliderOk), int(glassOk),
           createOk ? "PASS" : "FAIL");
    allOk = allOk && createOk;

    // ---- delete the NODE -> its orb is removed (node-side deletion propagates to the scene) ----
    const bool removed20 = krs::orb::removeOrbForNode(reg, 20);
    const bool node20Gone = krs::orb::findOrbForNode(reg, 20) == entt::null;
    const bool othersStay = krs::orb::findOrbForNode(reg, 10) != entt::null && krs::orb::findOrbForNode(reg, 30) != entt::null;
    const bool delNodeOk = removed20 && node20Gone && othersStay && krs::orb::orbCount(reg) == N - 1;
    printf("[orblife]   DELETE-NODE 20: removed:%d, orb gone:%d, others remain:%d, count=%d  %s\n",
           int(removed20), int(node20Gone), int(othersStay), krs::orb::orbCount(reg), delNodeOk ? "PASS" : "FAIL");
    allOk = allOk && delNodeOk;

    // ---- delete the ORB -> the bound node id is recovered so the node can be removed (reverse direction) ----
    entt::entity orb30 = krs::orb::findOrbForNode(reg, 30);
    const std::uint64_t boundNode = (orb30 != entt::null) ? reg.get<OrbBindingComponent>(orb30).nodeId : 0;
    if (orb30 != entt::null) reg.destroy(orb30);              // the operator deletes the orb in the scene
    const bool reverseOk = boundNode == 30                    // the reverse hook learns WHICH node to delete
                        && krs::orb::findOrbForNode(reg, 30) == entt::null
                        && krs::orb::orbCount(reg) == N - 2;
    printf("[orblife]   DELETE-ORB (node 30): recovered bound node=%llu (==30:%d), orb gone, count=%d  %s\n",
           (unsigned long long)boundNode, int(boundNode == 30), krs::orb::orbCount(reg), reverseOk ? "PASS" : "FAIL");
    allOk = allOk && reverseOk;

    // ---- NEG-CTRL: a NON-PROPAGATING removal leaves an orphan (the real failing model) ----
    // model a broken delete that does nothing; the orb for node 40 must persist (the bug the gate catches),
    // then the correct removal actually clears it.
    auto brokenRemove = [](entt::registry&, std::uint64_t) -> bool { return false; };  // never propagates
    const int before = krs::orb::orbCount(reg);
    brokenRemove(reg, 40);
    const bool orphanPersists = krs::orb::findOrbForNode(reg, 40) != entt::null && krs::orb::orbCount(reg) == before;
    const bool correctRemoves = krs::orb::removeOrbForNode(reg, 40) && krs::orb::findOrbForNode(reg, 40) == entt::null;
    const bool negOk = orphanPersists && correctRemoves;     // broken leaks (FAIL model), correct cleans up
    printf("[orblife]   NEG-CTRL: non-propagating delete leaves orphan:%d; correct delete clears it:%d  %s\n",
           int(orphanPersists), int(correctRemoves), negOk ? "PASS" : "FAIL");
    allOk = allOk && negOk;

    printf("[orblife] %s\n", allOk ? "ALL PASS (N orbs N colours; node-delete removes orb; orb-delete exposes node; leak neg-ctrl caught)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}
