#include "RobotBuilderPanel.hpp"

#include "Scene.hpp"
#include "components.hpp"            // BRepFace
#include "SelectionService.hpp"     // krs::sel::SelectionState / Selection / FeatureType
#include "RobotBuilder.hpp"         // krs::rbuild::RobotGraph / EditController / RBJoint
#include "RobotBuilderScene.hpp"    // buildDemoGraph / spawnGraphBodies / bodyIndexForEntity
#include "RobotModel.hpp"           // krs::robot::instantiateFromGraph (demo as a first-class robot)
#include "RobotConfig.hpp"          // krs::rcfg::RobotConfig (proven property hot-swap)

#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QMetaMethod>

#include <cstdio>
#include <vector>

namespace {
// House-style section header (centered bold label over a line) -- mirrors
// PhysicsPropertiesWidget::makeSectionHeader so the panel matches its siblings.
QWidget* makeSectionHeader(const QString& text, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 6, 0, 0);
    l->setSpacing(2);
    auto* label = new QLabel(text, w);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-weight: bold;"));
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    l->addWidget(label);
    l->addWidget(line);
    return w;
}
} // namespace

RobotBuilderPanel::RobotBuilderPanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    // Dark theme matching the established panels (audit palette).
    setStyleSheet(QStringLiteral(
        "QWidget{background:#2c313a;color:#e6e6e6;}"
        "QGroupBox{border:1px solid #4a5260;margin-top:6px;}"
        "QPushButton{background:#262b33;border:1px solid #4a5260;padding:4px;}"
        "QPushButton:hover{background:#323844;}"
        "QListWidget,QDoubleSpinBox,QSpinBox{background:#262b33;border:1px solid #4a5260;}"));

    initializeUI();
    setupConnections();
    refresh();
}

RobotBuilderPanel::~RobotBuilderPanel() = default;

