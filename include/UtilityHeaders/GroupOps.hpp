#pragma once
// ===========================================================================
// OBJECT GROUPS (krs::group) -- nested macro objects over WORLD transforms.
//
// Design: a group ROOT is an empty entity (TransformComponent at the members'
// combined centre, GroupComponent, TagComponent = the group name). Members
// keep their WORLD TransformComponents and carry GroupMemberComponent -- the
// render path is untouched; the root fans its transform DELTA out to members
// whenever it moves (applyRootDelta, called from the gizmo edit hook).
//
// Selection model (the CAD convention the user asked for): clicking any member
// selects the TOP group (topGroupOf); Alt+click selects the member itself.
// Applies (textures, physics properties, ...) expand to LEAF members.
// ===========================================================================
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "components.hpp"

namespace krs::group {

// Walk to the TOPMOST group root above e (nesting: a member may be a group root itself).
// Returns e unchanged when it belongs to no group. Cycle-guarded.
inline entt::entity topGroupOf(entt::registry& reg, entt::entity e) {
    int guard = 0;
    while (reg.valid(e) && guard++ < 64) {
        const auto* m = reg.try_get<GroupMemberComponent>(e);
        if (!m || !reg.valid(m->group) || !reg.all_of<GroupComponent>(m->group)) break;
        e = m->group;
    }
    return e;
}

// DIRECT members of a group root (one level).
inline std::vector<entt::entity> groupMembers(entt::registry& reg, entt::entity root) {
    std::vector<entt::entity> out;
    for (auto e : reg.view<GroupMemberComponent>())
        if (reg.get<GroupMemberComponent>(e).group == root) out.push_back(e);
    return out;
}

// LEAF targets of an apply (textures, properties): a plain entity is itself; a group root expands
// to every non-group descendant. Cycle/degeneracy-guarded by depth.
inline void expandLeafTargets(entt::registry& reg, entt::entity e,
                              std::vector<entt::entity>& out, int depth = 0) {
    if (!reg.valid(e) || depth > 32) return;
    if (reg.all_of<GroupComponent>(e)) {
        for (entt::entity m : groupMembers(reg, e)) expandLeafTargets(reg, m, out, depth + 1);
    } else {
        out.push_back(e);
    }
}
inline std::vector<entt::entity> leafTargets(entt::registry& reg, entt::entity e) {
    std::vector<entt::entity> out;
    expandLeafTargets(reg, e, out);
    return out;
}

// Create a group from >=1 entities: spawns the ROOT at the members' combined centre (mean of
// member translations -- cheap and stable), names it, and tags each member. Members already in
// another group are RE-parented to the new group (their old membership is overwritten -- the new
// group may itself be grouped later, giving nesting the other way). Returns the root.
inline entt::entity makeGroup(entt::registry& reg, const std::string& name,
                              const std::vector<entt::entity>& members) {
    entt::entity root = reg.create();
    glm::vec3 centre(0.0f);
    int n = 0;
    for (entt::entity m : members)
        if (reg.valid(m))
            if (const auto* tc = reg.try_get<TransformComponent>(m)) { centre += tc->translation; ++n; }
    if (n > 0) centre /= float(n);
    auto& tc = reg.emplace<TransformComponent>(root);
    tc.translation = centre;
    auto& gc = reg.emplace<GroupComponent>(root);
    gc.name = name;
    gc.lastXf = tc.getTransform();
    reg.emplace<TagComponent>(root, name);
    for (entt::entity m : members) {
        if (!reg.valid(m) || m == root) continue;
        reg.emplace_or_replace<GroupMemberComponent>(m, GroupMemberComponent{ root });
    }
    return root;
}

// Dissolve a group: members keep their world transforms (nothing moves), the root dies.
// Members re-attach to the root's OWN group if the root was nested (so ungrouping a child
// group promotes its members one level, Blender-style).
inline void ungroup(entt::registry& reg, entt::entity root) {
    if (!reg.valid(root) || !reg.all_of<GroupComponent>(root)) return;
    const auto* rootMembership = reg.try_get<GroupMemberComponent>(root);
    const entt::entity promoteTo = rootMembership ? rootMembership->group : entt::null;
    for (entt::entity m : groupMembers(reg, root)) {
        if (promoteTo != entt::null && reg.valid(promoteTo))
            reg.emplace_or_replace<GroupMemberComponent>(m, GroupMemberComponent{ promoteTo });
        else
            reg.remove<GroupMemberComponent>(m);
    }
    reg.destroy(root);
}

// Fan the root's transform DELTA out to every member (recursively through nested groups), then
// re-arm lastXf. Members' WORLD transforms compose with the delta matrix; TRS is re-decomposed
// (uniform-ish scale assumption -- per-axis lengths of the basis vectors). Call after any edit
// to the root's TransformComponent (the gizmo hook / property panel).
inline void applyRootDelta(entt::registry& reg, entt::entity root) {
    if (!reg.valid(root)) return;
    auto* gc = reg.try_get<GroupComponent>(root);
    const auto* rtc = reg.try_get<TransformComponent>(root);
    if (!gc || !rtc) return;
    const glm::mat4 cur = rtc->getTransform();
    const glm::mat4 delta = cur * glm::inverse(gc->lastXf);
    // near-identity -> nothing to do (avoids drift from repeated decompose round-trips)
    bool moved = false;
    for (int c = 0; c < 4 && !moved; ++c)
        for (int r = 0; r < 4 && !moved; ++r)
            if (std::abs(delta[c][r] - (c == r ? 1.0f : 0.0f)) > 1e-7f) moved = true;
    if (!moved) { gc->lastXf = cur; return; }

    for (entt::entity m : groupMembers(reg, root)) {
        if (auto* mtc = reg.try_get<TransformComponent>(m)) {
            const glm::mat4 w = delta * mtc->getTransform();
            const glm::vec3 sx(w[0]), sy(w[1]), sz(w[2]);
            const glm::vec3 scale(glm::length(sx), glm::length(sy), glm::length(sz));
            glm::mat3 rot(1.0f);
            if (scale.x > 1e-9f && scale.y > 1e-9f && scale.z > 1e-9f)
                rot = glm::mat3(glm::vec3(w[0]) / scale.x, glm::vec3(w[1]) / scale.y, glm::vec3(w[2]) / scale.z);
            mtc->translation = glm::vec3(w[3]);
            mtc->rotation = glm::normalize(glm::quat_cast(rot));
            mtc->scale = scale;
        }
        // nested group roots: their OWN members follow through their own delta pass
        if (reg.all_of<GroupComponent>(m)) applyRootDelta(reg, m);
    }
    gc->lastXf = cur;
}

// Headless gate (KRS_GROUP_SELFTEST): make/nest/move/ungroup with analytic assertions.
bool runGroupGate();

} // namespace krs::group
