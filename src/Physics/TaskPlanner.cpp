// TaskPlanner.cpp -- see TaskPlanner.hpp. Thin STRIPS-style forward search over grounded skills.
#include "TaskPlanner.hpp"
#include "WorldState.hpp"
#include "SkillRuntime.hpp"
#include "PropertyCatalog.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "SubgraphNode.hpp"
#include "KNode.hpp"
#include "KDoc.hpp"
#include "RobotModel.hpp"
#include "RobotBuilderScene.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <cstdio>
#include <cmath>
#include <deque>
#include <set>

namespace krs::skill {

namespace {

// Canonical positive text of a predicate (the STRIPS fact key -- sign carried separately).
std::string factKey(const Predicate& p) {
    Predicate pos = p; pos.negated = false;
    return pos.text();
}

// A search node: the symbolic fact overlay + the step indices taken to get here.
struct PlanState {
    std::set<std::string> assertedTrue, assertedFalse;
    std::vector<int> steps;                 // indices into the library
    std::string signature() const {         // dedupe key (facts fully determine the symbolic state)
        std::string s;
        for (const auto& f : assertedTrue)  { s += '+'; s += f; }
        for (const auto& f : assertedFalse) { s += '-'; s += f; }
        return s;
    }
};

bool evalInState(const Predicate& p, const PlanState& st, const krs::world::WorldState& ws) {
    const std::string key = factKey(p);
    bool v;
    if (st.assertedTrue.count(key))       v = true;
    else if (st.assertedFalse.count(key)) v = false;
    else { Predicate pos = p; pos.negated = false; v = pos.eval(ws); }   // fall through to observation
    return p.negated ? !v : v;
}
void applyInState(const Predicate& e, PlanState& st) {
    const std::string key = factKey(e);
    if (e.negated) { st.assertedFalse.insert(key); st.assertedTrue.erase(key); }
    else           { st.assertedTrue.insert(key);  st.assertedFalse.erase(key); }
}
bool goalMet(const std::vector<Predicate>& goal, const PlanState& st, const krs::world::WorldState& ws) {
    for (const auto& g : goal) if (!evalInState(g, st, ws)) return false;
    return true;
}

} // namespace

PlanResult planTask(const std::vector<SkillStep>& library, const krs::world::WorldState& start,
                    const std::vector<Predicate>& goal, int maxDepth, int maxNodes) {
    PlanResult out;
    if (goal.empty()) { out.why = "empty goal"; return out; }

    PlanState root;
    if (goalMet(goal, root, start)) { out.ok = true; return out; }       // already satisfied: empty plan
    if (library.empty()) { out.why = "empty skill library"; return out; }

    std::deque<PlanState> open{ root };
    std::set<std::string> seen{ root.signature() };
    while (!open.empty()) {
        if (out.nodesExpanded >= maxNodes) { out.why = "node cap reached without a plan"; return out; }
        PlanState cur = std::move(open.front()); open.pop_front();
        ++out.nodesExpanded;
        if (int(cur.steps.size()) >= maxDepth) continue;

        for (int i = 0; i < int(library.size()); ++i) {
            const SkillSpec* sp = library[i].spec;
            if (!sp) continue;
            bool applicable = true;
            for (const auto& p : sp->pre) if (!evalInState(p, cur, start)) { applicable = false; break; }
            if (!applicable) continue;
            PlanState next = cur;
            next.steps.push_back(i);
            for (const auto& e : sp->eff) applyInState(e, next);
            if (goalMet(goal, next, start)) {
                out.ok = true;
                for (int idx : next.steps) out.steps.push_back(library[idx]);
                return out;                                              // BFS: shallowest plan wins
            }
            const std::string sig = next.signature();
            if (seen.insert(sig).second) open.push_back(std::move(next));
        }
    }
    out.why = "search exhausted without reaching the goal";
    return out;
}

// ================================================================================================
// GATE TASKPLAN (Process & Skills P5) -- goal in, ordered executable plan out; replans around faults.
// ================================================================================================
bool runTaskPlanGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[taskplan] GATE TASKPLAN -- forward search: goal -> ordered skills; replans around a jammed gripper; executes\n");
    auto& F = NodeFactory::instance();
    bool allOk = true;