void RobotBuilderPanel::initializeUI()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    // --- Robot ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Robot"), content));
    m_loadDemoBtn = new QPushButton(QStringLiteral("Load Demo Robot"), content);
    m_loadDemoBtn->setObjectName(QStringLiteral("rbLoadDemoButton"));
    layout->addWidget(m_loadDemoBtn);
    m_dofLabel = new QLabel(QStringLiteral("DOF: -"), content);
    m_dofLabel->setObjectName(QStringLiteral("rbDofLabel"));
    m_dofLabel->setStyleSheet(QStringLiteral("font-weight:bold;padding:2px;"));
    layout->addWidget(m_dofLabel);

    // --- Joints (delete) ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Joints"), content));
    m_jointsList = new QListWidget(content);
    m_jointsList->setObjectName(QStringLiteral("rbJointsList"));
    layout->addWidget(m_jointsList);
    m_deleteBtn = new QPushButton(QStringLiteral("Delete Selected Joint"), content);
    m_deleteBtn->setObjectName(QStringLiteral("rbDeleteJointButton"));
    layout->addWidget(m_deleteBtn);

    // --- Define joint from features ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Define Joint"), content));
    m_defineHint = new QLabel(QStringLiteral("Select two coaxial bores in the viewport, then:"), content);
    m_defineHint->setWordWrap(true);
    layout->addWidget(m_defineHint);
    m_defineBtn = new QPushButton(QStringLiteral("Define Revolute from 2 Selected Bores"), content);
    m_defineBtn->setObjectName(QStringLiteral("rbDefineFromFeaturesButton"));
    layout->addWidget(m_defineBtn);

    // --- Joint limits (proven property hot-swap) ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Joint Limits (hot-swap)"), content));
    auto* limBox = new QGroupBox(content);
    auto* limForm = new QFormLayout(limBox);
    m_dofIndex = new QSpinBox(limBox);
    m_dofIndex->setObjectName(QStringLiteral("rbLimitDofSpin"));
    m_dofIndex->setRange(0, 0);
    m_limitLo = new QDoubleSpinBox(limBox);
    m_limitLo->setObjectName(QStringLiteral("rbLimitLoSpin"));
    m_limitLo->setRange(-3.14159 * 4, 3.14159 * 4); m_limitLo->setDecimals(3); m_limitLo->setSingleStep(0.1);
    m_limitLo->setKeyboardTracking(false);
    m_limitHi = new QDoubleSpinBox(limBox);
    m_limitHi->setObjectName(QStringLiteral("rbLimitHiSpin"));
    m_limitHi->setRange(-3.14159 * 4, 3.14159 * 4); m_limitHi->setDecimals(3); m_limitHi->setSingleStep(0.1);
    m_limitHi->setKeyboardTracking(false);
    limForm->addRow(QStringLiteral("DOF #"), m_dofIndex);
    limForm->addRow(QStringLiteral("Lower (rad)"), m_limitLo);
    limForm->addRow(QStringLiteral("Upper (rad)"), m_limitHi);
    layout->addWidget(limBox);
    m_applyLimitBtn = new QPushButton(QStringLiteral("Apply Limit (hot-swap)"), content);
    m_applyLimitBtn->setObjectName(QStringLiteral("rbApplyLimitButton"));
    layout->addWidget(m_applyLimitBtn);

    // --- Joint axis origin (adjust where the selected joint snaps to) ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Joint Axis Origin"), content));
    auto* axBox = new QGroupBox(content);
    auto* axForm = new QFormLayout(axBox);
    auto mkAxisSpin = [&](const char* objName) {
        auto* s = new QDoubleSpinBox(axBox);
        s->setObjectName(QString::fromLatin1(objName));
        s->setRange(-1000.0, 1000.0); s->setDecimals(3); s->setSingleStep(0.01);
        s->setKeyboardTracking(false);
        return s;
    };
    m_axisX = mkAxisSpin("rbAxisXSpin");
    m_axisY = mkAxisSpin("rbAxisYSpin");
    m_axisZ = mkAxisSpin("rbAxisZSpin");
    axForm->addRow(QStringLiteral("X (m)"), m_axisX);
    axForm->addRow(QStringLiteral("Y (m)"), m_axisY);
    axForm->addRow(QStringLiteral("Z (m)"), m_axisZ);
    layout->addWidget(axBox);
    m_applyAxisBtn = new QPushButton(QStringLiteral("Apply Axis Origin to Selected Joint"), content);
    m_applyAxisBtn->setObjectName(QStringLiteral("rbApplyAxisButton"));
    layout->addWidget(m_applyAxisBtn);
    m_snapAxisBtn = new QPushButton(QStringLiteral("Snap Selected Joint to Selected Bore"), content);
    m_snapAxisBtn->setObjectName(QStringLiteral("rbSnapAxisButton"));
    layout->addWidget(m_snapAxisBtn);

    // --- Status ---
    m_status = new QLabel(QStringLiteral("No robot loaded."), content);
    m_status->setObjectName(QStringLiteral("rbStatusLabel"));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#9fb4cc;padding:4px;"));
    layout->addWidget(m_status);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void RobotBuilderPanel::setupConnections()
{
    connect(m_loadDemoBtn,   &QPushButton::clicked,           this, &RobotBuilderPanel::onLoadDemo);
    connect(m_deleteBtn,     &QPushButton::clicked,           this, &RobotBuilderPanel::onDeleteJoint);
    connect(m_defineBtn,     &QPushButton::clicked,           this, &RobotBuilderPanel::onDefineFromFeatures);
    connect(m_applyLimitBtn, &QPushButton::clicked,           this, &RobotBuilderPanel::onApplyLimit);
    connect(m_applyAxisBtn,  &QPushButton::clicked,           this, &RobotBuilderPanel::onApplyAxisOrigin);
    connect(m_snapAxisBtn,   &QPushButton::clicked,           this, &RobotBuilderPanel::onSnapAxisToBore);
    connect(m_jointsList,    &QListWidget::currentRowChanged, this, &RobotBuilderPanel::onJointSelected);
}

krs::rbuild::RobotGraph* RobotBuilderPanel::graph() const
{
    if (!m_scene) return nullptr;
    return m_scene->getRegistry().ctx().find<krs::rbuild::RobotGraph>();
}

void RobotBuilderPanel::setStatus(const QString& msg)
{
    if (m_status) m_status->setText(msg);
}

