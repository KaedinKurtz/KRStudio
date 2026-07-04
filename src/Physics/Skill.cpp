// Skill.cpp -- see Skill.hpp. The skill contract: body + bound params + postcondition, actuating
// through the robot-keyed command bus under the per-pass re-assert lifecycle. P4 adds the typed
// Predicate pre/effects + the Sequence composer with static validation.
#include "Skill.hpp"
#include "WorldState.hpp"
#include "SkillRuntime.hpp"
#include "PropertyCatalog.hpp"     // gate: seed the WorldState through a real catalog stream
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "SubgraphNode.hpp"
#include "KNode.hpp"
#include "KDoc.hpp"
#include "KPack.hpp"
#include "KParts.hpp"
#include "RobotModel.hpp"          // LiveRobot / instantiateFromGraph / drainCommandBusIntoRobots
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies
#include "Scene.hpp"
#include "components.hpp"          // ArticulationCommandComponent

#include <QDir>
#include <QFile>

#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::skill {

using krs::policy::Status;
using krs::policy::Blackboard;

krs::policy::Action::Fn makeSkillLeaf(std::shared_ptr<Node> body, Scene* scene,
                                      ParamMap params, std::function<bool()> post) {
    auto bound = std::make_shared<bool>(false);   // shared: the functor is re-enterable across ticks
    return [body, scene, params = std::move(params), post = std::move(post), bound]
           (double /*dt*/, Blackboard& /*bb*/) -> Status {
        if (!body) return Status::Failure;
        if (!*bound) {
            body->setScene(scene);                              // propagates into subgraph interiors (P0)
            for (const auto& [name, v] : params) {
                if (v.kind == ParamValue::Kind::Number) {
                    body->setPortLiteral<double>(name, v.number);
                } else {
                    PortDataPacket pk; pk.data = v.config; pk.type = { "joint_config", "handle" };
                    body->setInput(name, pk);
                }
            }
            *bound = true;
        }
        body->process();                                        // re-asserts the bus entries THIS pass
        if (post && post()) return Status::Success;
        return Status::Running;                                 // Timeout decorator turns this into Failure
    };
}

std::function<bool()> qNear(const krs::robot::LiveRobot* lr, const Eigen::VectorXd& target, double tol) {
    return [lr, target, tol]() {
        if (!lr || lr->q.size() != target.size()) return false;
        return (lr->q - target).cwiseAbs().maxCoeff() < tol;
    };
}

// ================================================================================================
// P4: typed predicates + the Sequence composer
// ================================================================================================
bool Predicate::eval(const krs::world::WorldState& ws) const {
    bool v = false;
    switch (kind) {
        case Kind::GripperOpen: v = ws.gripperOpen(robotId); break;
        case Kind::Holding:     v = ws.isHolding(robotId, a); break;
        case Kind::At:          v = ws.at(a, b, tol); break;
        case Kind::Near:        v = ws.near(a, b, tol); break;
        case Kind::Fresh:       v = ws.fresh(a, tol); break;
    }
    return negated ? !v : v;
}
void Predicate::apply(krs::world::WorldState& ws) const {
    switch (kind) {
        case Kind::GripperOpen: ws.setGripperOpen(robotId, !negated); break;
        case Kind::Holding:     ws.setHolding(robotId, negated ? std::string() : a); break;
        case Kind::At: case Kind::Near:
            // asserting a spatial fact: pin the object's pose onto the frame (the symbolic effect a
            // "place" establishes; perception later overwrites it with the observed truth).
            if (!negated) if (const auto* f = ws.frame(b)) ws.setFrame(a + "@" + b, f->pos, f->rot);
            break;
        case Kind::Fresh: break;   // freshness is observed, never asserted
    }
}
std::string Predicate::text() const {
    std::string s = negated ? "!" : "";
    switch (kind) {
        case Kind::GripperOpen: return s + "gripperOpen(r" + std::to_string(robotId) + ")";
        case Kind::Holding:     return s + "holding(r" + std::to_string(robotId) + ", " + a + ")";
        case Kind::At:          return s + "at(" + a + ", " + b + ")";
        case Kind::Near:        return s + "near(" + a + ", " + b + ")";
        case Kind::Fresh:       return s + "fresh(" + a + ")";
    }
    return s + "?";
}

