#pragma once
// ===========================================================================
// Avoidance-field sprint, Phase 1 — introspectable published-state CATALOG
// (krs::twin). The canonical layer the (future) ECS->MQTT publisher AND the
// Object/Property nodes both read (rule 6). It records, per scene object, the
// list of published properties + their live value + a stale-aware publish
// frequency. Pure CPU, no broker -- so the bulk of the publishing feature is
// gateable in-process (the recon's recommendation; only a real broker is slow).
// ===========================================================================
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "components.hpp"   // RigidBodyComponent, TransformComponent, TagComponent

namespace krs::twin {

enum class PropType { Scalar = 1, Vec3 = 3, Quat = 4 };

struct PropertyEntry {
    std::string name;
    PropType type = PropType::Scalar;
    double v[4] = { 0, 0, 0, 0 };          // value (type-many components used)

    // publish-frequency tracking: a small ring of recent publish timestamps.
    static constexpr int kRing = 8;
    double stamps[kRing] = { 0 };
    int stampCount = 0, stampHead = 0;
    double lastStamp = -1e18;

    void record(double t) {
        stamps[stampHead] = t; stampHead = (stampHead + 1) % kRing;
        if (stampCount < kRing) ++stampCount;
        lastStamp = t;
    }
    double firstStamp() const {
        if (stampCount == 0) return lastStamp;
        return stamps[(stampHead - stampCount + kRing) % kRing];
    }
    // raw measured rate from the stamp ring (this is the value that FREEZES on a
    // stall -- the negative control).
    double measuredHz() const {
        if (stampCount < 2) return 0.0;
        const double span = lastStamp - firstStamp();
        return span > 1e-9 ? double(stampCount - 1) / span : 0.0;
    }
    // STALE-AWARE reported rate: the measured rate capped by the instantaneous
    // rate since the last publish, so once the stream stalls (now >> lastStamp)
    // the reported Hz falls toward 0 instead of freezing at the nominal rate.
    // `since` is clamped to >=0 so a non-monotonic now (clock skew, now<lastStamp)
    // can NEVER resurrect the frozen nominal rate -- it reports "just published"
    // (fresh) at worst, never a stale-but-high value.
    double reportedHz(double now) const {
        const double m = measuredHz();
        const double since = (now > lastStamp) ? (now - lastStamp) : 0.0;
        const double inst = since > 1e-9 ? 1.0 / since : m;   // since~0 => just published => fresh
        return std::min(m, inst);
    }
};

struct ObjectEntry {
    std::uint32_t id = 0;
    std::string name;
    std::vector<PropertyEntry> props;
    PropertyEntry* find(const std::string& p) {
        for (auto& e : props) if (e.name == p) return &e;
        return nullptr;
    }
    const PropertyEntry* find(const std::string& p) const {
        for (auto& e : props) if (e.name == p) return &e;
        return nullptr;
    }
};

class PropertyCatalog {
public:
    void clear() { objects_.clear(); now_ = 0.0; }

    // The catalog's notion of "current time" (the latest sim time seen). The node
    // layer reads frequency AT now() so a property that STALLS (its own stamp
    // freezes) while the publisher keeps ticking still reports a falling Hz. The
    // publisher advances it every tick; setNow() advances it without publishing
    // (e.g. an object removed mid-frame) so its stream goes stale against now().
    void setNow(double t) { now_ = std::max(now_, t); }
    double now() const { return now_; }

    // publish a property value at sim time t (creates the object/property on first
    // sight; records the stamp for the frequency estimate; advances now()).
    void publish(std::uint32_t id, const std::string& objName, const std::string& prop,
                 PropType type, const double* vals, double t) {
        now_ = std::max(now_, t);
        ObjectEntry* o = obj(id);
        if (!o) { objects_.push_back({ id, objName, {} }); o = &objects_.back(); }
        o->name = objName;
        PropertyEntry* e = o->find(prop);
        if (!e) { PropertyEntry ne; ne.name = prop; ne.type = type; o->props.push_back(ne); e = &o->props.back(); }
        const int n = int(type);
        for (int i = 0; i < n; ++i) e->v[i] = vals[i];
        e->record(t);
    }

