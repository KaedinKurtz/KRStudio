// SkillRuntime.cpp -- see SkillRuntime.hpp. The live closed-loop task executor.
#include "SkillRuntime.hpp"
#include "Skill.hpp"
#include "WorldState.hpp"
#include "PropertyCatalog.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "SubgraphNode.hpp"
#include "KNode.hpp"
#include "KDoc.hpp"
#include "RobotModel.hpp"          // LiveRobot / instantiateFromGraph / drainCommandBusIntoRobots
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies
#include "Scene.hpp"
#include "components.hpp"          // ArticulationCommandComponent

#include <cstdio>
#include <cmath>

namespace krs::skill {

using krs::policy::Status;

int SkillRuntime::start(std::string name, krs::policy::NodePtr root) {
    auto t = std::make_unique<Task>();
    t->id = nextId_++;
    t->name = std::move(name);
    t->root = std::move(root);
    tasks_.push_back(std::move(t));
    return tasks_.back()->id;
}

int SkillRuntime::startSkill(std::string name, krs::policy::Action::Fn leaf, double timeoutSec) {
    return start(std::move(name),
                 std::make_unique<krs::policy::Timeout>(
                     std::make_unique<krs::policy::Action>(std::move(leaf)), timeoutSec));
}

void SkillRuntime::tick(double dt) {
    for (auto& t : tasks_) {
        if (!t->active) continue;
        t->last = t->root->tick(dt, t->bb);
        if (t->last != Status::Running) t->active = false;   // retired: stops re-asserting -> DOFs release
    }
}

Status SkillRuntime::status(int id) const {
    for (const auto& t : tasks_) if (t->id == id) return t->last;
    return Status::Failure;
}
bool SkillRuntime::anyRunning() const {
    for (const auto& t : tasks_) if (t->active) return true;
    return false;
}
int SkillRuntime::runningCount() const {
    int n = 0; for (const auto& t : tasks_) if (t->active) ++n; return n;
}
void SkillRuntime::cancel(int id) {
    for (auto& t : tasks_)
        if (t->id == id && t->active) { t->active = false; t->last = Status::Failure; }
}

// entt's ctx storage wants copyable payloads; the runtime owns unique_ptr tasks, so it rides in a
// shared_ptr holder (the holder copies; the runtime instance is stable).
namespace { struct SkillRuntimeHolder { std::shared_ptr<SkillRuntime> p = std::make_shared<SkillRuntime>(); }; }
SkillRuntime& skillRuntime(entt::registry& reg) {
    auto* h = reg.ctx().find<SkillRuntimeHolder>();
    if (!h) h = &reg.ctx().emplace<SkillRuntimeHolder>();
    return *h->p;
}

// ================================================================================================
// GATE SKILLRUNTIME (Process & Skills P3) -- closed loop: monitor -> Failure -> retry -> Success.
// ================================================================================================
bool runSkillRuntimeGate() {
    using std::printf;
    using namespace krs::policy;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[skillrt] GATE SKILLRUNTIME -- live-style pump: WorldState-gated postcondition, honest Timeout, retry -> Success\n");
    auto& F = NodeFactory::instance();
    bool allOk = true;

    // ---- live demo robot + bus + world ----
    Scene scene; auto& reg = scene.getRegistry();
    krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
    g.robotId = 0;
    krs::rbuild::spawnGraphBodies(scene, g, 0);
    krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, 0);
    if (!lr) { printf("[skillrt] FAIL: no live robot\n"); return false; }
    const int n = lr->ndof();
    auto* bus = &reg.ctx().emplace<ArticulationCommandComponent>();
    krs::world::WorldState& ws = krs::world::worldState(reg);
    krs::twin::PropertyCatalog cat;                      // the gate's own observation stream
    ws.setFrame("bin_A", glm::vec3(1.0f, 0.0f, 0.0f));

    // the skill body: the same move_to_config subgraph as P1 (interior physics_config_drive).
    krs::knode::KNodeDoc doc;
    doc.id = "rt-move"; doc.name = "Move"; doc.category = "Skills";
    {
        auto cfgDrive = F.createNode("physics_config_drive");
        cfgDrive->setPortLiteral<int>("Robot", 0);
        krs::knode::InteriorNode m; m.id = "d"; m.typeId = "physics_config_drive";
        m.state = krs::kdoc::nodeToJson(*cfgDrive, "physics_config_drive");
        doc.nodes.push_back(m);
    }
    { krs::knode::ExposedPort p; p.name = "target"; p.interiorNode = "d"; p.interiorPort = "Config";
      p.isInput = true; p.dataType = "joint_config"; p.role = "targetConfig"; doc.ports.push_back(p); }

