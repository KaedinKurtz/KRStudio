// Skill.cpp -- see Skill.hpp. The skill contract: body + bound params + postcondition, actuating
// through the robot-keyed command bus under the per-pass re-assert lifecycle.
#include "Skill.hpp"
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

} // namespace krs::skill
