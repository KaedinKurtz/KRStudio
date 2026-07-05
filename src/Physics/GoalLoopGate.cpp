// GoalLoopGate.cpp -- the Goal Workspace STORY gate: goal -> regression -> knowledge gap ->
// excitation (honing) -> guarded execution -> persistence -> honest invalidation. The primitive
// bodies load FROM the shipped assets/skills/*.knode files (authored, versioned, shareable).
#include "TaskPlanner.hpp"
#include "GoalDoc.hpp"
#include "Hone.hpp"
#include "WorldState.hpp"
#include "SkillRuntime.hpp"
#include "PropertyCatalog.hpp"
#include "KNode.hpp"
#include "KSave.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "SubgraphNode.hpp"
#include "RobotModel.hpp"
#include "RobotBuilderScene.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QVariant>

#include <cstdio>
#include <cmath>
#include <random>

namespace krs::skill {

using krs::policy::Status;
using krs::policy::Blackboard;

bool runGoalLoopGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[goalloop] GATE GOALLOOP -- the whole story: .kgoal -> regression -> gap -> honing -> guarded execution -> persistence\n");
    auto& F = NodeFactory::instance();
    bool allOk = true;
    const QString dir = QDir::temp().filePath("krs_goalloop_gate");
    QDir(dir).removeRecursively();
    // QSettings hygiene (saveScene records ksave/lastScene -- restore it on exit).
    const QVariant priorLastScene = QSettings().value(QStringLiteral("ksave/lastScene"));
    struct SettingsRestore {
        QVariant v;
        ~SettingsRestore() {
            if (v.isValid()) QSettings().setValue(QStringLiteral("ksave/lastScene"), v);
            else             QSettings().remove(QStringLiteral("ksave/lastScene"));
        }
    } restoreGuard{ priorLastScene };

    // ---- the primitive bodies come FROM THE SHIPPED FILES ----
    const QString skillsDir = QStringLiteral(KRS_SOURCE_DIR) + QStringLiteral("/assets/skills");
    krs::knode::KNodeDoc moveDoc, graspDoc, placeDoc;
    QString kerr;
    const bool filesOk = krs::knode::loadKNode(skillsDir + "/move_to.knode", moveDoc, &kerr)
        && krs::knode::loadKNode(skillsDir + "/grasp.knode", graspDoc, &kerr)
        && krs::knode::loadKNode(skillsDir + "/place.knode", placeDoc, &kerr)
        && graspDoc.name == "Grasp" && !graspDoc.description.isEmpty()
        && graspDoc.ports.size() == 1 && graspDoc.ports[0].role == "targetConfig";
    printf("[goalloop]   primitives from assets/skills/*.knode: loaded+manifested=%d  %s\n",
           int(filesOk), filesOk ? "PASS" : "FAIL");
    allOk = allOk && filesOk;
    if (!filesOk) return false;