ComposeReport validateSequence(const std::vector<SkillStep>& steps, const krs::world::WorldState& start) {
    krs::world::WorldState sim = start;             // simulate effects over a copy
    for (size_t i = 0; i < steps.size(); ++i) {
        const SkillSpec* sp = steps[i].spec;
        if (!sp) return { false, "step " + std::to_string(i) + " has no spec" };
        for (const auto& p : sp->pre)
            if (!p.eval(sim))
                return { false, "step " + std::to_string(i) + " (" + sp->name + "): precondition "
                                + p.text() + " is not established" };
        for (const auto& e : sp->eff) e.apply(sim);
    }
    return { true, "" };
}

krs::policy::NodePtr composeSequence(const std::vector<SkillStep>& steps, Scene* scene,
                                     krs::world::WorldState& ws) {
    using namespace krs::policy;
    auto seq = std::make_unique<Sequence>();
    for (const auto& step : steps) {
        const SkillSpec* sp = step.spec;
        if (!sp) continue;
        // 1. the LIVE precondition guard (validation is static; the world can still diverge).
        auto pre = sp->pre;
        seq->add(std::make_unique<Condition>([pre, &ws](Blackboard&) {
            for (const auto& p : pre) if (!p.eval(ws)) return false;
            return true;
        }));
        // 2. the skill body under its timeout.
        seq->add(std::make_unique<Timeout>(
            std::make_unique<Action>(sp->realize(scene, step.params)), sp->timeoutSec));
        // 3. effects onto the LIVE world on this step's success.
        auto eff = sp->eff;
        seq->add(std::make_unique<Action>([eff, &ws](double, Blackboard&) {
            for (const auto& e : eff) e.apply(ws);
            return Status::Success;
        }));
    }
    return seq;
}

