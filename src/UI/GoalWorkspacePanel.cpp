// GoalWorkspacePanel.cpp -- see GoalWorkspacePanel.hpp. UX pass 2: a GUIDED numbered flow
// (1 objects -> 2 target locations -> 3 goal checklist -> 4 plan & execute) with plain-English
// explainers, contextual composer fields, empty-state hints, and FRAME AUTHORING (the missing
// piece that made at()-goals impossible to author from the GUI).
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
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
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
// A dim, word-wrapped explainer line under each step header.
QLabel* hint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    return l;
}
} // namespace

GoalWorkspacePanel::GoalWorkspacePanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    // The whole flow lives in a scrollable column: the dock stays usable at any height.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Tell the robot WHAT you want -- it figures out HOW."), page);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-weight:bold;"));
    root->addWidget(title);
    root->addWidget(hint(QStringLiteral(
        "Work top to bottom: check what the robot knows (1), name a target location if the goal "
        "needs one (2), build the goal checklist (3), then Plan and Execute (4)."), page));

    // ================= (1) SCENE OBJECTS =================
    auto* g1 = new QGroupBox(QStringLiteral("1 · Scene objects -- what the robot knows"), page);
    auto* lv = new QVBoxLayout(g1);
    lv->setSpacing(4);
    lv->addWidget(hint(QStringLiteral(
        "Each object lists what the robot has learned (value ± confidence). Green = measured, "
        "yellow = guessed, red = unknown. R2S objects MUST be truly measured before execution; "
        "SimOnly objects accept best guesses."), g1));
    m_world = new QTreeWidget(g1);
    m_world->setColumnCount(2);
    m_world->setHeaderLabels({ QStringLiteral("Object"), QStringLiteral("Knowledge") });
    m_world->setColumnWidth(0, 150);
    m_world->setMinimumHeight(140);
    lv->addWidget(m_world);
    auto* lrow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("↻ Refresh"), g1);
    refreshBtn->setToolTip(QStringLiteral("Re-read the live scene into the world model."));
    auto* tagBtn = new QPushButton(QStringLiteral("Toggle SimOnly ⇄ R2S"), g1);
    tagBtn->setToolTip(QStringLiteral(
        "R2S (real-to-sim) = parameters must be MEASURED (the planner inserts measuring steps).\n"
        "SimOnly = best guesses are accepted."));
    lrow->addWidget(refreshBtn); lrow->addWidget(tagBtn);
    lv->addLayout(lrow);
    auto* trow = new QHBoxLayout();
    m_traitPick = new QComboBox(g1);
    m_traitPick->addItems({ QStringLiteral("liquid-container"), QStringLiteral("fragile"), QStringLiteral("hot") });
    m_traitPick->setToolTip(QStringLiteral(
        "Traits constrain HOW the robot may move an object (e.g. a liquid-container caps tilt)."));
    auto* traitBtn = new QPushButton(QStringLiteral("Add trait"), g1);
    trow->addWidget(m_traitPick, 1); trow->addWidget(traitBtn);
    lv->addLayout(trow);
    root->addWidget(g1);

    // ================= (2) TARGET LOCATIONS =================
    auto* g2 = new QGroupBox(QStringLiteral("2 · Target locations (named frames)"), page);
    auto* fv = new QVBoxLayout(g2);
    fv->setSpacing(4);
    fv->addWidget(hint(QStringLiteral(
        "Goals like “cup is at drop_pose” need a NAMED location. Create one here: pick a "
        "name, then place it at the selected object's position or at typed coordinates."), g2));
    m_frameList = new QListWidget(g2);
    m_frameList->setMaximumHeight(70);
    fv->addWidget(m_frameList);
    auto* frow1 = new QHBoxLayout();
    frow1->addWidget(new QLabel(QStringLiteral("Name:"), g2));
    m_frameName = new QLineEdit(QStringLiteral("drop_pose"), g2);
    frow1->addWidget(m_frameName, 1);
    auto* atObjBtn = new QPushButton(QStringLiteral("Place at selected object"), g2);
    atObjBtn->setToolTip(QStringLiteral("Uses the position of the object selected in step 1."));
    frow1->addWidget(atObjBtn);
    fv->addLayout(frow1);
    auto* frow2 = new QHBoxLayout();
    auto mkSpin = [g2](double v) {
        auto* s = new QDoubleSpinBox(g2);
        s->setRange(-100.0, 100.0); s->setDecimals(3); s->setSingleStep(0.05); s->setValue(v);
        return s;
    };
    m_fx = mkSpin(0.5); m_fy = mkSpin(0.5); m_fz = mkSpin(0.0);
    frow2->addWidget(new QLabel(QStringLiteral("X"), g2)); frow2->addWidget(m_fx, 1);
    frow2->addWidget(new QLabel(QStringLiteral("Y"), g2)); frow2->addWidget(m_fy, 1);
    frow2->addWidget(new QLabel(QStringLiteral("Z"), g2)); frow2->addWidget(m_fz, 1);
    auto* atXyzBtn = new QPushButton(QStringLiteral("Place at X,Y,Z"), g2);
    frow2->addWidget(atXyzBtn);
    fv->addLayout(frow2);
    root->addWidget(g2);

    // ================= (3) THE GOAL =================
    auto* g3 = new QGroupBox(QStringLiteral("3 · The goal -- a checklist the world must satisfy"), page);
    auto* mv = new QVBoxLayout(g3);
    mv->setSpacing(4);
    mv->addWidget(hint(QStringLiteral(
        "Add one condition at a time. ✓/✗ update LIVE against the current scene, so you can watch "
        "the goal become true as the robot works (or as you drag objects around)."), g3));
    m_goalEmpty = hint(QStringLiteral("No conditions yet -- compose one below and press “Add”."), g3);
    mv->addWidget(m_goalEmpty);
    m_goalList = new QListWidget(g3);
    m_goalList->setMinimumHeight(70);
    mv->addWidget(m_goalList);

    m_kind = new QComboBox(g3);
    m_kind->addItems({
        QStringLiteral("Object is AT a location (exact spot)"),
        QStringLiteral("Object is NEAR another object / location"),
        QStringLiteral("Robot is HOLDING the object"),
        QStringLiteral("Robot is NOT holding the object"),
        QStringLiteral("Gripper is open"),
    });
    mv->addWidget(m_kind);
    // contextual rows: only the fields the chosen condition uses are visible
    m_rowA = new QWidget(g3);
    { auto* h = new QHBoxLayout(m_rowA); h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel(QStringLiteral("Object:"), m_rowA));
      m_objA = new QComboBox(m_rowA); m_objA->setEditable(true);
      h->addWidget(m_objA, 1); }
    m_rowB = new QWidget(g3);
    { auto* h = new QHBoxLayout(m_rowB); h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel(QStringLiteral("Target:"), m_rowB));
      m_objB = new QComboBox(m_rowB); m_objB->setEditable(true);
      h->addWidget(m_objB, 1); }
    m_rowTol = new QWidget(g3);
    { auto* h = new QHBoxLayout(m_rowTol); h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel(QStringLiteral("Within (m):"), m_rowTol));
      m_tol = new QDoubleSpinBox(m_rowTol);
      m_tol->setRange(0.001, 10.0); m_tol->setValue(0.1); m_tol->setDecimals(3); m_tol->setSingleStep(0.05);
      m_tol->setToolTip(QStringLiteral("How close counts as “there”."));
      h->addWidget(m_tol, 1); }
    mv->addWidget(m_rowA); mv->addWidget(m_rowB); mv->addWidget(m_rowTol);
    auto* addRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("＋ Add condition"), g3);
    auto* delBtn = new QPushButton(QStringLiteral("－ Remove selected"), g3);
    addRow->addWidget(addBtn); addRow->addWidget(delBtn);
    mv->addLayout(addRow);
    auto* fileRow = new QHBoxLayout();
    auto* saveBtn = new QPushButton(QStringLiteral("Save Goal..."), g3);
    auto* loadBtn = new QPushButton(QStringLiteral("Load Goal..."), g3);
    saveBtn->setToolTip(QStringLiteral("Write the checklist as a shareable .kgoal document."));
    fileRow->addWidget(saveBtn); fileRow->addWidget(loadBtn); fileRow->addStretch(1);
    mv->addLayout(fileRow);
    root->addWidget(g3);

    // ================= (4) PLAN & EXECUTE =================
    auto* g4 = new QGroupBox(QStringLiteral("4 · Plan & execute"), page);
    auto* rv = new QVBoxLayout(g4);
    rv->setSpacing(4);
    rv->addWidget(hint(QStringLiteral(
        "Plan works BACKWARD from the goal, ordering skills and inserting measuring steps for "
        "missing knowledge (the to-do below). Execute runs the plan on the live robot -- guards "
        "watch the trait envelopes while it moves."), g4));
    rv->addWidget(new QLabel(QStringLiteral("Steps (with safety envelopes):"), g4));
    m_planList = new QListWidget(g4);
    m_planList->setMinimumHeight(70);
    rv->addWidget(m_planList);
    rv->addWidget(new QLabel(QStringLiteral("Knowledge to-do (what must be measured first):"), g4));
    m_todoList = new QListWidget(g4);
    m_todoList->setMaximumHeight(70);
    rv->addWidget(m_todoList);
    auto* runRow = new QHBoxLayout();
    m_planBtn = new QPushButton(QStringLiteral("🗺  Plan"), g4);
    m_execBtn = new QPushButton(QStringLiteral("▶  Execute"), g4);
    m_cancelBtn = new QPushButton(QStringLiteral("■  Stop"), g4);
    m_execBtn->setEnabled(false); m_cancelBtn->setEnabled(false);
    m_execBtn->setToolTip(QStringLiteral("Enabled after a successful Plan."));
    runRow->addWidget(m_planBtn); runRow->addWidget(m_execBtn); runRow->addWidget(m_cancelBtn);
    rv->addLayout(runRow);
    m_status = new QLabel(g4);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#bbbbbb;"));
    rv->addWidget(m_status);
    root->addWidget(g4);

    root->addStretch(1);
    scroll->setWidget(page);
    outer->addWidget(scroll);

    connect(refreshBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onRefreshWorld);
    connect(tagBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onToggleTag);
    connect(traitBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onAddTrait);
    connect(atObjBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onSetFrameAtObject);
    connect(atXyzBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onSetFrameAtXyz);
    connect(addBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onAddPredicate);
    connect(delBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onRemovePredicate);
    connect(saveBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onSaveGoal);
    connect(loadBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onLoadGoal);
    connect(m_planBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onPlan);
    connect(m_execBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onExecute);
    connect(m_cancelBtn, &QPushButton::clicked, this, &GoalWorkspacePanel::onCancel);
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GoalWorkspacePanel::onKindChanged);
    onKindChanged(0);

    m_goal.name = QStringLiteral("goal");
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(200);   // ~5 Hz live truth + status
    connect(m_liveTimer, &QTimer::timeout, this, &GoalWorkspacePanel::onLiveTick);
    m_liveTimer->start();
    // Self-populate shortly after boot (the catalog needs an eval pass or two) -- an empty
    // panel with a Refresh button is exactly the "makes no sense" first impression to avoid.
    QTimer::singleShot(1500, this, &GoalWorkspacePanel::onRefreshWorld);
}

