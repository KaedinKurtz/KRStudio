// GoalWorkspacePanel.cpp -- see GoalWorkspacePanel.hpp. The goal-definition dock: world browser +
// goal stack (live truth) + plan & knowledge to-do + execute-on-the-live-runtime.
#include "GoalWorkspacePanel.hpp"
#include "WorldState.hpp"
#include "SkillRuntime.hpp"
#include "Skill.hpp"
#include "Hone.hpp"
#include "KNode.hpp"
#include "SubgraphNode.hpp"
#include "Scene.hpp"
#include "RobotModel.hpp"
#include "PropertyCatalog.hpp"

#include <QTreeWidget>
#include <QListWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QDir>

#include <cmath>
#include <random>

using krs::skill::Predicate;
using krs::skill::SkillSpec;
using krs::skill::SkillStep;
using krs::policy::Status;

namespace {
QString provText(krs::world::Prov p) {
    switch (p) {
        case krs::world::Prov::Unknown:   return QStringLiteral("unknown");
        case krs::world::Prov::Prior:     return QStringLiteral("prior");
        case krs::world::Prov::Estimated: return QStringLiteral("estimated");
        case krs::world::Prov::Measured:  return QStringLiteral("measured");
        case krs::world::Prov::Dynamic:   return QStringLiteral("DYNAMIC");
        case krs::world::Prov::Suspect:   return QStringLiteral("suspect");
    }
    return QStringLiteral("?");
}
QColor provColor(krs::world::Prov p) {
    switch (p) {
        case krs::world::Prov::Measured:  return QColor(0x66, 0xcc, 0x66);   // green: trusted
        case krs::world::Prov::Estimated: return QColor(0xaa, 0xcc, 0x55);
        case krs::world::Prov::Prior:     return QColor(0xcc, 0xaa, 0x44);   // yellow: guessed
        case krs::world::Prov::Dynamic:   return QColor(0x66, 0xaa, 0xee);   // blue: time-varying
        default:                          return QColor(0xdd, 0x66, 0x55);   // red: unknown/suspect
    }
}
} // namespace

