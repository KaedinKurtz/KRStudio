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
// Goal Workspace G2/G3: goal regression + knowledge gaps + excitation insertion
// ================================================================================================
RegressionResult planBackward(const std::vector<SkillStep>& library, const krs::world::WorldState& start,
                              const std::vector<Predicate>& goal, bool autoInsertExcitation,
                              int maxIterations) {
    RegressionResult out;
    if (goal.empty()) { out.why = "empty goal"; return out; }

    // ---- regression over the symbolic overlay (same fact machinery as the forward search) ----
    std::vector<Predicate> goals = goal;         // open subgoals (regressed backward)
    std::vector<int> chosen;                     // library indices, in REVERSE execution order
    std::set<std::string> seenGoalSigs;
    auto goalsSig = [&goals]() {
        std::string s;
        for (const auto& g : goals) { s += g.text(); s += ';'; }
        return s;
    };
    auto establishes = [](const SkillSpec* sp, const Predicate& g) {
        for (const auto& e : sp->eff)
            if (factKey(e) == factKey(g) && e.negated == g.negated) return true;
        return false;
    };
    for (out.iterations = 0; out.iterations < maxIterations; ++out.iterations) {
        // find an open subgoal not already true in the start world
        int unmetIdx = -1;
        for (int i = 0; i < int(goals.size()); ++i)
            if (!goals[i].eval(start)) { unmetIdx = i; break; }
        if (unmetIdx < 0) break;                                  // everything grounds in the start state
        const Predicate g = goals[unmetIdx];
        int pick = -1;
        for (int i = 0; i < int(library.size()); ++i)
            if (library[i].spec && library[i].spec->honesParam.empty() && establishes(library[i].spec, g)) { pick = i; break; }
        if (pick < 0) { out.why = "no skill establishes " + g.text(); return out; }
        chosen.push_back(pick);
        // regress: remove every subgoal this skill's effects establish, adopt its preconditions.
        std::vector<Predicate> next;
        for (const auto& og : goals) if (!establishes(library[pick].spec, og)) next.push_back(og);
        for (const auto& p : library[pick].spec->pre) next.push_back(p);
        goals = std::move(next);
        const std::string sig = goalsSig();
        if (!seenGoalSigs.insert(sig).second) { out.why = "regression cycle on " + g.text(); return out; }
    }
    if (out.iterations >= maxIterations) { out.why = "iteration cap reached"; return out; }

    // chosen is reverse execution order -> flip, stamp envelopes, collect knowledge gaps.
    for (auto it = chosen.rbegin(); it != chosen.rend(); ++it) {
        SkillStep step = library[*it];
        step.envelope = composeEnvelope(*step.spec, start, step.object);
        out.steps.push_back(std::move(step));
    }
    for (const auto& step : out.steps) {
        for (const auto& need : step.spec->needs) {
            if (start.needSatisfied(step.object, need.param, need.maxSigma)) continue;
            KnowledgeGap gap;
            gap.object = step.object; gap.param = need.param; gap.sigmaNeeded = need.maxSigma;
            if (const auto* k = start.knowledgeOf(step.object))
                if (auto pit = k->params.find(need.param); pit != k->params.end()) gap.sigmaNow = pit->second.sigma;
            for (const auto& ex : library)                        // suggest a matching excitation
                if (ex.spec && ex.spec->honesParam == need.param && ex.spec->honedSigma <= need.maxSigma)
                    { gap.suggestedSkill = ex.spec->name; break; }
            // dedupe (same object+param demanded by several steps)
            bool dup = false;
            for (const auto& gPrev : out.gaps)
                if (gPrev.object == gap.object && gPrev.param == gap.param) dup = true;
            if (!dup) out.gaps.push_back(std::move(gap));
        }
    }
    // G3: auto-insert the excitation BEFORE the first step that needs the parameter.
    if (autoInsertExcitation) {
        for (auto& gap : out.gaps) {
            if (gap.suggestedSkill.empty()) continue;
            const SkillStep* exStep = nullptr;
            for (const auto& ex : library)
                if (ex.spec && ex.spec->name == gap.suggestedSkill) { exStep = &ex; break; }
            if (!exStep) continue;
            for (auto sit = out.steps.begin(); sit != out.steps.end(); ++sit) {
                bool needsIt = false;
                for (const auto& n : sit->spec->needs)
                    if (n.param == gap.param && sit->object == gap.object) needsIt = true;
                if (needsIt) {
                    SkillStep ins = *exStep;
                    ins.object = gap.object;
                    ins.envelope = composeEnvelope(*ins.spec, start, ins.object);
                    out.steps.insert(sit, std::move(ins));
                    gap.resolvedByPlan = true;
                    break;
                }
            }
        }
    }
    out.ok = true;
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

// ================================================================================================
// GATE GOALPLAN (Goal Workspace G2/G3) -- regression, knowledge gaps, envelopes, live guards.
// ================================================================================================
bool runGoalPlanGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[goalplan] GATE GOALPLAN -- regression planning + R2S knowledge gaps + trait envelopes + runtime guards\n");
    bool allOk = true;

    // ---- world: an R2S liquid glass (mass unknown), a SimOnly block, an irrelevant R2S decoy ----
    Scene scene; auto& reg = scene.getRegistry();
    krs::world::WorldState& ws = krs::world::worldState(reg);
    krs::twin::PropertyCatalog cat;
    auto pubPose = [&cat](std::uint32_t id, const char* n, double x, double w, double z, double t) {
        double p[3] = { x, 0.5, 0.0 };  cat.publish(id, n, "position", krs::twin::PropType::Vec3, p, t);
        double q[4] = { w, 0.0, 0.0, z }; cat.publish(id, n, "orientation", krs::twin::PropType::Quat, q, t);
    };
    pubPose(60, "glass", 0.6, 1.0, 0.0, 0.1);   // upright
    pubPose(61, "block", 0.8, 1.0, 0.0, 0.1);
    pubPose(62, "decoy", 2.0, 1.0, 0.0, 0.1);
    ws.updateFromCatalog(cat);
    ws.setFrame("drop_pose", glm::vec3(1.2f, 0.4f, 0.3f));
    ws.setGripperOpen(0, true);
    ws.know("glass").tag = krs::world::LearnTag::R2S;
    ws.know("glass").traits = { "liquid-container" };
    ws.know("block").tag = krs::world::LearnTag::SimOnly;
    ws.know("decoy").tag = krs::world::LearnTag::R2S;            // unknown mass, but NOT in the plan

    // ---- the grounded library: grasp(obj) + place(obj) per object, and the lift_weigh excitation ----
    auto mkGrasp = [](const std::string& obj) {
        SkillSpec s;
        s.name = "grasp[" + obj + "]";
        s.pre = { { Predicate::Kind::GripperOpen, false, 0 }, { Predicate::Kind::Fresh, false, 0, obj, "", 10.0 } };
        s.eff = { { Predicate::Kind::Holding, false, 0, obj }, { Predicate::Kind::GripperOpen, true, 0 } };
        s.needs = { { "mass", 0.05 } };
        s.intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };        // the primitive's own loose bound
        s.timeoutSec = 0.5;
        return s;
    };
    auto mkPlace = [](const std::string& obj) {
        SkillSpec s;
        s.name = "place[" + obj + "]";
        s.pre = { { Predicate::Kind::Holding, false, 0, obj } };
        s.eff = { { Predicate::Kind::Holding, true, 0, obj }, { Predicate::Kind::GripperOpen, false, 0 },
                  { Predicate::Kind::At, false, 0, obj, "drop_pose", 0.1 } };
        s.needs = { { "mass", 0.05 } };
        s.intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };
        s.timeoutSec = 0.5;
        return s;
    };
    auto idle = [](Scene*, const ParamMap&) {                    // planning-only bodies (never run here)
        return [](double, Blackboard&) { return Status::Success; };
    };
    SkillSpec graspGlass = mkGrasp("glass"); graspGlass.realize = idle;
    SkillSpec placeGlass = mkPlace("glass"); placeGlass.realize = idle;
    SkillSpec graspBlock = mkGrasp("block"); graspBlock.realize = idle;
    SkillSpec placeBlock = mkPlace("block"); placeBlock.realize = idle;
    SkillSpec liftWeigh;
    liftWeigh.name = "lift_weigh";
    liftWeigh.honesParam = "mass"; liftWeigh.honedSigma = 0.02;
    liftWeigh.realize = idle; liftWeigh.timeoutSec = 0.5;

    std::vector<SkillStep> lib = {
        { &graspGlass, {}, "glass" }, { &placeGlass, {}, "glass" },
        { &graspBlock, {}, "block" }, { &placeBlock, {}, "block" },
        { &liftWeigh,  {}, ""      },
    };

    // ---- (1) regression order + gap for the R2S glass; relevance excludes the decoy ----
    {
        const std::vector<Predicate> goal = { { Predicate::Kind::At, false, 0, "glass", "drop_pose", 0.1 } };
        const RegressionResult rr = planBackward(lib, ws, goal, /*autoInsert*/false);
        const bool orderOk = rr.ok && rr.steps.size() == 2
            && rr.steps[0].spec->name == "grasp[glass]" && rr.steps[1].spec->name == "place[glass]";
        bool gapOk = rr.gaps.size() == 1 && rr.gaps[0].object == "glass" && rr.gaps[0].param == "mass"
            && rr.gaps[0].suggestedSkill == "lift_weigh" && !rr.gaps[0].resolvedByPlan;
        bool decoyClean = true;
        for (const auto& g : rr.gaps) if (g.object == "decoy") decoyClean = false;
        printf("[goalplan]   (1) regression [grasp,place]=%d ; gap(glass.mass -> lift_weigh)=%d ; decoy never demanded=%d  %s\n",
               int(orderOk), int(gapOk), int(decoyClean), (orderOk && gapOk && decoyClean) ? "PASS" : "FAIL");
        allOk = allOk && orderOk && gapOk && decoyClean;
    }

    // ---- (2) the SimOnly tag gates extraction: same plan on the block -> NO gaps ----
    {
        const std::vector<Predicate> goal = { { Predicate::Kind::At, false, 0, "block", "drop_pose", 0.1 } };
        const RegressionResult rr = planBackward(lib, ws, goal, false);
        const bool ok = rr.ok && rr.gaps.empty() && rr.steps.size() == 2;
        printf("[goalplan]   (2) SimOnly block: plan ok, ZERO gaps (best guess accepted)=%d  %s\n",
               int(ok), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- (3) auto-insert: lift_weigh splices in BEFORE the needing step; envelope intersection ----
    {
        const std::vector<Predicate> goal = { { Predicate::Kind::At, false, 0, "glass", "drop_pose", 0.1 } };
        const RegressionResult rr = planBackward(lib, ws, goal, /*autoInsert*/true);
        const bool insOk = rr.ok && rr.steps.size() == 3
            && rr.steps[0].spec->name == "lift_weigh"
            && rr.steps[1].spec->name == "grasp[glass]" && rr.steps[2].spec->name == "place[glass]"
            && rr.gaps.size() == 1 && rr.gaps[0].resolvedByPlan;
        // envelope: liquid-container's 15 deg BEATS the primitive's 60 deg (intersection).
        const bool envOk = insOk
            && rr.steps[1].envelope.count("maxTiltDeg") && std::abs(rr.steps[1].envelope.at("maxTiltDeg") - 15.0) < 1e-9
            && rr.steps[1].envelope.count("maxAccel");
        // the block's plan keeps the loose 60 (no liquid trait).
        const RegressionResult rb = planBackward(lib, ws, { { Predicate::Kind::At, false, 0, "block", "drop_pose", 0.1 } }, false);
        const bool blockEnvOk = rb.ok && std::abs(rb.steps[0].envelope.at("maxTiltDeg") - 60.0) < 1e-9;
        printf("[goalplan]   (3) auto-insert [lift_weigh,grasp,place]=%d ; glass envelope maxTilt=15 (traits beat 60)=%d ; block keeps 60=%d  %s\n",
               int(insOk), int(envOk), int(blockEnvOk), (insOk && envOk && blockEnvOk) ? "PASS" : "FAIL");
        allOk = allOk && insOk && envOk && blockEnvOk;
    }

    // ---- (4) the stamped envelope is a LIVE guard: a mid-run tilt past 15 deg FAILS the step ----
    {
        auto runOnce = [&](bool tiltIt) {
            // a grasp whose body needs ~15 ticks to finish; the guard watches the glass's live pose.
            SkillSpec slowGrasp = mkGrasp("glass");
            auto tick = std::make_shared<int>(0);
            slowGrasp.realize = [tick](Scene*, const ParamMap&) {
                return [tick](double, Blackboard&) { return (++(*tick) >= 15) ? Status::Success : Status::Running; };
            };
            std::vector<SkillStep> steps = { { &slowGrasp, {}, "glass" } };
            steps[0].envelope = composeEnvelope(slowGrasp, ws, "glass");
            SkillRuntime rt;
            const int id = rt.start("guarded", composeSequence(steps, &scene, ws));
            Status s = Status::Running;
            for (int k = 0; k < 60 && rt.anyRunning(); ++k) {
                if (tiltIt && k == 5) {                          // the glass tips 40 deg mid-run
                    pubPose(60, "glass", 0.6, std::cos(0.349), std::sin(0.349), 1.0 + k * 0.01);
                    ws.updateFromCatalog(cat);
                }
                rt.tick(1.0 / 60.0);
                s = rt.status(id);
            }
            return s;
        };
        const Status clean  = runOnce(false);
        const Status tilted = runOnce(true);
        // restore the upright glass for any later checks
        pubPose(60, "glass", 0.6, 1.0, 0.0, 2.0); ws.updateFromCatalog(cat);
        const bool ok = clean == Status::Success && tilted == Status::Failure;
        printf("[goalplan]   (4) live guard: upright run Success=%d ; 40-deg tilt mid-run -> Failure=%d  %s\n",
               int(clean == Status::Success), int(tilted == Status::Failure), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- NEG-CTRL: an unestablishable goal fails bounded with a reason ----
    {
        const RegressionResult rr = planBackward(lib, ws, { { Predicate::Kind::Holding, false, 1, "unobtainium" } }, false);
        const bool negOk = !rr.ok && rr.why.find("no skill establishes") != std::string::npos;
        printf("[goalplan]   NEG-CTRL unestablishable goal -> bounded fail (\"%s\")=%d  %s\n",
               rr.why.c_str(), int(negOk), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[goalplan] %s\n", allOk ? "ALL PASS (regression orders backward; R2S demands knowledge, SimOnly accepts guesses; excitation auto-splices; trait envelopes intersect + guard LIVE; relevance chain honest)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::skill