void GoalWorkspacePanel::onKindChanged(int idx)
{
    // At/Near use object+target+tolerance; Holding/NotHolding use object; GripperOpen uses none.
    const bool needsA   = (idx <= 3);
    const bool needsB   = (idx <= 1);
    const bool needsTol = (idx <= 1);
    m_rowA->setVisible(needsA);
    m_rowB->setVisible(needsB);
    m_rowTol->setVisible(needsTol);
}

void GoalWorkspacePanel::refreshFrames()
{
    if (!m_scene) return;
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    m_frameList->clear();
    const QString keepB = m_objB->currentText();
    // targets = frames first (that is what at() wants), then objects (near() accepts both)
    m_objB->clear();
    for (const auto& fn : ws.frameNames()) {
        glm::vec3 p(0.0f);
        ws.positionOf(fn, &p);
        m_frameList->addItem(QStringLiteral("%1   @ (%2, %3, %4)")
            .arg(QString::fromStdString(fn))
            .arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(p.z, 0, 'f', 2));
        m_objB->addItem(QString::fromStdString(fn));
    }
    if (m_frameList->count() == 0)
        m_frameList->addItem(QStringLiteral("(no frames yet -- name one below)"));
    for (const auto& name : ws.objectNames())
        m_objB->addItem(QString::fromStdString(name));
    if (!keepB.isEmpty()) m_objB->setCurrentText(keepB);
}