GoalWorkspacePanel::GoalWorkspacePanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto* split = new QSplitter(Qt::Horizontal, this);

    // ---- LEFT: the world browser ----
    auto* leftBox = new QWidget(split);
    auto* lv = new QVBoxLayout(leftBox);
    lv->setContentsMargins(0, 0, 0, 0); lv->setSpacing(3);
    m_world = new QTreeWidget(leftBox);
    m_world->setColumnCount(2);
    m_world->setHeaderLabels({ QStringLiteral("World"), QStringLiteral("Knowledge") });
    m_world->setColumnWidth(0, 150);
    lv->addWidget(m_world, 1);
    auto* lrow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("Refresh"), leftBox);
    auto* tagBtn = new QPushButton(QStringLiteral("SimOnly/R2S"), leftBox);
    lrow->addWidget(refreshBtn); lrow->addWidget(tagBtn);
    lv->addLayout(lrow);
    auto* trow = new QHBoxLayout();
    m_traitPick = new QComboBox(leftBox);
    m_traitPick->addItems({ QStringLiteral("liquid-container"), QStringLiteral("fragile"), QStringLiteral("hot") });
    auto* traitBtn = new QPushButton(QStringLiteral("Add trait"), leftBox);
    trow->addWidget(m_traitPick, 1); trow->addWidget(traitBtn);
    lv->addLayout(trow);
    split->addWidget(leftBox);

    // ---- CENTER: the goal stack ----
    auto* midBox = new QWidget(split);
    auto* mv = new QVBoxLayout(midBox);
    mv->setContentsMargins(0, 0, 0, 0); mv->setSpacing(3);
    mv->addWidget(new QLabel(QStringLiteral("Goal (AND of predicates, live truth):"), midBox));
    m_goalList = new QListWidget(midBox);
    mv->addWidget(m_goalList, 1);
    auto* addRow = new QHBoxLayout();
    m_kind = new QComboBox(midBox);
    m_kind->addItems({ QStringLiteral("at"), QStringLiteral("near"), QStringLiteral("holding"),
                       QStringLiteral("not holding"), QStringLiteral("gripper open") });
    m_objA = new QComboBox(midBox); m_objA->setEditable(true);
    m_objB = new QComboBox(midBox); m_objB->setEditable(true);
    m_tol = new QDoubleSpinBox(midBox);
    m_tol->setRange(0.001, 10.0); m_tol->setValue(0.1); m_tol->setDecimals(3); m_tol->setSingleStep(0.05);
    auto* addBtn = new QPushButton(QStringLiteral("+"), midBox); addBtn->setMaximumWidth(28);
    auto* delBtn = new QPushButton(QStringLiteral("-"), midBox); delBtn->setMaximumWidth(28);
    addRow->addWidget(m_kind); addRow->addWidget(m_objA, 1); addRow->addWidget(m_objB, 1);
    addRow->addWidget(m_tol); addRow->addWidget(addBtn); addRow->addWidget(delBtn);
    mv->addLayout(addRow);
    auto* fileRow = new QHBoxLayout();
    auto* saveBtn = new QPushButton(QStringLiteral("Save Goal..."), midBox);
    auto* loadBtn = new QPushButton(QStringLiteral("Load Goal..."), midBox);
    fileRow->addWidget(saveBtn); fileRow->addWidget(loadBtn); fileRow->addStretch(1);
    mv->addLayout(fileRow);
    split->addWidget(midBox);

    // ---- RIGHT: plan & knowledge ----
    auto* rightBox = new QWidget(split);
    auto* rv = new QVBoxLayout(rightBox);
    rv->setContentsMargins(0, 0, 0, 0); rv->setSpacing(3);
    rv->addWidget(new QLabel(QStringLiteral("Plan (envelopes stamped):"), rightBox));
    m_planList = new QListWidget(rightBox);
    rv->addWidget(m_planList, 2);
    rv->addWidget(new QLabel(QStringLiteral("Knowledge to-do:"), rightBox));
    m_todoList = new QListWidget(rightBox);
    rv->addWidget(m_todoList, 1);
    auto* runRow = new QHBoxLayout();
    m_planBtn = new QPushButton(QStringLiteral("Plan"), rightBox);
    m_execBtn = new QPushButton(QStringLiteral("Execute"), rightBox);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), rightBox);
    m_execBtn->setEnabled(false); m_cancelBtn->setEnabled(false);
    runRow->addWidget(m_planBtn); runRow->addWidget(m_execBtn); runRow->addWidget(m_cancelBtn);
    rv->addLayout(runRow);
    m_status = new QLabel(rightBox);
    m_status->setStyleSheet(QStringLiteral("color:#bbbbbb;"));
    rv->addWidget(m_status);
    split->addWidget(rightBox);

    split->setSizes({ 260, 320, 300 });
    root->addWidget(split, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onRefreshWorld);
    connect(tagBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onToggleTag);
    connect(traitBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onAddTrait);
    connect(addBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onAddPredicate);
    connect(delBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onRemovePredicate);
    connect(saveBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onSaveGoal);
    connect(loadBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onLoadGoal);
    connect(m_planBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onPlan);
    connect(m_execBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onExecute);
    connect(m_cancelBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onCancel);

    m_goal.name = QStringLiteral("goal");
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(200);   // ~5 Hz live truth + status
    connect(m_liveTimer, &QTimer::timeout, this, &GoalWorkspacePanel::onLiveTick);
    m_liveTimer->start();
}

void GoalWorkspacePanel::onRefreshWorld()
{
    if (!m_scene) return;
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    m_world->clear();
    const QString keepA = m_objA->currentText(), keepB = m_objB->currentText();
    m_objA->clear(); m_objB->clear();
    for (const auto& name : ws.objectNames()) {
        auto* item = new QTreeWidgetItem(m_world);
        item->setText(0, QString::fromStdString(name));
        const auto* k = ws.knowledgeOf(name);
        item->setText(1, k && k->tag == krs::world::LearnTag::R2S ? QStringLiteral("R2S") : QStringLiteral("SimOnly"));
        if (k) {
            for (const auto& t : k->traits) {
                auto* ti = new QTreeWidgetItem(item);
                ti->setText(0, QStringLiteral("trait"));
                ti->setText(1, QString::fromStdString(t));
            }
            for (const auto& [pn, p] : k->params) {
                auto* pi = new QTreeWidgetItem(item);
                pi->setText(0, QString::fromStdString(pn));
                pi->setText(1, QStringLiteral("%1 ± %2 (%3)")
                                   .arg(p.value, 0, 'g', 4).arg(p.sigma, 0, 'g', 3).arg(provText(p.prov)));
                pi->setForeground(1, provColor(p.prov));
            }
        }
        item->setExpanded(true);
        m_objA->addItem(QString::fromStdString(name));
    }
    // frames feed the B combo (targets)
    m_objB->addItem(QStringLiteral("drop_pose"));
    if (!keepA.isEmpty()) m_objA->setCurrentText(keepA);
    if (!keepB.isEmpty()) m_objB->setCurrentText(keepB);
}