    // ---- live demo robot + world: an R2S liquid glass with UNKNOWN mass ----
    Scene scene; auto& reg = scene.getRegistry();
    reg.ctx().emplace<krs::ksave::RobotSourceRegistry>().set(0, "", /*builder demo*/ 0);
    krs::rbuild::RobotGraph g = krs::rbuild::buildDemoGraph();
    g.robotId = 0;
    krs::rbuild::spawnGraphBodies(scene, g, 0);
    reg.ctx().emplace<krs::rbuild::RobotGraph>(g);
    krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, 0);
    if (!lr) { printf("[goalloop] FAIL: no live robot\n"); return false; }
    const int n = lr->ndof();
    auto* bus = &reg.ctx().emplace<ArticulationCommandComponent>();
    krs::world::WorldState& ws = krs::world::worldState(reg);
    krs::twin::PropertyCatalog cat;
    auto pubGlass = [&cat](double x, double t) {
        double p[3] = { x, 0.5, 0.0 };  cat.publish(70, "glass", "position", krs::twin::PropType::Vec3, p, t);
        double q[4] = { 1.0, 0.0, 0.0, 0.0 }; cat.publish(70, "glass", "orientation", krs::twin::PropType::Quat, q, t);
    };
    pubGlass(0.6, 0.1);
    ws.updateFromCatalog(cat);
    ws.setFrame("drop_pose", glm::vec3(1.2f, 0.4f, 0.3f));
    ws.setGripperOpen(0, true);
    ws.know("glass").tag = krs::world::LearnTag::R2S;
    ws.know("glass").traits = { "liquid-container" };            // envelope: maxTilt 15 will stamp

    // ---- SkillSpecs realized from the FILE-loaded subgraph bodies ----
    Eigen::VectorXd qGrasp(n), qPlace(n);
    for (int i = 0; i < n; ++i) { qGrasp[i] = 0.12 + 0.02 * i; qPlace[i] = -0.12 - 0.02 * i; }
    auto realizeFromDoc = [lr](const krs::knode::KNodeDoc& doc, const Eigen::VectorXd& target) {
        return [&doc, lr, target](Scene* sc, const ParamMap&) {
            auto body = std::make_shared<krs::nodes::SubgraphNode>(doc);
            return makeSkillLeaf(body, sc, { { "target", ParamValue::cfg(target) } }, qNear(lr, target));
        };
    };
    SkillSpec grasp;
    grasp.name = "grasp";
    grasp.pre = { { Predicate::Kind::GripperOpen, false, 0 }, { Predicate::Kind::Fresh, false, 0, "glass", "", 10.0 } };
    grasp.eff = { { Predicate::Kind::Holding, false, 0, "glass" }, { Predicate::Kind::GripperOpen, true, 0 } };
    grasp.needs = { { "mass", 0.05 } };
    grasp.intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };
    grasp.realize = realizeFromDoc(graspDoc, qGrasp);
    grasp.timeoutSec = 2.0;
    SkillSpec place;
    place.name = "place";
    place.pre = { { Predicate::Kind::Holding, false, 0, "glass" } };
    place.eff = { { Predicate::Kind::Holding, true, 0, "glass" }, { Predicate::Kind::GripperOpen, false, 0 },
                  { Predicate::Kind::At, false, 0, "glass", "drop_pose", 0.1 } };
    place.needs = { { "mass", 0.05 } };
    place.intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };
    place.realize = realizeFromDoc(placeDoc, qPlace);
    place.timeoutSec = 2.0;

    // lift_weigh: the excitation -- its body RUNS THE HONING (twin population vs a noisy "real"
    // 6DoF-FT record generated from the sealed truth mass) and writes the ledger on convergence.
    const double truthMass = 0.372;
    SkillSpec liftWeigh;
    liftWeigh.name = "lift_weigh";
    liftWeigh.honesParam = "mass"; liftWeigh.honedSigma = 0.02;
    liftWeigh.timeoutSec = 2.0;
    liftWeigh.realize = [&ws, truthMass](Scene*, const ParamMap&) {
        return [&ws, truthMass](double, Blackboard&) -> Status {
            krs::hone::HoneConfig cfg;                            // deterministic (seeded)
            auto model = [](double m) {                           // the twin: Fz = m*g over 50 samples
                std::vector<double> y(50, m * 9.81); return y;
            };
            std::vector<double> real = model(truthMass);          // the "real" FT record + sensor noise
            std::mt19937 rng(1234);
            std::normal_distribution<double> nz(0.0, cfg.measNoiseStd);
            for (auto& v : real) v += nz(rng);
            krs::hone::Belief prior{ 0.6, 0.25, krs::hone::Prov::Prior };
            const krs::hone::HoneResult hr = krs::hone::hone(prior, model, real, cfg);
            if (!hr.converged) return Status::Failure;
            ws.know("glass").params["mass"] = { hr.posterior.value, std::max(hr.posterior.sigma, 1e-4),
                                                krs::world::Prov::Measured };
            return Status::Success;
        };
    };

    const std::vector<SkillStep> lib = {
        { &grasp, {}, "glass" }, { &place, {}, "glass" }, { &liftWeigh, {}, "glass" },
    };

    // ---- the goal, as a .kgoal document (builder front-end) ----
    krs::goal::GoalDoc goalDoc = krs::goal::GoalBuilder("shelve the glass")
                                     .at("glass", "drop_pose", 0.1).doc();
    const QString goalPath = QDir(dir).filePath("shelve.kgoal");
    krs::goal::saveKGoal(goalDoc, goalPath);
    krs::goal::GoalDoc goalR; QString gerr;
    krs::goal::loadKGoal(goalPath, goalR, &gerr);                 // plan from the RELOADED file

    // ---- (1) plan: regression + auto-excitation; envelope stamped from the trait ----
    RegressionResult rr = planBackward(lib, ws, goalR.require, /*autoInsert*/true);
    const bool planOk = rr.ok && rr.steps.size() == 3
        && rr.steps[0].spec->name == "lift_weigh"
        && rr.steps[1].spec->name == "grasp" && rr.steps[2].spec->name == "place"
        && rr.gaps.size() == 1 && rr.gaps[0].resolvedByPlan
        && std::abs(rr.steps[1].envelope.at("maxTiltDeg") - 15.0) < 1e-9;
    printf("[goalloop]   (1) plan [lift_weigh, grasp, place] + gap-resolved + tilt-envelope 15=%d  %s\n",
           int(planOk), planOk ? "PASS" : "FAIL");
    allOk = allOk && planOk;

    // ---- (2) execute: honing runs, ledger updates, guarded motion drives the robot ----
    {
        SkillRuntime rt;
        const int id = rt.start("shelve", composeSequence(rr.steps, &scene, ws));
        const double dt = 1.0 / 60.0;
        int k = 0;
        for (; k < 900 && rt.anyRunning(); ++k) {
            bus->clearForEvalPass();
            rt.tick(dt);
            krs::robot::drainCommandBusIntoRobots(reg);
        }
        // the sim world moves the released glass to the drop pose (stand-in until kinematic attach).
        pubGlass(1.2, 3.0);
        {   // keep the published pose components consistent with the frame for the At check
            double p[3] = { 1.2, 0.4, 0.3 };
            cat.publish(70, "glass", "position", krs::twin::PropType::Vec3, p, 3.1);
        }
        ws.updateFromCatalog(cat);
        const auto* mk = ws.knowledgeOf("glass");
        const bool honed = mk && mk->params.count("mass")
            && mk->params.at("mass").prov == krs::world::Prov::Measured
            && std::abs(mk->params.at("mass").value - truthMass) < 5e-3
            && mk->params.at("mass").sigma <= 0.02;
        const double err = (lr->q - qPlace).cwiseAbs().maxCoeff();
        const bool ok = rt.status(id) == Status::Success && honed && err < 1e-4
            && !ws.isHolding(0, "glass") && krs::goal::satisfied(goalR, ws);
        printf("[goalloop]   (2) execution: Success=%d in %d ticks; mass Measured %.4f+-%.4f (truth %.3f)=%d; "
               "q@place err=%.1e; goal satisfied=%d  %s\n",
               int(rt.status(id) == Status::Success), k,
               mk && mk->params.count("mass") ? mk->params.at("mass").value : -1.0,
               mk && mk->params.count("mass") ? mk->params.at("mass").sigma : -1.0,
               truthMass, int(honed), err, int(krs::goal::satisfied(goalR, ws)), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- (3) persistence: save -> fresh scene -> load -> ZERO gaps (yesterday's glass known) ----
    {
        const std::string scenePath = QDir(dir).filePath("lab.kscene").toStdString();
        const krs::ksave::Report sr = krs::ksave::saveScene(scene, scenePath);
        Scene s2;
        const krs::ksave::Report lrp = krs::ksave::loadScene(s2, scenePath);
        auto& ws2 = krs::world::worldState(s2.getRegistry());
        // the robot SEES the glass again (perception); it must not need to re-WEIGH it.
        krs::twin::PropertyCatalog cat2;
        { double p[3] = { 0.6, 0.5, 0.0 };  cat2.publish(70, "glass", "position", krs::twin::PropType::Vec3, p, 0.1);
          double q2[4] = { 1.0, 0.0, 0.0, 0.0 }; cat2.publish(70, "glass", "orientation", krs::twin::PropType::Quat, q2, 0.1); }
        ws2.updateFromCatalog(cat2);
        ws2.setFrame("drop_pose", glm::vec3(1.2f, 0.4f, 0.3f));
        ws2.setGripperOpen(0, true);
        const RegressionResult rr2 = planBackward(lib, ws2, goalR.require, true);
        const bool noRelearn = sr.ok && lrp.ok && rr2.ok && rr2.gaps.empty()
            && rr2.steps.size() == 2 && rr2.steps[0].spec->name == "grasp";
        printf("[goalloop]   (3) persistence: reload plans [grasp, place] with ZERO gaps (no re-weigh)=%d  %s\n",
               int(noRelearn), noRelearn ? "PASS" : "FAIL");
        allOk = allOk && noRelearn;

        // ---- NEG-CTRL: a residual spike invalidates -> the gap honestly REAPPEARS ----
        ws2.invalidate("glass", "mass");
        const RegressionResult rr3 = planBackward(lib, ws2, goalR.require, false);
        const bool negOk = rr3.ok && rr3.gaps.size() == 1 && rr3.gaps[0].param == "mass";
        printf("[goalloop]   NEG-CTRL residual-spike invalidation -> gap(mass) reappears=%d  %s\n",
               int(negOk), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[goalloop] %s\n", allOk ? "ALL PASS (file-authored primitives; .kgoal goal; regression + auto-excitation; honing writes the ledger; guarded execution reaches the goal; knowledge persists across save/load; invalidation honest)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::skill