    // --- introspection (the CATALOG query the node layer uses) ---
    bool hasObject(std::uint32_t id) const { return obj(id) != nullptr; }
    std::vector<std::uint32_t> objectIds() const {
        std::vector<std::uint32_t> v; v.reserve(objects_.size());
        for (auto& o : objects_) v.push_back(o.id); return v;
    }
    std::string objectName(std::uint32_t id) const { const ObjectEntry* o = obj(id); return o ? o->name : std::string(); }
    std::vector<std::string> propertiesOf(std::uint32_t id) const {
        std::vector<std::string> v; const ObjectEntry* o = obj(id);
        if (o) { v.reserve(o->props.size()); for (auto& e : o->props) v.push_back(e.name); }
        return v;
    }
    const PropertyEntry* get(std::uint32_t id, const std::string& prop) const {
        const ObjectEntry* o = obj(id); return o ? o->find(prop) : nullptr;
    }
    double frequency(std::uint32_t id, const std::string& prop, double now) const {
        const PropertyEntry* e = get(id, prop); return e ? e->reportedHz(now) : 0.0;
    }
    int objectCount() const { return int(objects_.size()); }

private:
    std::vector<ObjectEntry> objects_;
    double now_ = 0.0;
    ObjectEntry* obj(std::uint32_t id) { for (auto& o : objects_) if (o.id == id) return &o; return nullptr; }
    const ObjectEntry* obj(std::uint32_t id) const { for (auto& o : objects_) if (o.id == id) return &o; return nullptr; }
};

// process-wide singleton: the SSOT both the publisher and the nodes read.
inline PropertyCatalog& catalog() { static PropertyCatalog c; return c; }

// per-entity previous velocity, for the acceleration finite-difference (accel is
// NOT a stored ECS field -- the sanctioned FidelityRigid approach).
struct AccelTracker {
    std::unordered_map<std::uint32_t, glm::vec3> prevLin, prevAng;
    bool has(std::uint32_t id) const { return prevLin.count(id) != 0; }
};

// the canonical published-property set. publishSceneState() below must publish
// exactly these; the gate cross-checks propertiesOf() against this list, so any
// divergence between the two (a renamed/added/dropped property in either place)
// FAILS the PUBLISH-CATALOG gate -- the drift guard is the gate, not a shared
// table. Keep this list and publishSceneState() in lockstep.
inline std::vector<std::string> canonicalProperties() {
    return { "position", "orientation", "linearVelocity", "angularVelocity",
             "linearAcceleration", "angularAcceleration", "mass" };
}

// resolve a stable human-readable object name (TagComponent.tag else entity-id).
inline std::string objectNameFor(entt::registry& reg, entt::entity e) {
    if (const auto* tag = reg.try_get<TagComponent>(e)) if (!tag->tag.empty()) return tag->tag;
    return "entity-" + std::to_string(std::uint32_t(entt::to_integral(e)));
}

// populate the catalog from the ECS at sim time t. Acceleration = (v - prevV)/dt
// via the tracker (0 on the first observation of a body).
inline void publishSceneState(PropertyCatalog& cat, entt::registry& reg, double t, double dt, AccelTracker& acc) {
    for (auto e : reg.view<RigidBodyComponent, TransformComponent>()) {
        const auto& rb = reg.get<RigidBodyComponent>(e);
        const auto& xf = reg.get<TransformComponent>(e);
        const std::uint32_t id = std::uint32_t(entt::to_integral(e));
        const std::string name = objectNameFor(reg, e);

        double pos[3]  = { xf.translation.x, xf.translation.y, xf.translation.z };
        cat.publish(id, name, "position", PropType::Vec3, pos, t);
        double quat[4] = { xf.rotation.w, xf.rotation.x, xf.rotation.y, xf.rotation.z };
        cat.publish(id, name, "orientation", PropType::Quat, quat, t);
        double lv[3]   = { rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z };
        cat.publish(id, name, "linearVelocity", PropType::Vec3, lv, t);
        double av[3]   = { rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z };
        cat.publish(id, name, "angularVelocity", PropType::Vec3, av, t);

        glm::vec3 la(0.0f), aa(0.0f);
        if (acc.has(id) && dt > 1e-9) {
            la = (rb.linearVelocity  - acc.prevLin[id]) / float(dt);
            aa = (rb.angularVelocity - acc.prevAng[id]) / float(dt);
        }
        acc.prevLin[id] = rb.linearVelocity; acc.prevAng[id] = rb.angularVelocity;
        double laa[3] = { la.x, la.y, la.z }; cat.publish(id, name, "linearAcceleration",  PropType::Vec3, laa, t);
        double aaa[3] = { aa.x, aa.y, aa.z }; cat.publish(id, name, "angularAcceleration", PropType::Vec3, aaa, t);
        double m[1] = { double(rb.mass) }; cat.publish(id, name, "mass", PropType::Scalar, m, t);
    }
}

// GATE TWIN (env KRS_TWIN_SELFTEST; in the bench): PUBLISH-CATALOG /
// OBJECT-PROPERTY-NODES / FREQUENCY-OUTPUT. Returns true iff all pass.
bool runTwinGate();

// GATE QUATERNION-OUTPUT (env KRS_QUATOUT_SELFTEST; in the bench): the rigid-body Property node's quaternion
// output matches the body's actual orientation + bakes into a Transform losslessly; NEG = a stale quat.
bool runQuaternionOutputGate();

} // namespace krs::twin