    // ---- live demo robot + world (same fixture family as P1/P3/P4) ----
    Scene scene; auto& reg = scene.getRegistry();
    krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
    g.robotId = 0;
    krs::rbuild::spawnGraphBodies(scene, g, 0);
    krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, 0);
    if (!lr) { printf("[taskplan] FAIL: no live robot\n"); return false; }
    const int n = lr->ndof();
    auto* bus = &reg.ctx().emplace<ArticulationCommandComponent>();
    krs::world::WorldState& ws = krs::world::worldState(reg);
    {
        krs::twin::PropertyCatalog seedCat;
        double p[3] = { 0.6, 0.1, 0.0 };
        seedCat.publish(40, "cup", "position", krs::twin::PropType::Vec3, p, 0.05);
        ws.updateFromCatalog(seedCat);
    }
    ws.setFrame("drop_pose", glm::vec3(1.2f, 0.4f, 0.3f));
    ws.setGripperOpen(0, true);

    // ---- the grounded skill library: open_gripper, pick(cup), place(cup, drop_pose) ----
    krs::knode::KNodeDoc moveDoc;
    moveDoc.id = "tp-move"; moveDoc.name = "Move"; moveDoc.category = "Skills";
    {
        auto cfgDrive = F.createNode("physics_config_drive");
        cfgDrive->setPortLiteral<int>("Robot", 0);
        krs::knode::InteriorNode m; m.id = "d"; m.typeId = "physics_config_drive";
        m.state = krs::kdoc::nodeToJson(*cfgDrive, "physics_config_drive");
        moveDoc.nodes.push_back(m);
    }
    { krs::knode::ExposedPort p; p.name = "target"; p.interiorNode = "d"; p.interiorPort = "Config";
      p.isInput = true; p.dataType = "joint_config"; p.role = "targetConfig"; moveDoc.ports.push_back(p); }

    Eigen::VectorXd qGrasp(n), qPlace(n);
    for (int i = 0; i < n; ++i) { qGrasp[i] = 0.10 + 0.02 * i; qPlace[i] = -0.10 - 0.02 * i; }
    auto realizeMove = [&moveDoc, lr](const Eigen::VectorXd& target) {
        return [&moveDoc, lr, target](Scene* sc, const ParamMap&) {
            auto body = std::make_shared<krs::nodes::SubgraphNode>(moveDoc);
            return makeSkillLeaf(body, sc, { { "target", ParamValue::cfg(target) } }, qNear(lr, target));
        };
    };

    SkillSpec openGrip;                       // the fault-recovery skill the planner can discover
    openGrip.name = "open_gripper";
    openGrip.pre  = { { Predicate::Kind::GripperOpen, true, 0 } };      // gripper must be CLOSED
    openGrip.eff  = { { Predicate::Kind::GripperOpen, false, 0 } };
    openGrip.realize = [&ws](Scene*, const ParamMap&) {                  // no motion: actuate the fact
        return [&ws](double, Blackboard&) { ws.setGripperOpen(0, true); return Status::Success; };
    };
    openGrip.timeoutSec = 1.0;

    SkillSpec pick;
    pick.name = "pick";
    pick.pre = { { Predicate::Kind::GripperOpen, false, 0 },
                 { Predicate::Kind::Fresh, false, 0, "cup", "", 10.0 } };
    pick.eff = { { Predicate::Kind::Holding, false, 0, "cup" },
                 { Predicate::Kind::GripperOpen, true, 0 } };
    pick.realize = realizeMove(qGrasp);
    pick.timeoutSec = 2.0;

    SkillSpec place;
    place.name = "place";
    place.pre = { { Predicate::Kind::Holding, false, 0, "cup" } };
    place.eff = { { Predicate::Kind::Holding, true, 0, "cup" },
                  { Predicate::Kind::GripperOpen, false, 0 },
                  { Predicate::Kind::At, false, 0, "cup", "drop_pose", 0.1 } };
    place.realize = realizeMove(qPlace);
    place.timeoutSec = 2.0;

    const std::vector<SkillStep> library = { { &openGrip, {} }, { &pick, {} }, { &place, {} } };
    const std::vector<Predicate> goal = { { Predicate::Kind::At, false, 0, "cup", "drop_pose", 0.1 } };

    // executor: pump a composed plan under the real per-pass lifecycle.
    auto execute = [&](const std::vector<SkillStep>& steps) {
        SkillRuntime rt;
        const int id = rt.start("planned", composeSequence(steps, &scene, ws));
        const double dt = 1.0 / 60.0;
        for (int k = 0; k < 600 && rt.anyRunning(); ++k) {
            bus->clearForEvalPass();
            rt.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
        }
        return rt.status(id);
    };

    // ---- (1) plan from the clean start: [pick, place] in order; executes to Success ----
    {
        const PlanResult pr = planTask(library, ws, goal);
        const bool orderOk = pr.ok && pr.steps.size() == 2
            && pr.steps[0].spec->name == "pick" && pr.steps[1].spec->name == "place";
        const bool execOk = orderOk && execute(pr.steps) == Status::Success
            && (lr->q - qPlace).cwiseAbs().maxCoeff() < 1e-4 && !ws.isHolding(0, "cup");
        printf("[taskplan]   (1) goal at(cup,drop_pose) -> plan [%s] (%d nodes) ; executes=%d  %s\n",
               pr.ok ? (pr.steps.size() == 2 ? "pick, place" : "unexpected") : "none",
               pr.nodesExpanded, int(execOk), (orderOk && execOk) ? "PASS" : "FAIL");
        allOk = allOk && orderOk && execOk;
    }

    // ---- (2) REPLAN AROUND A FAULT: gripper jammed shut -> [open_gripper, pick, place]; executes ----
    {
        ws.setGripperOpen(0, false);                       // the exact state P4's neg-ctrl failed on
        ws.setHolding(0, "");
        const PlanResult pr = planTask(library, ws, goal);
        const bool orderOk = pr.ok && pr.steps.size() == 3
            && pr.steps[0].spec->name == "open_gripper"
            && pr.steps[1].spec->name == "pick" && pr.steps[2].spec->name == "place";
        const bool execOk = orderOk && execute(pr.steps) == Status::Success && ws.gripperOpen(0);
        printf("[taskplan]   (2) jammed gripper -> plan [%s] ; executes=%d  %s\n",
               orderOk ? "open_gripper, pick, place" : (pr.ok ? "unexpected" : pr.why.c_str()),
               int(execOk), (orderOk && execOk) ? "PASS" : "FAIL");
        allOk = allOk && orderOk && execOk;
    }

    // ---- (3) an already-satisfied goal yields an EMPTY plan ----
    {
        const std::vector<Predicate> trivial = { { Predicate::Kind::GripperOpen, false, 0 } };
        const PlanResult pr = planTask(library, ws, trivial);
        const bool ok = pr.ok && pr.steps.empty();
        printf("[taskplan]   (3) already-satisfied goal -> empty plan=%d  %s\n", int(ok), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- NEG-CTRLs: unreachable goal fails with a reason (bounded); empty library fails loud ----
    {
        const std::vector<Predicate> impossible = { { Predicate::Kind::Holding, false, 1, "unobtainium" } };
        const PlanResult pr = planTask(library, ws, impossible, 6, 5000);
        const bool boundedFail = !pr.ok && !pr.why.empty();
        const PlanResult pe = planTask({}, ws, goal);
        const bool emptyFail = !pe.ok && pe.why.find("empty") != std::string::npos;
        const bool negOk = boundedFail && emptyFail;
        printf("[taskplan]   NEG-CTRLs: unreachable goal bounded-fail=%d (\"%s\") ; empty library fails=%d  %s\n",
               int(boundedFail), pr.why.c_str(), int(emptyFail), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[taskplan] %s\n", allOk ? "ALL PASS (goal -> ordered executable plan by effect/precondition matching; replans around a jammed gripper and EXECUTES the recovery; empty-plan/unreachable/empty-library honest)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::skill
