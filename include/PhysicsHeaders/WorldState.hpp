#pragma once
// ===========================================================================
// WORLD STATE -- the LIGHT task-level world model (krs::world), Process & Skills P2.
//
// The task layer's single typed source of truth about "what is where":
//   * OBJECTS by stable NAME (poses reassembled each update from the live
//     PropertyCatalog's split position/orientation streams -- decoupled from the
//     recyclable u32 entity ids the catalog is keyed by);
//   * named TASK FRAMES ("home", "bin_A", "drop_pose") -> world poses;
//   * GRIPPER / ATTACHMENT facts (gripperOpen, holding(robotId) -> object) --
//     symbolic facts a skill's pre/post-conditions read and its effects write
//     (kinematic re-parenting of a held object is a separate, later work item);
//   * a PREDICATE view (at/near/holding/gripperOpen) for skill conditions and,
//     later, the task planner's fact base.
//
// DELIBERATELY LIGHT: no meshes, no occupancy, no reconstruction -- the later
// perception sprint PLUGS INTO this (it populates objects/poses from sensors
// instead of the catalog). Facts live HERE, not in the PropertyCatalog (whose
// canonical property set is gate-locked). ctx singleton via worldState(reg).
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace krs::twin { class PropertyCatalog; }

namespace krs::world {

// One tracked object: the catalog's split position/orientation reassembled into a pose, keyed by
// the stable human NAME (TagComponent), with the volatile entity id kept only as information.
struct WorldObject {
    std::string   name;
    std::uint32_t entityId = 0;      // catalog key at last update (recyclable -- do NOT address by it)
    glm::vec3     pos = glm::vec3(0);
    glm::quat     rot = glm::quat(1, 0, 0, 0);
    bool          hasPose = false;   // an object seen without a full pose stays "present but poseless"
    double        lastSeen = -1.0;   // WorldState.now at the last update that included it
};

// A named task frame (a world pose a skill/planner targets: "home", "bin_A", "drop_pose").
struct TaskFrame { glm::vec3 pos = glm::vec3(0); glm::quat rot = glm::quat(1, 0, 0, 0); };

struct WorldState {
    double now = 0.0;

    // ---- observation (derived): refresh objects from the live catalog. An object that vanished
    //      from the catalog is KEPT with its last pose but its lastSeen stops advancing (stale-
    //      detectable via now - lastSeen), never silently re-bound to a recycled id. ----
    void updateFromCatalog(const krs::twin::PropertyCatalog& cat);

    const WorldObject* object(const std::string& name) const;
    std::vector<std::string> objectNames() const;

    // ---- task frames (authored / asserted) ----
    void setFrame(const std::string& name, const glm::vec3& p, const glm::quat& r = glm::quat(1, 0, 0, 0));
    const TaskFrame* frame(const std::string& name) const;

    // ---- gripper / attachment facts (skill effects write these; preconditions read them) ----
    void setGripperOpen(int robotId, bool open);
    bool gripperOpen(int robotId) const;                    // default true (an unknown gripper is open)
    void setHolding(int robotId, const std::string& objName);   // "" = release
    std::string holding(int robotId) const;

    // ---- predicates (the symbolic view skills + the planner evaluate) ----
    bool isHolding(int robotId, const std::string& objName) const;
    // position of a named object OR frame (objects win on a name clash); false if unknown.
    bool positionOf(const std::string& objOrFrame, glm::vec3* out) const;
    bool near(const std::string& a, const std::string& b, double tol) const;   // |posA - posB| < tol
    bool at(const std::string& obj, const std::string& frameName, double tol) const { return near(obj, frameName, tol); }
    // staleness: seen within `maxAge` of now (an object the catalog stopped publishing goes stale).
    bool fresh(const std::string& objName, double maxAge) const;

private:
    std::vector<WorldObject> objects_;
    std::map<std::string, TaskFrame> frames_;
    std::map<int, bool> gripperOpen_;
    std::map<int, std::string> holding_;
    WorldObject* obj(const std::string& name);
};

// ctx singleton accessor (get-or-emplace on the scene registry).
WorldState& worldState(entt::registry& reg);

// Headless gate (KRS_WORLDSTATE_SELFTEST): two named objects + a robot published into a live
// catalog reassemble into poses-by-name (position + quaternion exact); a moved object's pose tracks
// the next update; task frames resolve; gripper/holding facts round-trip and drive isHolding; the
// at/near predicates discriminate true from false at their tolerance; NEG-CTRLs: an object the
// catalog stops publishing goes STALE (fresh() false) but never re-binds; an unknown name yields
// false, never a fabricated pose.
bool runWorldStateGate();

} // namespace krs::world