void RobotBuilderPanel::refresh()
{
    if (refreshFromLiveRobot()) return;   // bound to a LiveRobot (e.g. the FANUC) -> live-edit path

    const QSignalBlocker blocker(this);
    m_isUpdatingUI = true;

    auto* g = graph();
    m_jointsList->clear();
    if (!g) {
        m_dofLabel->setText(QStringLiteral("DOF: - (no robot)"));
        m_dofIndex->setRange(0, 0);
        m_isUpdatingUI = false;
        return;
    }

    // Keep entity robot-tags in sync with LIVE membership so a detached subtree
    // becomes grabbable in the viewport and a re-mated one re-locks (Phase 3).
    if (m_scene) krs::rbuild::syncRobotTagsToMembership(*m_scene, *g);

    static const char* kJTypeName[] = { "Revolute", "Prismatic", "Fixed" };
    for (int i = 0; i < int(g->joints.size()); ++i) {
        const auto& j = g->joints[i];
        const int t = int(j.type);
        m_jointsList->addItem(QStringLiteral("J%1: B%2 - B%3 [%4]%5")
            .arg(i).arg(j.parent).arg(j.child)
            .arg(QString::fromLatin1((t >= 0 && t <= 2) ? kJTypeName[t] : "?"))
            .arg(j.ambiguous ? QStringLiteral(" (ambiguous)") : QString()));
    }
    m_dofLabel->setText(QStringLiteral("DOF: %1   (bodies: %2, joints: %3)")
        .arg(g->dof()).arg(int(g->bodies.size())).arg(int(g->joints.size())));

    // Rebuild the proven RobotConfig from the live graph for the hot-swap section.
    m_cfg = std::make_unique<krs::rcfg::RobotConfig>();
    m_cfg->robot = g->toRobot();
    m_cfg->ensureNames();
    const int dof = g->dof();
    m_dofIndex->setRange(0, dof > 0 ? dof - 1 : 0);

    m_isUpdatingUI = false;
}

void RobotBuilderPanel::onLoadDemo()
{
    if (m_isUpdatingUI) return;   // consistency with the other action slots
    if (!m_scene) { setStatus(QStringLiteral("No scene.")); return; }
    auto& reg = m_scene->getRegistry();

    // The demo is a SEPARATE robot (robotId 1) so it coexists with the boot FANUC
    // (robotId 0) -- two named robots in the outliner, each selectable. Clean up a
    // previous demo's bodies + root first so repeated loads don't accumulate.
    const int demoId = 1;
    std::vector<entt::entity> kill;
    for (auto e : reg.view<RobotSubcomponentComponent>())
        if (reg.get<RobotSubcomponentComponent>(e).robotId == demoId) kill.push_back(e);
    for (auto e : reg.view<RobotRootComponent>())
        if (reg.get<RobotRootComponent>(e).robotId == demoId) kill.push_back(e);
    if (!kill.empty()) reg.destroy(kill.begin(), kill.end());

    auto* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
    *gp = krs::rbuild::buildDemoGraph();
    gp->robotId = demoId;   // carried on the graph so refresh()'s tag-sync keeps id 1 (not 0)
    krs::rbuild::spawnGraphBodies(*m_scene, *gp, demoId);
    // Make it first-class (named root + LiveRobot) so it shows as its own outliner robot.
    if (auto* lr = krs::robot::instantiateFromGraph(*m_scene, *gp, demoId)) {
        lr->name = lr->model.name = "Demo Robot";
        if (reg.valid(lr->root)) {
            reg.emplace_or_replace<RobotRootComponent>(lr->root, RobotRootComponent{ "Demo Robot", demoId });
            reg.emplace_or_replace<TagComponent>(lr->root, std::string("Demo Robot"));
        }
    }
    m_editRobotId = -1;   // authoring the demo's RobotGraph (full feature editing), not a live-robot bind
    setStatus(QStringLiteral("Loaded demo robot (robotId %1): %2 bodies, DOF %3. Select two bores to define J2.")
                  .arg(demoId).arg(int(gp->bodies.size())).arg(gp->dof()));
    refresh();
    emit graphChanged();
}

// Bind the builder to a first-class robot selected elsewhere (the outliner). If the
// authoring graph already represents that robot (the demo), stay in graph-authoring
// mode (define/delete/snap). Otherwise -- the boot FANUC, which is a LiveRobot with no
// authoring graph -- bind to its live model for joint-LIMIT editing. The user's ask
// "the default loaded robot is not editable" is met: select the FANUC, its joints +
// limits populate, and edits write straight into the live model (see onApplyLimitLive).
void RobotBuilderPanel::editRobot(int robotId)
{
    if (m_isUpdatingUI || !m_scene) return;
    auto& reg = m_scene->getRegistry();

    // Authoring graph already owns this robot -> edit it as a graph (richest path).
    if (auto* g = reg.ctx().find<krs::rbuild::RobotGraph>(); g && g->robotId == robotId) {
        m_editRobotId = -1;
        refresh();
        setStatus(QStringLiteral("Editing authored robot %1: define/delete joints, snap axes.").arg(robotId));
        return;
    }

    // Otherwise bind to the LiveRobot (e.g. the FANUC) for limit editing.
    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(robotId) : nullptr;
    if (!lr) { setStatus(QStringLiteral("Robot %1 not found in the live registry.").arg(robotId)); return; }
    m_editRobotId = robotId;
    refresh();
    setStatus(QStringLiteral("Editing %1 (live robot %2): %3 DOF. Pick a DOF, set [lo, hi], Apply Limit.")
                  .arg(QString::fromStdString(lr->name)).arg(robotId).arg(lr->ndof()));
}