    Eigen::VectorXd target(n);
    for (int i = 0; i < n; ++i) target[i] = 0.1 + 0.03 * i;

    // The CLOSED-LOOP postcondition: robot at target AND the world says the cup reached bin_A.
    // The "world" (this gate) only moves the cup there after some time -- so early attempts
    // honestly TIME OUT and the retry loop is what converges.
    auto post = [lr, target, &ws]() {
        const bool qOk = (lr->q - target).cwiseAbs().maxCoeff() < 1e-4;
        return qOk && ws.at("cup", "bin_A", 0.1);
    };

    // one runtime pump under the REAL per-pass lifecycle, with the world advancing per tick.
    const double dt = 1.0 / 60.0;
    auto pump = [&](SkillRuntime& rt, int worldArrivesAtTick, int maxTicks) {
        int k = 0;
        for (; k < maxTicks && rt.anyRunning(); ++k) {
            // the world: the cup starts far away and "arrives" at bin_A at the given tick.
            const double cupX = (k >= worldArrivesAtTick) ? 1.0 : 5.0;
            double p[3] = { cupX, 0.0, 0.0 };
            cat.publish(20, "cup", "position", krs::twin::PropType::Vec3, p, k * dt);
            ws.updateFromCatalog(cat);
            // the pass: clear -> tick (skills re-assert) -> drain (commands land in q)
            bus->clearForEvalPass();
            rt.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
        }
        return k;
    };

    // ---- (1) closed loop: short attempts time out until the world catches up; retry converges ----
    {
        SkillRuntime rt;
        auto body = std::make_shared<krs::nodes::SubgraphNode>(doc);
        // task = Retry( Timeout( skill, 0.1s ) ): each attempt gets 0.1s; the cup arrives at tick 30
        // (~0.5s), so the first ~5 attempts MUST fail before one can succeed.
        auto leaf = makeSkillLeaf(body, &scene, { { "target", ParamValue::cfg(target) } }, post);
        const int id = rt.start("move+wait-for-world",
            std::make_unique<RetryUntilSuccess>(
                std::make_unique<Timeout>(std::make_unique<Action>(leaf), 0.1), /*maxAttempts*/ 0));
        const int ticks = pump(rt, /*worldArrivesAtTick*/30, /*maxTicks*/600);
        const double err = (lr->q - target).cwiseAbs().maxCoeff();
        const bool ok = rt.status(id) == Status::Success && ticks > 30 && err < 1e-4;
        printf("[skillrt]   (1) closed loop: Success=%d after %d ticks (>30 => retries really happened), |q-t|=%.2e  %s\n",
               int(rt.status(id) == Status::Success), ticks, err, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- (2) cancel releases the DOFs next pass ----
    {
        SkillRuntime rt;
        auto body = std::make_shared<krs::nodes::SubgraphNode>(doc);
        auto never = []() { return false; };
        const int id = rt.startSkill("never", makeSkillLeaf(body, &scene, { { "target", ParamValue::cfg(target) } }, never), 1e9);
        bus->clearForEvalPass(); rt.tick(dt);
        const bool asserting = !bus->entries.empty();
        rt.cancel(id);
        bus->clearForEvalPass(); rt.tick(dt);
        const bool released = bus->entries.empty() && rt.status(id) == Status::Failure;
        printf("[skillrt]   (2) cancel: was-asserting=%d ; post-cancel bus empty + status Failure=%d  %s\n",
               int(asserting), int(released), (asserting && released) ? "PASS" : "FAIL");
        allOk = allOk && (asserting && released);
    }

    // ---- NEG-CTRL: retry capped at 1 attempt -> the SAME task honestly FAILS (recovery was the retry) ----
    {
        SkillRuntime rt;
        auto body = std::make_shared<krs::nodes::SubgraphNode>(doc);
        // reset the world: cup far away again, arriving at tick 30 as before.
        auto leaf = makeSkillLeaf(body, &scene, { { "target", ParamValue::cfg(target) } }, post);
        const int id = rt.start("capped",
            std::make_unique<RetryUntilSuccess>(
                std::make_unique<Timeout>(std::make_unique<Action>(leaf), 0.1), /*maxAttempts*/ 1));
        pump(rt, /*worldArrivesAtTick*/30, /*maxTicks*/600);
        const bool negOk = rt.status(id) == Status::Failure;
        printf("[skillrt]   NEG-CTRL retry capped at 1 -> Failure=%d  %s\n",
               int(negOk), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[skillrt] %s\n", allOk ? "ALL PASS (runtime pumps tasks under the per-pass lifecycle; WorldState-gated monitor times out honestly; retry converges once the world allows; cancel releases; capped retry fails honestly)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::skill
