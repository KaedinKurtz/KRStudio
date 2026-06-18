// ===========================================================================
// Avoidance-field sprint, Phase 2 — EMITTER node (krs::field). One node, two
// emitted TYPES selected by a combo (param "type"):
//   0 = AVOIDANCE-FIELD: places/updates a PointEffectorComponent (+FieldSourceTag)
//       on the object, sampled by FieldSolver (rule 6). strength = amplitude*sign
//       (node-driven amplitude so Phase 3 dynamics can drive it); Linear falloff.
//   1 = SUBSTANCE: emits a particle stream FROM the object (water/fire as a
//       MODIFIER) at a node-driven rate; particles originate at + follow the object.
// Switching type cleans up the other type's effect; an invalid type emits nothing.
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "Scene.hpp"
#include "FieldSolver.hpp"
#include "AvoidanceField.hpp"
#include "components.hpp"     // PointEffectorComponent, FieldSourceTag, TransformComponent

#include <cstdio>
#include <cmath>
#include <memory>
#include <algorithm>

namespace krs::field {

namespace {

class EmitterNode : public Node {
public:
    EmitterNode() {
        m_id = "field_emitter";
        m_ports.push_back({ "Object",    { "entt::entity", "handle" }, Port::Direction::Input, this });
        m_ports.push_back({ "Amplitude", { "double", "unitless" },     Port::Direction::Input, this });  // field magnitude (strength)
        m_ports.push_back({ "Radius",    { "double", "m" },            Port::Direction::Input, this });  // field reach (overall size)
        m_ports.push_back({ "Falloff",   { "double", "unitless" },     Port::Direction::Input, this });  // falloff RATE: 0=flat,1=linear,>1=steeper
        m_ports.push_back({ "Rate",      { "double", "1/s" },          Port::Direction::Input, this });  // substance spawn rate
        // sensible literal defaults so the in-node spinboxes show real values (radius 5 m, linear falloff).
        setPortLiteral<float>("Radius", 5.0f);
        setPortLiteral<float>("Falloff", 1.0f);
    }
    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        const auto obj = getInput<entt::entity>("Object");
        entt::entity e = entt::null;
        if (obj && reg.valid(*obj)) e = *obj;
        const int type = getParam<int>("type", 0);

        if (type == 0 && e != entt::null) {
            // AVOIDANCE-FIELD: place/update the field source on the object.
            const double amp = getInputD("Amplitude", 0.0);
            const double sign = (getParam<int>("sign", 1) >= 0) ? 1.0 : -1.0;
            const double strength = amp * sign;
            if (std::abs(strength) < 1e-12) {
                // zero amplitude == NO field: remove the source entirely (not a 0-strength ghost).
                if (reg.all_of<PointEffectorComponent>(e)) reg.remove<PointEffectorComponent>(e);
                if (reg.all_of<FieldSourceTag>(e)) reg.remove<FieldSourceTag>(e);
            } else {
                auto& pe = reg.get_or_emplace<PointEffectorComponent>(e);
                pe.strength = float(strength);
                // Radius input wins; the legacy "radius" param is the fallback (so old graphs/gates still work).
                pe.radius = float(getInputD("Radius", getParam<double>("radius", 5.0)));
                pe.falloff = PointEffectorComponent::FalloffType::Linear;
                pe.falloffExponent = float(std::max(0.0, getInputD("Falloff", getParam<double>("falloff", 1.0))));
                if (!reg.all_of<FieldSourceTag>(e)) reg.emplace<FieldSourceTag>(e);
            }
        } else if (e != entt::null) {
            // not field type (substance / invalid / unset): tear down any field source.
            if (reg.all_of<PointEffectorComponent>(e)) reg.remove<PointEffectorComponent>(e);
            if (reg.all_of<FieldSourceTag>(e)) reg.remove<FieldSourceTag>(e);
        }