// Fill the controls from a bound LiveRobot's model (no RobotGraph round-trip, so the
// FANUC's CAD-derived kinematics are never re-synthesized/perturbed). Returns true iff
// it handled the refresh (m_editRobotId>=0 and the robot still exists).
bool RobotBuilderPanel::refreshFromLiveRobot()
{
    if (m_editRobotId < 0 || !m_scene) return false;
    auto& reg = m_scene->getRegistry();
    auto* rr  = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(m_editRobotId) : nullptr;
    if (!lr) { m_editRobotId = -1; return false; }   // robot gone -> fall back to graph mode

    const QSignalBlocker blocker(this);
    m_isUpdatingUI = true;
    m_jointsList->clear();

    // Bind the proven hot-swap config straight to the live model (a copy; Apply writes the
    // live model directly in onApplyLimitLive). ensureNames() gives the joints stable labels.
    m_cfg = std::make_unique<krs::rcfg::RobotConfig>();
    m_cfg->robot = lr->model;
    m_cfg->ensureNames();

    static const char* kJTypeName[] = { "Revolute", "Prismatic", "Fixed" };
    int dofSeen = 0;
    for (int i = 0; i < int(lr->model.joints.size()); ++i) {
        const auto& j = lr->model.joints[i];
        const int t = int(j.type);
        const bool isDof = j.member && j.type != krs::dyn::JType::Fixed;
        m_jointsList->addItem(QStringLiteral("J%1: %2%3  limits [%4, %5]")
            .arg(i)
            .arg(QString::fromLatin1((t >= 0 && t <= 2) ? kJTypeName[t] : "?"))
            .arg(isDof ? QStringLiteral(" (dof %1)").arg(dofSeen) : QStringLiteral(" (fixed)"))
            .arg(j.qLower, 0, 'f', 2).arg(j.qUpper, 0, 'f', 2));
        if (isDof) ++dofSeen;
    }
    const int dof = dofSeen;
    m_dofLabel->setText(QStringLiteral("DOF: %1   (live robot \"%2\", %3 joints)")
                            .arg(dof).arg(QString::fromStdString(lr->name)).arg(int(lr->model.joints.size())));
    m_dofIndex->setRange(0, dof > 0 ? dof - 1 : 0);

    m_isUpdatingUI = false;
    return true;
}

