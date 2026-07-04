// WorldState.cpp -- see WorldState.hpp. The light task-level world model: catalog-derived object
// poses by name + task frames + gripper/holding facts + the predicate view.
#include "WorldState.hpp"
#include "PropertyCatalog.hpp"

#include <cstdio>
#include <cmath>

namespace krs::world {

WorldObject* WorldState::obj(const std::string& name) {
    for (auto& o : objects_) if (o.name == name) return &o;
    return nullptr;
}
const WorldObject* WorldState::object(const std::string& name) const {
    for (const auto& o : objects_) if (o.name == name) return &o;
    return nullptr;
}
std::vector<std::string> WorldState::objectNames() const {
    std::vector<std::string> v; v.reserve(objects_.size());
    for (const auto& o : objects_) v.push_back(o.name);
    return v;
}

void WorldState::updateFromCatalog(const krs::twin::PropertyCatalog& cat) {
    now = cat.now();
    for (std::uint32_t id : cat.objectIds()) {
        const std::string name = cat.objectName(id);
        if (name.empty()) continue;
        const auto* pe = cat.get(id, "position");
        WorldObject* o = obj(name);
        if (!o) { objects_.push_back({}); o = &objects_.back(); o->name = name; }
        o->entityId = id;
        o->lastSeen = now;
        if (pe) {   // reassemble the split streams into ONE pose
            o->pos = glm::vec3(float(pe->v[0]), float(pe->v[1]), float(pe->v[2]));
            if (const auto* qe = cat.get(id, "orientation"))
                o->rot = glm::quat(float(qe->v[0]), float(qe->v[1]), float(qe->v[2]), float(qe->v[3]));  // (w,x,y,z)
            o->hasPose = true;
        } else if (const auto* ee = cat.get(id, "endEffector")) {
            // a robot root publishes no "position", but its end-effector IS the pose a task cares about.
            o->pos = glm::vec3(float(ee->v[0]), float(ee->v[1]), float(ee->v[2]));
            o->hasPose = true;
        }
    }
    // objects the catalog no longer lists are KEPT (last pose, stale lastSeen) -- stale-detectable,
    // never silently re-bound.
}

void WorldState::setFrame(const std::string& name, const glm::vec3& p, const glm::quat& r) {
    frames_[name] = { p, r };
}
const TaskFrame* WorldState::frame(const std::string& name) const {
    auto it = frames_.find(name);
    return it == frames_.end() ? nullptr : &it->second;
}

void WorldState::setGripperOpen(int robotId, bool open) { gripperOpen_[robotId] = open; }
bool WorldState::gripperOpen(int robotId) const {
    auto it = gripperOpen_.find(robotId);
    return it == gripperOpen_.end() ? true : it->second;
}
void WorldState::setHolding(int robotId, const std::string& objName) {
    if (objName.empty()) holding_.erase(robotId); else holding_[robotId] = objName;
}
std::string WorldState::holding(int robotId) const {
    auto it = holding_.find(robotId);
    return it == holding_.end() ? std::string() : it->second;
}
bool WorldState::isHolding(int robotId, const std::string& objName) const {
    return !objName.empty() && holding(robotId) == objName;
}

bool WorldState::positionOf(const std::string& objOrFrame, glm::vec3* out) const {
    if (const WorldObject* o = object(objOrFrame); o && o->hasPose) { if (out) *out = o->pos; return true; }
    if (const TaskFrame* f = frame(objOrFrame)) { if (out) *out = f->pos; return true; }
    return false;
}
bool WorldState::near(const std::string& a, const std::string& b, double tol) const {
    glm::vec3 pa, pb;
    if (!positionOf(a, &pa) || !positionOf(b, &pb)) return false;   // unknown name -> false, never fabricated
    return double(glm::length(pa - pb)) < tol;
}
bool WorldState::fresh(const std::string& objName, double maxAge) const {
    const WorldObject* o = object(objName);
    return o && (now - o->lastSeen) <= maxAge;
}

WorldState& worldState(entt::registry& reg) {
    auto* ws = reg.ctx().find<WorldState>();
    return ws ? *ws : reg.ctx().emplace<WorldState>();
}

// ================================================================================================
// GATE WORLDSTATE (Process & Skills P2)
// ================================================================================================
bool runWorldStateGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[world] GATE WORLDSTATE -- catalog-derived poses by name + frames + gripper/holding facts + predicates\n");
    bool allOk = true;
    krs::twin::PropertyCatalog cat;
    WorldState ws;