        if (type == 1 && e != entt::null) {
            // SUBSTANCE: emit from the object's current position (it follows the object).
            const double rate = getInputD("Rate", 0.0);
            const glm::vec3 origin = reg.get<TransformComponent>(e).translation;
            m_substance.spawn(origin, m_emitVel, rate, m_dt, m_time);
            m_time += m_dt;
        }
    }
    SubstanceEmitter& substance() { return m_substance; }
private:
    SubstanceEmitter m_substance;
    glm::vec3 m_emitVel{ 0.0f, 1.0f, 0.0f };
    double m_time = 0.0, m_dt = 1.0 / 60.0;
};

struct EmitterRegistrar {
    EmitterRegistrar() {
        NodeFactory::instance().registerNodeType("field_emitter",
            { "Emitter", "Twin", "Emits an avoidance FIELD or a SUBSTANCE stream FROM the wired object (by type)." },
            []() { return std::make_unique<EmitterNode>(); });
    }
};
static EmitterRegistrar g_emitterRegistrar;

// gate helpers
void feedObject(Node& n, entt::entity e) {
    PortDataPacket pk; pk.data = e; pk.type = { "entt::entity", "handle" };
    n.setInput("Object", pk);
}
void feedD(Node& n, const std::string& port, double v) {
    PortDataPacket pk; pk.data = v; pk.type = { "double", "unitless" };
    n.setInput(port, pk);
}

} // namespace