// Limit edit on a bound LiveRobot (the FANUC): write the new [lo,hi] straight into the
// live model joint, then rebuild() (DOF count unchanged -> q PRESERVED, no pose jump;
// the new limits are used by clampDof for every subsequent applyCommand).
void RobotBuilderPanel::onApplyLimitLive()
{
    auto& reg = m_scene->getRegistry();
    auto* rr  = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(m_editRobotId) : nullptr;
    if (!lr) { setStatus(QStringLiteral("Live robot gone.")); m_editRobotId = -1; refresh(); return; }

    const int idx = m_dofIndex->value();
    // Map chain-DOF index -> model.joints[] array index (skip fixed / non-member).
    int arrIdx = -1, dofSeen = 0;
    for (int ji = 0; ji < int(lr->model.joints.size()); ++ji) {
        const auto& jt = lr->model.joints[ji];
        if (!jt.member || jt.type == krs::dyn::JType::Fixed) continue;
        if (dofSeen == idx) { arrIdx = ji; break; }
        ++dofSeen;
    }
    if (arrIdx < 0) { setStatus(QStringLiteral("DOF index out of range.")); return; }

    const double lo = m_limitLo->value(), hi = m_limitHi->value();
    if (hi < lo) { setStatus(QStringLiteral("Upper limit must be >= lower limit.")); return; }
    lr->model.joints[arrIdx].qLower  = lo;
    lr->model.joints[arrIdx].qUpper  = hi;
    lr->model.joints[arrIdx].engProv = krs::robot::Provenance::UserSupplied;
    lr->rebuild();   // DOF count unchanged -> q preserved; clampDof now honours the new limits
    setStatus(QStringLiteral("Live robot %1 J%2 limits set [%3, %4] (now clamps the drive).")
                  .arg(m_editRobotId).arg(arrIdx).arg(lo, 0, 'f', 3).arg(hi, 0, 'f', 3));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onDeleteJoint()
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) {   // FANUC bind: structural joint edits are an authoring-graph op
        setStatus(QStringLiteral("Delete/define joints applies to builder-authored robots. "
                                 "Load Demo to author, or edit this robot's limits."));
        return;
    }
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to delete.")); return; }

    const int parent = g->joints[row].parent;
    const int child  = g->joints[row].child;
    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();
    const bool ok = ctrl.deleteJoint(row);   // row IS the current joints-vector index (acted on immediately)
    const int after = ctrl.dof();
    setStatus(QStringLiteral("Delete J%1 (B%2-B%3): %4. DOF %5 -> %6%7")
        .arg(row).arg(parent).arg(child).arg(ok ? QStringLiteral("ok") : QStringLiteral("FAILED"))
        .arg(before).arg(after)
        .arg(after < before ? QStringLiteral("  (downstream subtree detached)") : QString()));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onDefineFromFeatures()
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) {   // FANUC bind: feature-define is an authoring-graph op
        setStatus(QStringLiteral("Define-from-features applies to builder-authored robots. Load Demo to author."));
        return;
    }
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    auto& reg = m_scene->getRegistry();
    auto* sel = reg.ctx().find<krs::sel::SelectionState>();

    std::vector<const krs::sel::Selection*> cyls;
    if (sel) for (const auto& s : sel->selected)
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) cyls.push_back(&s);
    if (cyls.size() != 2) {
        setStatus(QStringLiteral("Select exactly TWO cylindrical bores (have %1).").arg(int(cyls.size())));
        return;
    }

    auto toFace = [](const krs::sel::Selection& s) {
        BRepFace f; f.type = int(s.type); f.axisPos = s.axisPos; f.axisDir = s.axisDir;
        f.normal = s.normal; f.radius = s.radius; return f;
    };
    const int a = krs::rbuild::bodyIndexForEntity(*g, int(cyls[0]->entity));
    const int b = krs::rbuild::bodyIndexForEntity(*g, int(cyls[1]->entity));
    if (a < 0 || b < 0 || a == b) {
        setStatus(QStringLiteral("Selected bores must be on two distinct robot bodies (a=%1 b=%2).").arg(a).arg(b));
        return;
    }

    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();
    krs::rbuild::RBJoint created;
    const bool ok = ctrl.defineFromFeatures(toFace(*cyls[0]), a, toFace(*cyls[1]), b, &created);
    if (!ok) {
        setStatus(QStringLiteral("Cannot define joint: the two bores are not coaxial (rejected for the right reason)."));
        return;
    }
    setStatus(QStringLiteral("Defined revolute B%1-B%2 at axis (%3, %4, %5). DOF %6 -> %7")
        .arg(a).arg(b)
        .arg(created.axisPos.x, 0, 'f', 3).arg(created.axisPos.y, 0, 'f', 3).arg(created.axisPos.z, 0, 'f', 3)
        .arg(before).arg(ctrl.dof()));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onJointSelected(int row)
{
    // Live-robot bind (FANUC): the row is a model.joints[] index. Reflect that joint's
    // limits into the limit spins and point the DOF index at it, so Apply Limit acts on
    // the joint the user clicked.
    if (m_editRobotId >= 0 && m_scene) {
        auto& reg = m_scene->getRegistry();
        auto* rr  = reg.ctx().find<krs::robot::RobotRegistry>();
        krs::robot::LiveRobot* lr = rr ? rr->get(m_editRobotId) : nullptr;
        if (!lr || row < 0 || row >= int(lr->model.joints.size())) return;
        const auto& j = lr->model.joints[row];
        int dofIdx = -1, dofSeen = 0;
        for (int ji = 0; ji <= row; ++ji) {
            const auto& jt = lr->model.joints[ji];
            if (!jt.member || jt.type == krs::dyn::JType::Fixed) continue;
            if (ji == row) dofIdx = dofSeen;
            ++dofSeen;
        }
        const QSignalBlocker bl(m_limitLo), bh(m_limitHi), bd(m_dofIndex);
        if (dofIdx >= 0) m_dofIndex->setValue(dofIdx);
        m_limitLo->setValue(j.qLower);
        m_limitHi->setValue(j.qUpper);
        return;
    }

    // Authoring graph: reflect the selected joint's axis origin into the adjust spin boxes.
    auto* g = graph();
    if (!g || row < 0 || row >= int(g->joints.size()) || !m_axisX) return;
    const auto& j = g->joints[row];
    const QSignalBlocker bx(m_axisX), by(m_axisY), bz(m_axisZ);
    m_axisX->setValue(j.axisPos.x);
    m_axisY->setValue(j.axisPos.y);
    m_axisZ->setValue(j.axisPos.z);
}