void GoalWorkspacePanel::onRefreshWorld()
{
    if (!m_scene) return;
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    m_world->clear();
    const QString keepA = m_objA->currentText();
    m_objA->clear();
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
    if (!keepA.isEmpty()) m_objA->setCurrentText(keepA);
    refreshFrames();
}

void GoalWorkspacePanel::onToggleTag()
{
    if (!m_scene || !m_world->currentItem()) { m_status->setText(QStringLiteral("Select an object in the list first.")); return; }
    QTreeWidgetItem* item = m_world->currentItem();
    while (item->parent()) item = item->parent();
    auto& k = krs::world::worldState(m_scene->getRegistry()).know(item->text(0).toStdString());
    k.tag = (k.tag == krs::world::LearnTag::R2S) ? krs::world::LearnTag::SimOnly : krs::world::LearnTag::R2S;
    onRefreshWorld();
}

void GoalWorkspacePanel::onAddTrait()
{
    if (!m_scene || !m_world->currentItem()) { m_status->setText(QStringLiteral("Select an object in the list first.")); return; }
    QTreeWidgetItem* item = m_world->currentItem();
    while (item->parent()) item = item->parent();
    auto& k = krs::world::worldState(m_scene->getRegistry()).know(item->text(0).toStdString());
    const std::string t = m_traitPick->currentText().toStdString();
    if (!k.hasTrait(t)) k.traits.push_back(t);
    onRefreshWorld();
}

