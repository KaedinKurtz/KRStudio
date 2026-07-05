// GroupOps.cpp -- the KRS_GROUP_SELFTEST gate for krs::group (see GroupOps.hpp).
// Headless, pure CPU, synthetic entities in a Scene registry; analytic assertions
// with measured numbers; NEG-CTRLs. Mirrors the Measure gate's style.
#include "GroupOps.hpp"
#include "Scene.hpp"

#include <cmath>
#include <cstdio>

namespace krs::group {

namespace {
struct GSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[group]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};
bool near3(const glm::vec3& a, const glm::vec3& b, float tol = 1e-5f) {
    return glm::length(a - b) < tol;
}
} // namespace

bool runGroupGate() {
    std::printf("[group] ================ GROUP GATE (krs::group) ================\n");
    GSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();

    auto mkBox = [&](glm::vec3 p, const char* name) {
        entt::entity e = reg.create();
        reg.emplace<TransformComponent>(e, p, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        reg.emplace<TagComponent>(e, std::string(name));
        return e;
    };
    entt::entity a = mkBox({ 1, 0, 0 }, "A");
    entt::entity b = mkBox({ 3, 0, 0 }, "B");

    // ---- (1) makeGroup: root spawns at the members' combined centre. ----
    entt::entity g1 = makeGroup(reg, "G1", { a, b });
    const glm::vec3 rootP = reg.get<TransformComponent>(g1).translation;
    std::printf("[group] root at (%.3f, %.3f, %.3f) (want 2, 0, 0)\n", rootP.x, rootP.y, rootP.z);
    S.check("MAKE-CENTRE       group root spawns at the member centroid (2,0,0)",
            near3(rootP, { 2, 0, 0 }));
    S.check("MEMBERSHIP        both members resolve topGroupOf -> the root",
            topGroupOf(reg, a) == g1 && topGroupOf(reg, b) == g1);

    // ---- (2) TRANSLATE the root: members follow by exactly the delta. ----
    reg.get<TransformComponent>(g1).translation += glm::vec3(0, 2, 0);
    applyRootDelta(reg, g1);
    const glm::vec3 pa = reg.get<TransformComponent>(a).translation;
    const glm::vec3 pb = reg.get<TransformComponent>(b).translation;
    std::printf("[group] after +2Y: A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f)\n",
                pa.x, pa.y, pa.z, pb.x, pb.y, pb.z);
    S.check("TRANSLATE-FANOUT  root +2Y -> members at (1,2,0) and (3,2,0) exactly",
            near3(pa, { 1, 2, 0 }) && near3(pb, { 3, 2, 0 }));

    // ---- (3) ROTATE the root 90 deg about Z: members orbit the group centre. ----
    reg.get<TransformComponent>(g1).rotation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    applyRootDelta(reg, g1);
    const glm::vec3 ra = reg.get<TransformComponent>(a).translation;
    const glm::vec3 rb = reg.get<TransformComponent>(b).translation;
    std::printf("[group] after rotZ90: A=(%.3f,%.3f,%.3f) (want 2,1,0)  B=(%.3f,%.3f,%.3f) (want 2,3,0)\n",
                ra.x, ra.y, ra.z, rb.x, rb.y, rb.z);
    S.check("ROTATE-FANOUT     root rotZ90 -> members orbit the centre to (2,1,0)/(2,3,0)",
            near3(ra, { 2, 1, 0 }) && near3(rb, { 2, 3, 0 }));
    const float wA = std::abs(glm::degrees(glm::angle(reg.get<TransformComponent>(a).rotation)) - 90.0f);
    S.check("ROTATE-ORIENT     member orientation carries the 90 deg (measured err < 0.01 deg)",
            wA < 0.01f);

    // ---- (4) NESTING: an outer group over {g1, c}; outer translate reaches the leaves. ----
    entt::entity c = mkBox({ 5, 2, 0 }, "C");
    entt::entity g2 = makeGroup(reg, "G2", { g1, c });
    S.check("NEST-TOP          topGroupOf(leaf A) walks to the OUTER root",
            topGroupOf(reg, a) == g2);
    reg.get<TransformComponent>(g2).translation += glm::vec3(0, 0, 3);
    applyRootDelta(reg, g2);
    S.check("NEST-FANOUT       outer +3Z reaches nested leaves (A.z == 3)",
            std::abs(reg.get<TransformComponent>(a).translation.z - 3.0f) < 1e-5f
            && std::abs(reg.get<TransformComponent>(c).translation.z - 3.0f) < 1e-5f);

    // ---- (5) leafTargets expands nested groups to plain entities only. ----
    const auto leaves = leafTargets(reg, g2);
    std::printf("[group] leafTargets(outer) -> %d leaves (want 3)\n", int(leaves.size()));
    S.check("LEAF-EXPAND       apply targets = the 3 plain bodies, no group roots",
            leaves.size() == 3);

    // ---- (6) UNGROUP the nested group: members PROMOTE to the outer group; nothing moves. ----
    const glm::vec3 beforeA = reg.get<TransformComponent>(a).translation;
    ungroup(reg, g1);
    S.check("UNGROUP-PROMOTE   dissolving the nested group promotes members to the outer group",
            reg.valid(a) && topGroupOf(reg, a) == g2 && !reg.valid(g1));
    S.check("UNGROUP-STATIC    ungrouping moves NOTHING (A stays put)",
            near3(reg.get<TransformComponent>(a).translation, beforeA));

    // ---- (7) NEG-CTRLs. ----
    applyRootDelta(reg, a);                       // plain entity: no-op, no crash
    S.check("NEG-CTRL          applyRootDelta on a non-group entity is a safe no-op",
            near3(reg.get<TransformComponent>(a).translation, beforeA));
    entt::entity x = mkBox({ 0, 0, 0 }, "X");
    entt::entity y = mkBox({ 1, 0, 0 }, "Y");
    reg.emplace<GroupComponent>(x).name = "X";    // adversarial CYCLE: X member of Y, Y member of X
    reg.emplace<GroupComponent>(y).name = "Y";
    reg.emplace<GroupMemberComponent>(x, GroupMemberComponent{ y });
    reg.emplace<GroupMemberComponent>(y, GroupMemberComponent{ x });
    const entt::entity top = topGroupOf(reg, x);  // must terminate (guard), whatever it returns
    S.check("NEG-CTRL          membership cycle terminates (guarded walk, no hang)",
            top == x || top == y);

    const bool pass = (S.pass == S.total);
    std::printf("[group] %d/%d checks\n", S.pass, S.total);
    std::printf("[group] %s\n", pass
        ? "ALL PASS (centroid root; translate/rotate fan-out exact; nesting reaches leaves; "
          "ungroup promotes without motion; cycle-guarded)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::group