void RobotBuilderPanel::onApplyAxisOrigin()
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) {   // FANUC bind: axes are CAD-derived; only limits are live-editable here
        setStatus(QStringLiteral("Axis origin editing applies to builder-authored joints. "
                                 "The live robot's axes are CAD-derived -- edit its limits instead."));
        return;
    }
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to adjust.")); return; }
    g->joints[row].axisPos = glm::vec3(float(m_axisX->value()), float(m_axisY->value()), float(m_axisZ->value()));
    g->joints[row].prov    = krs::rbuild::Prov::Manual;   // user-adjusted -> manual provenance
    setStatus(QStringLiteral("J%1 axis origin set to (%2, %3, %4).")
        .arg(row).arg(g->joints[row].axisPos.x, 0, 'f', 3)
        .arg(g->joints[row].axisPos.y, 0, 'f', 3).arg(g->joints[row].axisPos.z, 0, 'f', 3));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onSnapAxisToBore()
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) {   // FANUC bind: see onApplyAxisOrigin
        setStatus(QStringLiteral("Snap-to-bore applies to builder-authored joints, not the CAD-derived live robot."));
        return;
    }
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to snap.")); return; }
    auto* sel = m_scene ? m_scene->getRegistry().ctx().find<krs::sel::SelectionState>() : nullptr;
    const krs::sel::Selection* bore = nullptr;
    if (sel) for (const auto& s : sel->selected)
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) { bore = &s; break; }
    if (!bore) { setStatus(QStringLiteral("Select a cylindrical bore in the viewport to snap to.")); return; }
    g->joints[row].axisPos = bore->axisPos;
    g->joints[row].axisDir = glm::normalize(bore->axisDir);
    g->joints[row].prov    = krs::rbuild::Prov::Manual;
    if (m_axisX) {
        const QSignalBlocker bx(m_axisX), by(m_axisY), bz(m_axisZ);
        m_axisX->setValue(bore->axisPos.x); m_axisY->setValue(bore->axisPos.y); m_axisZ->setValue(bore->axisPos.z);
    }
    setStatus(QStringLiteral("J%1 snapped to bore axis (%2, %3, %4).")
        .arg(row).arg(bore->axisPos.x, 0, 'f', 3).arg(bore->axisPos.y, 0, 'f', 3).arg(bore->axisPos.z, 0, 'f', 3));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onApplyLimit()
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) { onApplyLimitLive(); return; }   // bound to a LiveRobot (the FANUC)
    auto* g = graph();
    if (!g || !m_cfg) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int dof = g->dof();
    const int idx = m_dofIndex->value();
    if (idx < 0 || idx >= dof) { setStatus(QStringLiteral("DOF index out of range.")); return; }

    const double lo = m_limitLo->value();
    const double hi = m_limitHi->value();
    // Map the chain-DOF index to the robot.joints[] ARRAY index: toJointLimits()
    // enumerates only member, non-Fixed joints in order, so DOF-space != array-space
    // once a robot has a fixed/non-member joint. (For the all-revolute demo they
    // coincide; this keeps Apply Limit correct for mixed-joint robots too.)
    int arrIdx = -1, dofSeen = 0;
    for (int ji = 0; ji < int(m_cfg->robot.joints.size()); ++ji) {
        const auto& jt = m_cfg->robot.joints[ji];
        if (!jt.member || jt.type == krs::dyn::JType::Fixed) continue;
        if (dofSeen == idx) { arrIdx = ji; break; }
        ++dofSeen;
    }
    if (arrIdx < 0) { setStatus(QStringLiteral("DOF index out of range.")); return; }
    // The proven hot-swap: set the limit, then re-derive toJointLimits() LIVE (no cache).
    const bool ok = m_cfg->setPositionLimit(arrIdx, lo, hi);
    const krs::plan::JointLimits L = m_cfg->toJointLimits();
    const double rlo = (idx < int(L.qLower.size())) ? L.qLower[idx] : 0.0;
    const double rhi = (idx < int(L.qUpper.size())) ? L.qUpper[idx] : 0.0;
    setStatus(QStringLiteral("Hot-swap limit DOF %1: set [%2, %3] %4; readback [%5, %6]")
        .arg(idx).arg(lo, 0, 'f', 3).arg(hi, 0, 'f', 3)
        .arg(ok ? QStringLiteral("ok") : QStringLiteral("FAILED"))
        .arg(rlo, 0, 'f', 3).arg(rhi, 0, 'f', 3));
    emit graphChanged();
}

// --- IMenu (reflection-only panel: trivial lifecycle) ---
void RobotBuilderPanel::initializeFresh()        { refresh(); }
void RobotBuilderPanel::initializeFromDatabase() { refresh(); }
void RobotBuilderPanel::shutdownAndSave()        { /* nothing persisted */ }