void GoalWorkspacePanel::onSetFrameAtObject()
{
    if (!m_scene) return;
    const QString name = m_frameName->text().trimmed();
    if (name.isEmpty()) { m_status->setText(QStringLiteral("Give the frame a name first.")); return; }
    if (!m_world->currentItem()) { m_status->setText(QStringLiteral("Select an object in step 1 first.")); return; }
    QTreeWidgetItem* item = m_world->currentItem();
    while (item->parent()) item = item->parent();
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    glm::vec3 p(0.0f);
    if (!ws.positionOf(item->text(0).toStdString(), &p)) {
        m_status->setText(QStringLiteral("That object has no known position yet (press Refresh)."));
        return;
    }
    ws.setFrame(name.toStdString(), p);
    m_status->setText(QStringLiteral("Frame “%1” placed at %2's position.").arg(name, item->text(0)));
    refreshFrames();
    m_objB->setCurrentText(name);
}

void GoalWorkspacePanel::onSetFrameAtXyz()
{
    if (!m_scene) return;
    const QString name = m_frameName->text().trimmed();
    if (name.isEmpty()) { m_status->setText(QStringLiteral("Give the frame a name first.")); return; }
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    ws.setFrame(name.toStdString(),
                glm::vec3(float(m_fx->value()), float(m_fy->value()), float(m_fz->value())));
    m_status->setText(QStringLiteral("Frame “%1” placed at (%2, %3, %4).")
        .arg(name).arg(m_fx->value(), 0, 'f', 2).arg(m_fy->value(), 0, 'f', 2).arg(m_fz->value(), 0, 'f', 2));
    refreshFrames();
    m_objB->setCurrentText(name);
}