    // ---- publish two named objects (position + orientation) + a robot-ish endEffector object ----
    auto pub = [&cat](std::uint32_t id, const char* name, double x, double y, double z, double t) {
        double p[3] = { x, y, z };  cat.publish(id, name, "position", krs::twin::PropType::Vec3, p, t);
        double q[4] = { 0.9238795, 0.0, 0.3826834, 0.0 };   // 45deg about Y, (w,x,y,z)
        cat.publish(id, name, "orientation", krs::twin::PropType::Quat, q, t);
    };
    pub(10, "cup",   1.0, 0.5, 0.0, 0.1);
    pub(11, "plate", 2.0, 0.5, 1.0, 0.1);
    { double ee[3] = { 0.3, 1.2, 0.4 }; cat.publish(12, "robot0", "endEffector", krs::twin::PropType::Vec3, ee, 0.1); }
    ws.updateFromCatalog(cat);

    const WorldObject* cup = ws.object("cup");
    const WorldObject* rob = ws.object("robot0");
    const bool poseOk = cup && cup->hasPose
        && glm::length(cup->pos - glm::vec3(1.0f, 0.5f, 0.0f)) < 1e-6f
        && std::abs(cup->rot.w - 0.9238795f) < 1e-5f && std::abs(cup->rot.y - 0.3826834f) < 1e-5f
        && rob && rob->hasPose && glm::length(rob->pos - glm::vec3(0.3f, 1.2f, 0.4f)) < 1e-6f;
    // a moved object's pose TRACKS the next update
    pub(10, "cup", 1.5, 0.5, 0.0, 0.2);
    ws.updateFromCatalog(cat);
    const bool tracks = ws.object("cup") && std::abs(ws.object("cup")->pos.x - 1.5f) < 1e-6f;
    printf("[world]   poses-by-name: cup pos+quat exact=%d robot endEffector=%d moved-cup tracks=%d  %s\n",
           int(poseOk), int(rob && rob->hasPose), int(tracks), (poseOk && tracks) ? "PASS" : "FAIL");
    allOk = allOk && poseOk && tracks;

    // ---- frames + facts + predicates ----
    ws.setFrame("bin_A", glm::vec3(1.6f, 0.5f, 0.0f));
    ws.setFrame("far_frame", glm::vec3(9.0f, 9.0f, 9.0f));
    ws.setGripperOpen(0, false);
    ws.setHolding(0, "cup");
    const bool factOk = !ws.gripperOpen(0) && ws.isHolding(0, "cup") && !ws.isHolding(0, "plate")
        && ws.gripperOpen(1);                                    // unknown gripper defaults open
    ws.setHolding(0, "");                                        // release
    const bool releaseOk = !ws.isHolding(0, "cup") && ws.holding(0).empty();
    const bool predOk = ws.at("cup", "bin_A", 0.2)               // |1.5-1.6| = 0.1 < 0.2
        && !ws.at("cup", "bin_A", 0.05)                          // ...but not within 0.05 (discriminates)
        && !ws.near("cup", "far_frame", 1.0)
        && ws.near("cup", "plate", 2.0) && !ws.near("cup", "plate", 0.5);
    printf("[world]   facts: gripper/holding round-trip=%d release=%d ; predicates at/near discriminate=%d  %s\n",
           int(factOk), int(releaseOk), int(predOk), (factOk && releaseOk && predOk) ? "PASS" : "FAIL");
    allOk = allOk && factOk && releaseOk && predOk;

    // ---- NEG-CTRLs: staleness (catalog stops publishing) + unknown names never fabricate ----
    {
        // plate stops being published; cup keeps updating -> plate goes stale, keeps its LAST pose.
        pub(10, "cup", 1.5, 0.5, 0.0, 1.0);
        ws.updateFromCatalog(cat);                               // now = 1.0; plate lastSeen also bumped (still listed)
        // simulate a catalog that no longer lists plate: a FRESH catalog without it
        krs::twin::PropertyCatalog cat2;
        { double p[3] = { 1.5, 0.5, 0.0 }; cat2.publish(10, "cup", "position", krs::twin::PropType::Vec3, p, 2.0); }
        ws.updateFromCatalog(cat2);                              // now = 2.0; plate untouched
        const bool staleOk = !ws.fresh("plate", 0.5) && ws.fresh("cup", 0.5)
            && ws.object("plate") && std::abs(ws.object("plate")->pos.x - 2.0f) < 1e-6f;  // last pose kept
        glm::vec3 dummy;
        const bool unknownOk = !ws.positionOf("no_such_thing", &dummy) && !ws.near("no_such_thing", "cup", 100.0);
        const bool negOk = staleOk && unknownOk;
        printf("[world]   NEG-CTRLs: vanished object stale-but-kept=%d unknown-name-never-fabricates=%d  %s\n",
               int(staleOk), int(unknownOk), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[world] %s\n", allOk ? "ALL PASS (poses reassemble by name + track; frames/facts/predicates work; staleness honest; unknowns never fabricated)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::world