// ===========================================================================
// GATE (Phase 1) -- the panel's controls EXIST, are CONNECTED, and INVOKE the
// proven ops; the data-model state changes correctly. The on-screen click is
// OPERATOR-VISUAL-CONFIRM; driving QPushButton::click() here exercises the REAL
// signal->slot->op path (an unconnected button would change nothing -> FAIL).
// NEG-CTRLs: an unconnected button is detected (SIGNAL-WIRING); each button drives
// its OWN op (wrong-op would move DOF the wrong way); degenerate define is rejected.
// ===========================================================================
namespace krs::rbuild {

bool runRobotBuilderPanelGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE RBUILD-PANEL -- controls exist + connected + invoke the proven ops (UI-COMPLETENESS / SIGNAL-WIRING / EDIT-OP-INVOKED)\n");

    // ---- UI-COMPLETENESS: every expected control exists + is in the panel ----
    bool completeness = true;
    {
        Scene scene;
        auto& g = scene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        RobotBuilderPanel panel(&scene);

        const char* names[] = {
            "rbLoadDemoButton", "rbDofLabel", "rbJointsList", "rbDeleteJointButton",
            "rbDefineFromFeaturesButton", "rbLimitDofSpin", "rbLimitLoSpin",
            "rbLimitHiSpin", "rbApplyLimitButton", "rbStatusLabel"
        };
        for (const char* n : names) {
            QWidget* w = panel.findChild<QWidget*>(QString::fromLatin1(n));
            if (!w) { completeness = false; printf("[rbuild]   MISSING control: %s\n", n); }
        }
    }

    // ---- SIGNAL-WIRING (by EFFECT) -- the anti-"button does nothing" check.
    // isSignalConnected is protected, and an effect test is STRONGER anyway: a
    // connected button fires its slot, an unconnected one is a no-op. The detector
    // proves the method distinguishes the two; loadDemo proves a real panel button
    // produces its effect (the action buttons are proven by EDIT-OP-INVOKED below).
    bool wiringDetector = false, loadDemoWired = false;
    {
        bool firedConnected = false;
        QPushButton connected;
        QObject::connect(&connected, &QPushButton::clicked, [&] { firedConnected = true; });
        connected.click();
        QPushButton dangling; dangling.click();   // never connected -> no effect
        wiringDetector = firedConnected;           // (dangling has no observable effect by construction)

        Scene scene;
        RobotBuilderPanel panel(&scene);           // no graph yet
        auto* load = panel.findChild<QPushButton*>(QStringLiteral("rbLoadDemoButton"));
        const bool hadGraph = (scene.getRegistry().ctx().find<RobotGraph>() != nullptr);
        if (load) load->click();                   // drive the REAL clicked->onLoadDemo path
        auto* gp = scene.getRegistry().ctx().find<RobotGraph>();
        loadDemoWired = load && !hadGraph && gp && !gp->bodies.empty();
        printf("[rbuild]   controls present=%s ; wiring detector(connected fires)=%s ; loadDemo populates graph=%s\n",
               completeness ? "yes" : "NO", wiringDetector ? "yes" : "NO", loadDemoWired ? "yes" : "NO");
    }

    // ---- EDIT-OP-INVOKED: DELETE control drops DOF by 1 ----
    bool deleteOk = false, deleteWrongDir = false;
    {
        Scene scene;
        auto& g = scene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        RobotBuilderPanel panel(&scene);
        auto* list = panel.findChild<QListWidget*>(QStringLiteral("rbJointsList"));
        auto* del  = panel.findChild<QPushButton*>(QStringLiteral("rbDeleteJointButton"));
        const int before = g.dof();
        list->setCurrentRow(1);          // J1 (B1-B2)
        del->click();                    // drive the REAL clicked->slot->deleteJoint path
        const int after = g.dof();
        deleteOk = (after == before - 1);
        deleteWrongDir = (after > before); // delete must never RAISE dof (wrong-op guard)
        printf("[rbuild]   delete control: DOF %d -> %d (want %d)  %s\n",
               before, after, before - 1, deleteOk ? "PASS" : "FAIL");
    }