void GoalWorkspacePanel::onAddPredicate()
{
    const std::string a = m_objA->currentText().toStdString();
    const std::string b = m_objB->currentText().toStdString();
    const int idx = m_kind->currentIndex();
    if (idx <= 3 && a.empty()) { m_status->setText(QStringLiteral("Pick the object the condition is about.")); return; }
    if (idx <= 1 && b.empty()) { m_status->setText(QStringLiteral("Pick a target (create a frame in step 2 if needed).")); return; }
    const double tol = m_tol->value();
    Predicate p;
    switch (idx) {
        case 0: p = { Predicate::Kind::At, false, 0, a, b, tol }; break;
        case 1: p = { Predicate::Kind::Near, false, 0, a, b, tol }; break;
        case 2: p = { Predicate::Kind::Holding, false, 0, a }; break;
        case 3: p = { Predicate::Kind::Holding, true, 0, a }; break;
        default: p = { Predicate::Kind::GripperOpen, false, 0 }; break;
    }
    m_goal.require.push_back(p);
    rebuildGoalList();
    m_status->setText(QStringLiteral("Condition added. Add more, or continue to step 4."));
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
    m_goalEmpty->setVisible(m_goal.require.empty());
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
    if (!m_scene || m_goal.require.empty()) {
        m_status->setText(QStringLiteral("The goal is empty -- add at least one condition in step 3 first."));
        return;
    }
    auto& ws = krs::world::worldState(m_scene->getRegistry());
    // v1: the bound object = the first predicate's subject.
    const std::string object = m_goal.require.front().a;
    const std::vector<SkillStep> lib = buildLibraryFor(object);
    if (lib.empty()) {
        m_status->setText(QStringLiteral("Cannot build the skill library: no robot is loaded (or the "
                                         "assets/skills/*.knode primitives are missing)."));
        return;
    }
    m_plan = krs::skill::planBackward(lib, ws, m_goal.require, /*autoInsertExcitation*/ true);

    m_planList->clear();
    m_todoList->clear();
    if (!m_plan.ok) {
        m_planList->addItem(QStringLiteral("PLAN FAILED: %1").arg(QString::fromStdString(m_plan.why)));
        m_execBtn->setEnabled(false);
        m_status->setText(QStringLiteral("Planning failed -- see the first line above for why."));
        return;
    }
    int stepNo = 1;
    for (const auto& s : m_plan.steps) {
        QString env;
        for (const auto& [k, v] : s.envelope) env += QStringLiteral(" %1=%2").arg(QString::fromStdString(k)).arg(v);
        m_planList->addItem(QStringLiteral("%1. %2 [%3]%4")
                                .arg(stepNo++)
                                .arg(QString::fromStdString(s.spec->name), QString::fromStdString(s.object), env));
    }
    for (const auto& gap : m_plan.gaps) {
        auto* it = new QListWidgetItem(QStringLiteral("%1.%2: σ %3 -> need %4  (%5%6)")
            .arg(QString::fromStdString(gap.object), QString::fromStdString(gap.param))
            .arg(gap.sigmaNow, 0, 'g', 3).arg(gap.sigmaNeeded, 0, 'g', 3)
            .arg(QString::fromStdString(gap.suggestedSkill.empty() ? "no excitation known" : gap.suggestedSkill),
                 gap.resolvedByPlan ? QStringLiteral(" -- inserted into the plan") : QStringLiteral(" -- still queued")));
        it->setForeground(gap.resolvedByPlan ? QColor(0x66, 0xcc, 0x66) : QColor(0xcc, 0xaa, 0x44));
        m_todoList->addItem(it);
    }
    if (m_plan.gaps.empty())
        m_todoList->addItem(QStringLiteral("(nothing -- all required knowledge is already good enough)"));
    m_execBtn->setEnabled(true);
    m_status->setText(QStringLiteral("Plan ready: %1 step(s), %2 knowledge gap(s). Press Execute to run it live.")
                          .arg(m_plan.steps.size()).arg(m_plan.gaps.size()));
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
    m_status->setText(QStringLiteral("Executing on the live runtime... watch the ✓s in step 3."));
}

void GoalWorkspacePanel::onCancel()
{
    if (m_scene && m_taskId >= 0) krs::skill::skillRuntime(m_scene->getRegistry()).cancel(m_taskId);
    m_cancelBtn->setEnabled(false);
    m_status->setText(QStringLiteral("Stopped -- the robot's joints release on the next pass."));
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
        if (s == Status::Success)      { m_status->setText(QStringLiteral("Task SUCCEEDED -- the goal checklist should be all ✓.")); m_taskId = -1; m_cancelBtn->setEnabled(false); m_execBtn->setEnabled(true); onRefreshWorld(); }
        else if (s == Status::Failure) { m_status->setText(QStringLiteral("Task FAILED -- check the knowledge to-do and trait guards, then re-Plan.")); m_taskId = -1; m_cancelBtn->setEnabled(false); m_execBtn->setEnabled(true); }
    }
}