void GoalWorkspacePanel::onToggleTag()
{
    if (!m_scene || !m_world->currentItem()) return;
    QTreeWidgetItem* item = m_world->currentItem();
    while (item->parent()) item = item->parent();
    auto& k = krs::world::worldState(m_scene->getRegistry()).know(item->text(0).toStdString());
    k.tag = (k.tag == krs::world::LearnTag::R2S) ? krs::world::LearnTag::SimOnly : krs::world::LearnTag::R2S;
    onRefreshWorld();
}

void GoalWorkspacePanel::onAddTrait()
{
    if (!m_scene || !m_world->currentItem()) return;
    QTreeWidgetItem* item = m_world->currentItem();
    while (item->parent()) item = item->parent();
    auto& k = krs::world::worldState(m_scene->getRegistry()).know(item->text(0).toStdString());
    const std::string t = m_traitPick->currentText().toStdString();
    if (!k.hasTrait(t)) k.traits.push_back(t);
    onRefreshWorld();
}

void GoalWorkspacePanel::onAddPredicate()
{
    const std::string a = m_objA->currentText().toStdString();
    const std::string b = m_objB->currentText().toStdString();
    const double tol = m_tol->value();
    Predicate p;
    switch (m_kind->currentIndex()) {
        case 0: p = { Predicate::Kind::At, false, 0, a, b, tol }; break;
        case 1: p = { Predicate::Kind::Near, false, 0, a, b, tol }; break;
        case 2: p = { Predicate::Kind::Holding, false, 0, a }; break;
        case 3: p = { Predicate::Kind::Holding, true, 0, a }; break;
        default: p = { Predicate::Kind::GripperOpen, false, 0 }; break;
    }
    m_goal.require.push_back(p);
    rebuildGoalList();
}

void GoalWorkspacePanel::onRemovePredicate()
{
    const int row = m_goalList->currentRow();
    if (row >= 0 && row < int(m_goal.require.size())) {
        m_goal.require.erase(m_goal.require.begin() + row);
        rebuildGoalList();
    }
}

void GoalWorkspacePanel::rebuildGoalList()
{
    m_goalList->clear();
    for (const auto& p : m_goal.require)
        m_goalList->addItem(QString::fromStdString(p.text()));
    onLiveTick();
}

void GoalWorkspacePanel::onSaveGoal()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Goal"),
        QStringLiteral("goal.kgoal"), QStringLiteral("Goal (*.kgoal)"));
    if (path.isEmpty()) return;
    krs::goal::saveKGoal(m_goal, path);
    m_status->setText(QStringLiteral("Saved %1").arg(path));
}

void GoalWorkspacePanel::onLoadGoal()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load Goal"),
        QString(), QStringLiteral("Goal (*.kgoal)"));
    if (path.isEmpty()) return;
    QString err;
    if (krs::goal::loadKGoal(path, m_goal, &err)) rebuildGoalList();
    else m_status->setText(QStringLiteral("Load failed: %1").arg(err));
}