// ================================================================================================
// GATE SKILL (Process & Skills P1)
// ================================================================================================
bool runSkillGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[skill] GATE SKILL -- a parameterized, shareable skill drives the keyed bus to a goal under the per-pass lifecycle\n");
    const QString dir = QDir::temp().filePath("krs_skill_gate");
    QDir(dir).removeRecursively();
    auto& F = NodeFactory::instance();
    bool allOk = true;

    // ---- a live demo robot (the same spawn path the app + KSAVE gate use) ----
    Scene scene; auto& reg = scene.getRegistry();
    krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
    g.robotId = 0;
    krs::rbuild::spawnGraphBodies(scene, g, 0);
    krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, 0);
    if (!lr) { printf("[skill] FAIL: no live robot\n"); return false; }
    const int n = lr->ndof();

    // ---- author the skill as a .knode WITH a manifest + a role-tagged parameter ----
    krs::knode::KNodeDoc doc;
    doc.id = "skill-move-cfg"; doc.name = "Move To Config"; doc.category = "Skills";
    doc.description = "Drive one robot's whole joint config to a target (keyed bus).";
    doc.version = "1.0.0"; doc.author = "gate"; doc.tags = { "motion", "skill" };
    {
        auto cfgDrive = F.createNode("physics_config_drive");
        if (!cfgDrive) { printf("[skill] FAIL: physics_config_drive missing\n"); return false; }
        cfgDrive->setPortLiteral<int>("Robot", 0);
        krs::knode::InteriorNode m; m.id = "d"; m.typeId = "physics_config_drive";
        m.state = krs::kdoc::nodeToJson(*cfgDrive, "physics_config_drive");
        doc.nodes.push_back(m);
    }
    { krs::knode::ExposedPort p; p.name = "target"; p.interiorNode = "d"; p.interiorPort = "Config";
      p.isInput = true; p.dataType = "joint_config"; p.role = "targetConfig"; doc.ports.push_back(p); }

    // manifest + param metadata round-trip the file; a .knodepack now carries the description.
    const QString knodePath = QDir(dir).filePath("move_to_config.knode");
    krs::knode::saveKNode(doc, knodePath);
    krs::knode::KNodeDoc r; QString kerr;
    const bool loaded = krs::knode::loadKNode(knodePath, r, &kerr);
    const bool manifestOk = loaded && r.description == doc.description && r.version == "1.0.0"
        && r.tags.size() == 2 && r.ports.size() == 1 && r.ports[0].role == "targetConfig";
    bool packDescOk = false;
    {
        krs::parts::PartLibrary lib; lib.setSearchRoots({ dir }); lib.rescan();
        const QString packPath = QDir(dir).filePath("skill.knodepack");
        QString perr;
        krs::kpack::writePack(knodePath, lib, packPath, &perr);
        krs::kpack::PackDoc pd; QString lerr;
        packDescOk = krs::kpack::loadPack(packPath, pd, &lerr) && !pd.entries.empty()
            && pd.entries[0].description == doc.description;
    }
    printf("[skill]   manifest: .knode round-trip(desc/ver/tags/param-role)=%d ; .knodepack carries description=%d  %s\n",
           int(manifestOk), int(packDescOk), (manifestOk && packDescOk) ? "PASS" : "FAIL");
    allOk = allOk && manifestOk && packDescOk;

    // ---- instantiate + run the skill under the REAL per-pass bus lifecycle ----
    Eigen::VectorXd target(n);
    for (int i = 0; i < n; ++i) target[i] = 0.15 + 0.05 * i;    // well within demo limits
    auto body = std::make_shared<krs::nodes::SubgraphNode>(r);   // from the RELOADED doc (proves the file)
    auto* bus = &reg.ctx().emplace<ArticulationCommandComponent>();

    Tree tree(std::make_unique<Timeout>(
        std::make_unique<Action>(makeSkillLeaf(body, &scene, { { "target", ParamValue::cfg(target) } },
                                               qNear(lr, target))),
        2.0));
    const double dt = 1.0 / 60.0;
    Status s = Status::Running;
    int ticks = 0;
    for (; ticks < 300 && s == Status::Running; ++ticks) {
        bus->clearForEvalPass();                                 // the eval pass: clear -> tick -> drain
        s = tree.tick(dt);
        krs::robot::drainCommandBusIntoRobots(reg);
    }
    const double err = (lr->q - target).cwiseAbs().maxCoeff();
    const bool reached = (s == Status::Success) && err < 1e-4;
    printf("[skill]   move_to_config: Success=%d in %d tick(s), |q-target|max=%.2e  %s\n",
           int(s == Status::Success), ticks, err, reached ? "PASS" : "FAIL");
    allOk = allOk && reached;

    // ---- RELEASE: once the skill stops ticking, nothing re-asserts -> the DOFs release ----
    bus->clearForEvalPass();
    krs::robot::drainCommandBusIntoRobots(reg);
    const bool released = bus->entries.empty() && bus->target.empty();
    printf("[skill]   release: after the skill stops, bus stays empty next pass=%d  %s\n",
           int(released), released ? "PASS" : "FAIL");
    allOk = allOk && released;

    // ---- NEG-CTRL: a postcondition that never holds -> Timeout -> Failure (not infinite Running) ----
    {
        Eigen::VectorXd wrong(n); wrong.setConstant(99.0);       // beyond limits: q can never equal it
        auto body2 = std::make_shared<krs::nodes::SubgraphNode>(r);
        Tree t2(std::make_unique<Timeout>(
            std::make_unique<Action>(makeSkillLeaf(body2, &scene, { { "target", ParamValue::cfg(target) } },
                                                   qNear(lr, wrong))),
            0.1));
        Status s2 = Status::Running;
        int k = 0;
        for (; k < 100 && s2 == Status::Running; ++k) {
            bus->clearForEvalPass();
            s2 = t2.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
        }
        const bool negOk = (s2 == Status::Failure);
        printf("[skill]   NEG-CTRL impossible postcondition -> Timeout Failure=%d (after %d ticks)  %s\n",
               int(negOk), k, negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[skill] %s\n", allOk ? "ALL PASS (a manifest-carrying, role-tagged, shareable skill binds params, drives the robot-keyed bus to its goal under the per-pass lifecycle, releases on stop; impossible goals fail honestly)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

// ================================================================================================
// GATE PICK-PLACE (Process & Skills P4) -- composed skills execute a validated multi-step task.
// ================================================================================================
bool runPickPlaceGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[pickplace] GATE PICK-PLACE -- pick+place SkillSpecs compose, validate, and execute closed-loop in sim\n");
    auto& F = NodeFactory::instance();
    bool allOk = true;

    // ---- live demo robot + bus + world (cup on the table, a drop frame) ----
    Scene scene; auto& reg = scene.getRegistry();
    krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
    g.robotId = 0;
    krs::rbuild::spawnGraphBodies(scene, g, 0);
    krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, 0);
    if (!lr) { printf("[pickplace] FAIL: no live robot\n"); return false; }
    const int n = lr->ndof();
    auto* bus = &reg.ctx().emplace<ArticulationCommandComponent>();
    krs::world::WorldState& ws = krs::world::worldState(reg);
    {
        krs::twin::PropertyCatalog seedCat;
        double p[3] = { 0.6, 0.1, 0.0 };
        seedCat.publish(30, "cup", "position", krs::twin::PropType::Vec3, p, 0.05);
        ws.updateFromCatalog(seedCat);
    }
    ws.setFrame("drop_pose", glm::vec3(1.2f, 0.4f, 0.3f));
    ws.setGripperOpen(0, true);

    // ---- the shared move-to-config subgraph body (the hybrid contract's simplest realization) ----
    krs::knode::KNodeDoc moveDoc;
    moveDoc.id = "pp-move"; moveDoc.name = "Move"; moveDoc.category = "Skills";
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

    // ---- the two SkillSpecs: typed preconditions + effects ----
    SkillSpec pick;
    pick.name = "pick";
    pick.pre = { { Predicate::Kind::GripperOpen, false, 0 },
                 { Predicate::Kind::Fresh, false, 0, "cup", "", 10.0 } };
    pick.eff = { { Predicate::Kind::Holding, false, 0, "cup" },
                 { Predicate::Kind::GripperOpen, true, 0 } };          // negated => gripper CLOSED
    pick.realize = realizeMove(qGrasp);
    pick.timeoutSec = 2.0;

    SkillSpec place;
    place.name = "place";
    place.pre = { { Predicate::Kind::Holding, false, 0, "cup" } };
    place.eff = { { Predicate::Kind::Holding, true, 0, "cup" },       // negated => released
                  { Predicate::Kind::GripperOpen, false, 0 },
                  { Predicate::Kind::At, false, 0, "cup", "drop_pose", 0.1 } };
    place.realize = realizeMove(qPlace);
    place.timeoutSec = 2.0;

    // ---- (1) static validation: [pick, place] valid; [place, pick] rejected with WHY ----
    const std::vector<SkillStep> good = { { &pick, {} }, { &place, {} } };
    const std::vector<SkillStep> bad  = { { &place, {} }, { &pick, {} } };
    const ComposeReport vGood = validateSequence(good, ws);
    const ComposeReport vBad  = validateSequence(bad,  ws);
    const bool valOk = vGood.valid && !vBad.valid && vBad.why.find("holding") != std::string::npos;
    printf("[pickplace]   (1) validation: [pick,place] ok=%d ; [place,pick] rejected=%d (\"%s\")  %s\n",
           int(vGood.valid), int(!vBad.valid), vBad.why.c_str(), valOk ? "PASS" : "FAIL");
    allOk = allOk && valOk;

    // ---- (2) execute the validated task under the SkillRuntime (the real per-pass lifecycle) ----
    {
        SkillRuntime rt;
        const int id = rt.start("pick+place", composeSequence(good, &scene, ws));
        const double dt = 1.0 / 60.0;
        int k = 0;
        bool heldMidway = false;
        for (; k < 600 && rt.anyRunning(); ++k) {
            bus->clearForEvalPass();
            rt.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
            if (ws.isHolding(0, "cup")) heldMidway = true;   // pick's effect observed mid-task
        }
        const double err = (lr->q - qPlace).cwiseAbs().maxCoeff();
        const bool ok = rt.status(id) == Status::Success && err < 1e-4
            && heldMidway && !ws.isHolding(0, "cup") && ws.gripperOpen(0);
        printf("[pickplace]   (2) execution: Success=%d in %d ticks; q at place (err=%.2e); held-midway=%d released-at-end=%d gripper-open=%d  %s\n",
               int(rt.status(id) == Status::Success), k, err, int(heldMidway),
               int(!ws.isHolding(0, "cup")), int(ws.gripperOpen(0)), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- NEG-CTRL: gripper already closed at runtime -> the pick step's LIVE guard fails the task ----
    {
        ws.setGripperOpen(0, false);
        ws.setHolding(0, "");                                 // holding nothing, gripper jammed shut
        SkillRuntime rt;
        const int id = rt.start("pick+place-jammed", composeSequence(good, &scene, ws));
        const double dt = 1.0 / 60.0;
        for (int k = 0; k < 100 && rt.anyRunning(); ++k) {
            bus->clearForEvalPass();
            rt.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
        }
        const bool negOk = rt.status(id) == Status::Failure;
        printf("[pickplace]   NEG-CTRL gripper-jammed-closed -> runtime precondition fails the task=%d  %s\n",
               int(negOk), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[pickplace] %s\n", allOk ? "ALL PASS (typed pre/eff validate composition statically + guard it live; the composed pick+place executes closed-loop to Success with correct facts; a jammed gripper fails honestly)"
                                     : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::skill