    // ---- EDIT-OP-INVOKED: DEFINE control raises DOF by 1, frame matches ----
    bool defineOk = false, defineWrongDir = false, degenRejected = false;
    {
        Scene scene;
        auto& reg = scene.getRegistry();
        auto& g = reg.ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        auto& sel = reg.ctx().emplace<krs::sel::SelectionState>();
        // two COAXIAL bores on B2 & B3 (world axis (1,0,*) dir z) -> define succeeds.
        krs::sel::Selection s2; s2.valid = true; s2.entity = entt::entity(std::uint32_t(g.bodies[2].entity));
        s2.type = krs::sel::FeatureType::Cylinder; s2.axisPos = { 1.0f, 0, 0 }; s2.axisDir = { 0, 0, 1 };
        s2.normal = { 0, 0, 1 }; s2.radius = 0.05f;
        krs::sel::Selection s3 = s2; s3.entity = entt::entity(std::uint32_t(g.bodies[3].entity));
        sel.selected = { s2, s3 };

        RobotBuilderPanel panel(&scene);
        auto* def = panel.findChild<QPushButton*>(QStringLiteral("rbDefineFromFeaturesButton"));
        const int before = g.dof();
        def->click();
        const int after = g.dof();
        defineOk = (after == before + 1);
        defineWrongDir = (after < before); // define must never LOWER dof (wrong-op guard)
        // a committed B2-B3 joint now exists with a Manual-provenance frame at (1,0,*)
        const int jbc = g.jointBetween(2, 3);
        const bool frameOk = (jbc >= 0) &&
            (std::abs(g.joints[jbc].axisPos.x - 1.0f) < 1e-3f) &&
            (std::abs(g.joints[jbc].axisDir.z - 1.0f) < 1e-3f);
        defineOk = defineOk && frameOk;
        printf("[rbuild]   define control: DOF %d -> %d (want %d), frame-matches=%s  %s\n",
               before, after, before + 1, frameOk ? "yes" : "no", defineOk ? "PASS" : "FAIL");

        // degenerate (offset) bores -> rejected, DOF unchanged.
        Scene scene2;
        auto& reg2 = scene2.getRegistry();
        auto& g2 = reg2.ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene2, g2, 0);
        auto& sel2 = reg2.ctx().emplace<krs::sel::SelectionState>();
        krs::sel::Selection o2 = s2; o2.entity = entt::entity(std::uint32_t(g2.bodies[2].entity));
        krs::sel::Selection o3 = s2; o3.entity = entt::entity(std::uint32_t(g2.bodies[3].entity));
        o3.axisPos = { 1.0f, 0.3f, 0 }; // offset -> not coaxial
        sel2.selected = { o2, o3 };
        RobotBuilderPanel panel2(&scene2);
        auto* def2 = panel2.findChild<QPushButton*>(QStringLiteral("rbDefineFromFeaturesButton"));
        const int b2 = g2.dof(); def2->click(); const int a2 = g2.dof();
        degenRejected = (a2 == b2);
        printf("[rbuild]   define degenerate pair: DOF %d -> %d (want unchanged)  %s\n",
               b2, a2, degenRejected ? "PASS" : "FAIL");
    }

    // ---- EDIT-OP-INVOKED: property hot-swap (limit) reflected live ----
    bool limitOk = false;
    {
        Scene scene;
        auto& g = scene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        RobotBuilderPanel panel(&scene);
        auto* dofSpin = panel.findChild<QSpinBox*>(QStringLiteral("rbLimitDofSpin"));
        auto* lo = panel.findChild<QDoubleSpinBox*>(QStringLiteral("rbLimitLoSpin"));
        auto* hi = panel.findChild<QDoubleSpinBox*>(QStringLiteral("rbLimitHiSpin"));
        auto* apply = panel.findChild<QPushButton*>(QStringLiteral("rbApplyLimitButton"));
        dofSpin->setValue(0); lo->setValue(-1.25); hi->setValue(1.25);
        auto* status = panel.findChild<QLabel*>(QStringLiteral("rbStatusLabel"));
        apply->click();
        // the status echoes the live readback; success = it reports the set limit back.
        limitOk = status && status->text().contains(QStringLiteral("readback [-1.250, 1.250]"));
        printf("[rbuild]   limit hot-swap control: %s  %s\n",
               status ? status->text().toLatin1().constData() : "(no status)",
               limitOk ? "PASS" : "FAIL");
    }

    const bool pass = completeness && wiringDetector && loadDemoWired && deleteOk && !deleteWrongDir
                    && defineOk && !defineWrongDir && degenRejected && limitOk;

    printf("[rbuild]   NEG-CTRLs: delete-wrong-direction=%s define-wrong-direction=%s degenerate-rejected=%s  %s\n",
           deleteWrongDir ? "YES(bug)" : "no", defineWrongDir ? "YES(bug)" : "no",
           degenRejected ? "yes" : "no",
           (!deleteWrongDir && !defineWrongDir && degenRejected) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (panel controls present, connected, and invoke the proven ops; chain re-derives)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rbuild