// The v1 built-in library bound to `object`: grasp+place bodies from assets/skills/*.knode with
// placeholder joint targets, plus the lift_weigh honing excitation (sim twin, sealed truth).
std::vector<SkillStep> GoalWorkspacePanel::buildLibraryFor(const std::string& object)
{
    m_specs.clear();
    std::vector<SkillStep> lib;
    if (!m_scene) return lib;
    auto& reg = m_scene->getRegistry();
    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = (rr && !rr->robots.empty()) ? rr->robots.front().get() : nullptr;
    if (!lr) return lib;
    const int n = lr->ndof();
    Eigen::VectorXd qGrasp(n), qPlace(n);
    for (int i = 0; i < n; ++i) { qGrasp[i] = 0.12 + 0.02 * i; qPlace[i] = -0.12 - 0.02 * i; }

    const QString skillsDir = QStringLiteral(KRS_SOURCE_DIR) + QStringLiteral("/assets/skills");
    auto graspDoc = std::make_shared<krs::knode::KNodeDoc>();
    auto placeDoc = std::make_shared<krs::knode::KNodeDoc>();
    QString kerr;
    if (!krs::knode::loadKNode(skillsDir + "/grasp.knode", *graspDoc, &kerr)) return lib;
    if (!krs::knode::loadKNode(skillsDir + "/place.knode", *placeDoc, &kerr)) return lib;
    auto realize = [lr, this](std::shared_ptr<krs::knode::KNodeDoc> doc, Eigen::VectorXd target) {
        return [doc, lr, target, this](Scene*, const krs::skill::ParamMap&) {
            auto body = std::make_shared<krs::nodes::SubgraphNode>(*doc);
            return krs::skill::makeSkillLeaf(body, m_scene, { { "target", krs::skill::ParamValue::cfg(target) } },
                                             krs::skill::qNear(lr, target));
        };
    };

    auto grasp = std::make_unique<SkillSpec>();
    grasp->name = "grasp";
    grasp->pre = { { Predicate::Kind::GripperOpen, false, 0 }, { Predicate::Kind::Fresh, false, 0, object, "", 30.0 } };
    grasp->eff = { { Predicate::Kind::Holding, false, 0, object }, { Predicate::Kind::GripperOpen, true, 0 } };
    grasp->needs = { { "mass", 0.05 } };
    grasp->intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };
    grasp->realize = realize(graspDoc, qGrasp);
    grasp->timeoutSec = 5.0;

    auto place = std::make_unique<SkillSpec>();
    place->name = "place";
    place->pre = { { Predicate::Kind::Holding, false, 0, object } };
    place->eff = { { Predicate::Kind::Holding, true, 0, object }, { Predicate::Kind::GripperOpen, false, 0 },
                   { Predicate::Kind::At, false, 0, object, "drop_pose", 0.1 } };
    place->needs = { { "mass", 0.05 } };
    place->intrinsicEnvelope = { { "maxTiltDeg", 60.0 } };
    place->realize = realize(placeDoc, qPlace);
    place->timeoutSec = 5.0;

    auto lift = std::make_unique<SkillSpec>();
    lift->name = "lift_weigh";
    lift->honesParam = "mass"; lift->honedSigma = 0.02;
    lift->timeoutSec = 5.0;
    Scene* sc = m_scene;
    lift->realize = [sc, object](Scene*, const krs::skill::ParamMap&) {
        return [sc, object](double, krs::policy::Blackboard&) -> Status {
            // sim-only twin honing: sealed truth from the CAD prior (or 0.372 default), noisy FT record.
            auto& wsr = krs::world::worldState(sc->getRegistry());
            double truth = 0.372;
            if (const auto* k = wsr.knowledgeOf(object))
                if (auto it = k->params.find("mass"); it != k->params.end() && it->second.value > 0) truth = it->second.value;
            krs::hone::HoneConfig cfg;
            auto model = [](double m) { return std::vector<double>(50, m * 9.81); };
            std::vector<double> real = model(truth);
            std::mt19937 rng(1234);
            std::normal_distribution<double> nz(0.0, cfg.measNoiseStd);
            for (auto& v : real) v += nz(rng);
            const krs::hone::HoneResult hr = krs::hone::hone({ truth * 1.5, 0.3, krs::hone::Prov::Prior }, model, real, cfg);
            if (!hr.converged) return Status::Failure;
            wsr.know(object).params["mass"] = { hr.posterior.value, std::max(hr.posterior.sigma, 1e-4),
                                                krs::world::Prov::Measured };
            return Status::Success;
        };
    };

    lib.push_back({ grasp.get(), {}, object });
    lib.push_back({ place.get(), {}, object });
    lib.push_back({ lift.get(),  {}, object });
    m_specs.push_back(std::move(grasp));
    m_specs.push_back(std::move(place));
    m_specs.push_back(std::move(lift));
    return lib;
}