bool runEmitterGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[emitter] GATE EMITTER -- avoidance-field emission (magnitude/sign) + substance emission (origin/rate/follow)\n");
    bool allOk = true;
    FieldSolver fs;

    auto mkObj = [](Scene& s, const char* name, glm::vec3 pos) {
        auto& reg = s.getRegistry();
        entt::entity e = reg.create();
        reg.emplace<TagComponent>(e).tag = name;
        reg.emplace<TransformComponent>(e).translation = pos;
        reg.emplace<RigidBodyComponent>(e);
        return e;
    };

    // ---- EMITTER-FIELD: commanded magnitude/sign at the object ----
    {
        Scene scene; auto& reg = scene.getRegistry();
        const entt::entity e = mkObj(scene, "src", glm::vec3(0, 0, 0));
        EmitterNode em; em.setScene(&scene);
        em.setParam<int>("type", 0); em.setParam<int>("sign", 1); em.setParam<double>("radius", 5.0);
        feedObject(em, e); feedD(em, "Amplitude", 10.0);
        em.process();
        // two samples pin the Linear SLOPE (not just one point): |field| = amp*(1-d/R).
        // Q=(2,0,0): 10*(1-2/5)=6 (away/+X). Q2=(4,0,0): 10*(1-4/5)=2.
        const glm::vec3 Q(2, 0, 0), Q2(4, 0, 0);
        const glm::vec3 field = fs.getVectorAt(reg, Q);
        const float mag = glm::length(field), mag2 = glm::length(fs.getVectorAt(reg, Q2));
        const bool magOk = std::abs(mag - 6.0f) < 1e-3f && std::abs(mag2 - 2.0f) < 1e-3f;   // slope + radius pinned
        const bool repOk = mag > 1e-6f && glm::dot(glm::normalize(field), glm::vec3(1, 0, 0)) > 0.999f;  // repulsive = away

        em.setParam<int>("sign", -1); em.process();   // attractive (sign param)
        const glm::vec3 fieldA = fs.getVectorAt(reg, Q);
        const bool attrOk = glm::length(fieldA) > 1e-6f && glm::dot(glm::normalize(fieldA), glm::vec3(-1, 0, 0)) > 0.999f; // toward

        // a NEGATIVE wired amplitude (sign param +1) must also give attractive -> proves signed
        // amplitude works (Phase-3 dynamics may emit a signed magnitude).
        em.setParam<int>("sign", 1); feedD(em, "Amplitude", -10.0); em.process();
        const glm::vec3 fieldN = fs.getVectorAt(reg, Q);
        const bool signedOk = glm::length(fieldN) > 1e-6f && glm::dot(glm::normalize(fieldN), glm::vec3(-1, 0, 0)) > 0.999f;

        // zero amplitude -> the field source is REMOVED (no field, no ghost effector).
        feedD(em, "Amplitude", 0.0); em.process();
        const bool zeroOk = glm::length(fs.getVectorAt(reg, Q)) == 0.0f && !reg.all_of<PointEffectorComponent>(e)
                         && !reg.all_of<FieldSourceTag>(e);

        // NEG-CTRL: a fresh emitter with NO object wired -> no field source created.
        Scene s2; auto& r2 = s2.getRegistry(); mkObj(s2, "x", glm::vec3(0, 0, 0));
        EmitterNode em2; em2.setScene(&s2); em2.setParam<int>("type", 0); feedD(em2, "Amplitude", 10.0); em2.process();
        const bool discOk = glm::length(fs.getVectorAt(r2, Q)) == 0.0f;

        const bool ok = magOk && repOk && attrOk && signedOk && zeroOk && discOk;
        printf("[emitter]   EMITTER-FIELD: |field|@d2=%.4f @d4=%.4f (want 6.0/2.0, ok:%d); repulsive-away:%d; attractive-toward:%d; "
               "signed-amp:%d; zero-amp->source-removed:%d; disconnected->no-emission:%d  %s\n",
               mag, mag2, int(magOk), int(repOk), int(attrOk), int(signedOk), int(zeroOk), int(discOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- RADIUS + FALLOFF-RATE: the new UI-exposed emitter controls ----
    {
        Scene scene; auto& reg = scene.getRegistry();
        const entt::entity e = mkObj(scene, "src", glm::vec3(0, 0, 0));
        EmitterNode em; em.setScene(&scene); em.setParam<int>("type", 0);
        feedObject(em, e); feedD(em, "Amplitude", 10.0);

        // RADIUS shrinks the reach: with R=2 the field is present inside and ~0 at/after the edge.
        feedD(em, "Radius", 2.0); feedD(em, "Falloff", 1.0); em.process();
        const float in1  = glm::length(fs.getVectorAt(reg, glm::vec3(1.0f, 0, 0)));        // 10*(1-1/2)=5
        const float edge = glm::length(fs.getVectorAt(reg, glm::vec3(2.0001f, 0, 0)));     // beyond R -> 0
        const bool radiusOk = std::abs(in1 - 5.0f) < 1e-3f && edge < 1e-4f;

        // FALLOFF RATE at a fixed point inside R=5: 10*(1-2/5)^p -> p=0:10 (flat), p=1:6 (linear), p=2:3.6 (steeper).
        feedD(em, "Radius", 5.0);
        feedD(em, "Falloff", 0.0); em.process(); const float p0 = glm::length(fs.getVectorAt(reg, glm::vec3(2, 0, 0)));
        feedD(em, "Falloff", 1.0); em.process(); const float p1 = glm::length(fs.getVectorAt(reg, glm::vec3(2, 0, 0)));
        feedD(em, "Falloff", 2.0); em.process(); const float p2 = glm::length(fs.getVectorAt(reg, glm::vec3(2, 0, 0)));
        const bool falloffOk = std::abs(p0 - 10.0f) < 1e-3f && std::abs(p1 - 6.0f) < 1e-3f
                            && std::abs(p2 - 3.6f) < 1e-3f && p0 > p1 && p1 > p2;   // higher rate => steeper

        const bool ok = radiusOk && falloffOk;
        printf("[emitter]   RADIUS-FALLOFF: R=2 -> |f|@1=%.3f (want 5), edge~0:%d; falloff-rate p0/p1/p2 @d2=%.3f/%.3f/%.3f "
               "(want 10/6/3.6, monotone-steeper:%d)  %s\n",
               in1, int(radiusOk), p0, p1, p2, int(falloffOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- EMITTER-SUBSTANCE: origin + rate + follows the object ----
    {
        Scene scene;
        const entt::entity e = mkObj(scene, "faucet", glm::vec3(1, 2, 3));
        EmitterNode em; em.setScene(&scene); em.setParam<int>("type", 1);
        feedObject(em, e); feedD(em, "Rate", 100.0);                 // 100 particles/s
        for (int k = 0; k < 30; ++k) em.process();                  // 30 steps @ 1/60 = 0.5 s
        const int n = em.substance().count();
        const bool rateOk = std::abs(n - 50) <= 2;                  // ~rate*T = 100*0.5
        bool originOk = true;
        for (const auto& p : em.substance().particles())
            if (glm::length(p.pos - glm::vec3(1, 2, 3)) > 1e-4f) originOk = false;

        // FOLLOW: move the object -> the NEW spawns originate at the new position.
        scene.getRegistry().get<TransformComponent>(e).translation = glm::vec3(-5, 0, 0);
        const int before = em.substance().count();
        for (int k = 0; k < 30; ++k) em.process();
        int fresh = 0; bool followOk = true;
        for (int i = before; i < em.substance().count(); ++i) {
            ++fresh;
            if (glm::length(em.substance().particles()[i].pos - glm::vec3(-5, 0, 0)) > 1e-4f) followOk = false;
        }
        // NEG-CTRL: rate <= 0 -> no spawns.
        const int beforeZero = em.substance().count();
        feedD(em, "Rate", 0.0); for (int k = 0; k < 10; ++k) em.process();
        const bool zeroRateOk = em.substance().count() == beforeZero;

        const bool ok = rateOk && originOk && fresh > 0 && followOk && zeroRateOk;
        printf("[emitter]   EMITTER-SUBSTANCE: spawned %d in 0.5s (want ~50, ok:%d); origin-at-object:%d; "
               "moves-with-object (%d new at new pos):%d; rate0->no-spawn:%d  %s\n",
               n, int(rateOk), int(originOk), fresh, int(followOk), int(zeroRateOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- EMITTER-TYPE-SWITCH: field vs substance vs invalid ----
    {
        Scene scene; auto& reg = scene.getRegistry();
        const entt::entity e = mkObj(scene, "o", glm::vec3(0, 0, 0));
        EmitterNode em; em.setScene(&scene);
        feedObject(em, e); feedD(em, "Amplitude", 10.0); feedD(em, "Rate", 100.0);

        em.setParam<int>("type", 0); em.process();                  // FIELD
        const bool fieldOn = reg.all_of<PointEffectorComponent>(e) && reg.get<PointEffectorComponent>(e).strength != 0.0f;
        const int subAfterField = em.substance().count();           // field type emits NO substance

        // field -> INVALID directly: the field source must be torn down (NOT pre-satisfied
        // by a prior substance phase) and nothing emitted.
        em.setParam<int>("type", 99); const int subBeforeInvalid = em.substance().count();
        em.process();
        const bool invalidTearsDown = !reg.all_of<PointEffectorComponent>(e) && em.substance().count() == subBeforeInvalid;

        // re-establish FIELD, then -> SUBSTANCE: field torn down + substance emits.
        em.setParam<int>("type", 0); em.process();
        const bool fieldBack = reg.all_of<PointEffectorComponent>(e);
        em.setParam<int>("type", 1); const int subBefore = em.substance().count();
        for (int k = 0; k < 10; ++k) em.process();
        const bool fieldOff = !reg.all_of<PointEffectorComponent>(e);   // switching tears down the field
        const bool substanceOn = em.substance().count() > subBefore;

        const bool ok = fieldOn && subAfterField == 0 && invalidTearsDown && fieldBack && fieldOff && substanceOn;
        printf("[emitter]   EMITTER-TYPE-SWITCH: field->effector(no substance):%d; field->invalid tears down field:%d; "
               "field->substance tears down + emits:%d/%d  %s\n",
               int(fieldOn && subAfterField == 0), int(invalidTearsDown), int(fieldOff), int(substanceOn), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[emitter] %s\n", allOk ? "ALL PASS (field magnitude/sign; substance origin/rate/follow; type switch)"
                                    : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::field