void GoalWorkspacePanel::onPlan()
{
    if (!m_scene || m_goal.require.empty()) { m_status->setText(QStringLiteral("Add a goal predicate first.")); return; }
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    // v1: the bound object = the first predicate's subject.
    const std::string object = m_goal.require.front().a;
    const std::vector<SkillStep> lib = buildLibraryFor(object);
    if (lib.empty()) { m_status->setText(QStringLiteral("No robot / primitive files -- cannot build the library.")); return; }
    m_plan = krs::skill::planBackward(lib, ws, m_goal.require, /*autoInsertExcitation*/ true);

    m_planList->clear();
    m_todoList->clear();
    if (!m_plan.ok) {
        m_planList->addItem(QStringLiteral("PLAN FAILED: %1").arg(QString::fromStdString(m_plan.why)));
        m_execBtn->setEnabled(false);
        return;
    }
    for (const auto& s : m_plan.steps) {
        QString env;
        for (const auto& [k, v] : s.envelope) env += QStringLiteral(" %1=%2").arg(QString::fromStdString(k)).arg(v);
        m_planList->addItem(QStringLiteral("%1 [%2]%3")
                                .arg(QString::fromStdString(s.spec->name), QString::fromStdString(s.object), env));
    }
    for (const auto& gap : m_plan.gaps) {
        auto* it = new QListWidgetItem(QStringLiteral("%1.%2: σ %3 -> need %4  (%5%6)")
            .arg(QString::fromStdString(gap.object), QString::fromStdString(gap.param))
            .arg(gap.sigmaNow, 0, 'g', 3).arg(gap.sigmaNeeded, 0, 'g', 3)
            .arg(QString::fromStdString(gap.suggestedSkill.empty() ? "no excitation known" : gap.suggestedSkill),
                 gap.resolvedByPlan ? QStringLiteral(" -- inserted") : QStringLiteral(" -- queued")));
        it->setForeground(gap.resolvedByPlan ? QColor(0x66, 0xcc, 0x66) : QColor(0xcc, 0xaa, 0x44));
        m_todoList->addItem(it);
    }
    m_execBtn->setEnabled(true);
    m_status->setText(QStringLiteral("Plan: %1 step(s), %2 gap(s).").arg(m_plan.steps.size()).arg(m_plan.gaps.size()));
}

void GoalWorkspacePanel::onExecute()
{
    if (!m_scene || !m_plan.ok) return;
    auto& reg = m_scene->getRegistry();
    auto& ws = krs::world::worldState(reg);
    m_taskId = krs::skill::skillRuntime(reg).start("goal-workspace",
                                                   krs::skill::composeSequence(m_plan.steps, m_scene, ws));
    m_execBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_status->setText(QStringLiteral("Executing on the live runtime..."));
}

void GoalWorkspacePanel::onCancel()
{
    if (m_scene && m_taskId >= 0) krs::skill::skillRuntime(m_scene->getRegistry()).cancel(m_taskId);
    m_cancelBtn->setEnabled(false);
}

void GoalWorkspacePanel::onLiveTick()
{
    if (!m_scene) return;
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    // live truth on the goal cards
    for (int i = 0; i < m_goalList->count() && i < int(m_goal.require.size()); ++i) {
        const bool ok = m_goal.require[i].eval(ws);
        QListWidgetItem* it = m_goalList->item(i);
        it->setText(QStringLiteral("%1  %2").arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"),
                                                 QString::fromStdString(m_goal.require[i].text())));
        it->setForeground(ok ? QColor(0x66, 0xcc, 0x66) : QColor(0xdd, 0x66, 0x55));
    }
    // running-task status
    if (m_taskId >= 0) {
        const Status s = krs::skill::skillRuntime(m_scene->getRegistry()).status(m_taskId);
        if (s == Status::Success)      { m_status->setText(QStringLiteral("Task SUCCEEDED.")); m_taskId = -1; m_cancelBtn->setEnabled(false); m_execBtn->setEnabled(true); onRefreshWorld(); }
        else if (s == Status::Failure) { m_status->setText(QStringLiteral("Task FAILED (see gaps/guards).")); m_taskId = -1; m_cancelBtn->setEnabled(false); m_execBtn->setEnabled(true); }
    }
}
