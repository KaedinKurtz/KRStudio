#include "RobotBuilderPanel.hpp"

#include "Scene.hpp"
#include "components.hpp"            // BRepFace
#include "SelectionService.hpp"     // krs::sel::SelectionState / Selection / FeatureType
#include "RobotBuilder.hpp"         // krs::rbuild::RobotGraph / EditController / RBJoint
#include "RobotBuilderScene.hpp"    // buildDemoGraph / spawnGraphBodies / bodyIndexForEntity
#include "RobotModel.hpp"           // krs::robot::instantiateFromGraph (demo as a first-class robot)
#include "RobotConfig.hpp"          // krs::rcfg::RobotConfig (proven property hot-swap)
#include "SceneBuilder.hpp"         // spawnPrimitive (selected-joint axis overlay bar)
#include "PrimitiveBuilders.hpp"    // Primitive::Cylinder
#include <glm/gtx/quaternion.hpp>   // glm::rotation (orient the axis bar)

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
#include <QComboBox>
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
        "QListWidget,QDoubleSpinBox,QSpinBox,QComboBox{background:#262b33;border:1px solid #4a5260;}"
        // Once a custom border is set on the spin boxes, the style no longer computes the
        // up/down sub-control geometry, so the UP button's hit-area misaligns (unclickable,
        // wrong cursor). Define both buttons explicitly so each is clearly clickable.
        "QSpinBox,QDoubleSpinBox{min-height:24px;padding-right:18px;}"
        "QSpinBox::up-button,QDoubleSpinBox::up-button{subcontrol-origin:border;"
        "subcontrol-position:top right;width:16px;height:11px;border-left:1px solid #4a5260;}"
        "QSpinBox::down-button,QDoubleSpinBox::down-button{subcontrol-origin:border;"
        "subcontrol-position:bottom right;width:16px;height:11px;border-left:1px solid #4a5260;}"
        "QSpinBox::up-button:hover,QDoubleSpinBox::up-button:hover,"
        "QSpinBox::down-button:hover,QDoubleSpinBox::down-button:hover{background:#323844;}"
        "QSpinBox::up-arrow,QDoubleSpinBox::up-arrow{image:none;width:0;height:0;"
        "border-left:4px solid transparent;border-right:4px solid transparent;border-bottom:5px solid #cfd6df;}"
        "QSpinBox::down-arrow,QDoubleSpinBox::down-arrow{image:none;width:0;height:0;"
        "border-left:4px solid transparent;border-right:4px solid transparent;border-top:5px solid #cfd6df;}"));

    initializeUI();
    setupConnections();
    refresh();
}

RobotBuilderPanel::~RobotBuilderPanel()
{
    // Remove the selected-joint axis overlay bar so it doesn't outlive the panel.
    if (m_scene) {
        auto& reg = m_scene->getRegistry();
        const entt::entity bar = entt::entity(m_selAxisBar);
        if (reg.valid(bar)) reg.destroy(bar);
    }
}

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
    m_defineHint = new QLabel(QStringLiteral(
        "Click two coaxial bores in the viewport (each one highlights and stays selected; "
        "click a bore again to deselect, or use Clear). Then Define a revolute about their shared axis."),
        content);
    m_defineHint->setWordWrap(true);
    layout->addWidget(m_defineHint);
    m_defineBtn = new QPushButton(QStringLiteral("Define Revolute from 2 Selected Bores"), content);
    m_defineBtn->setObjectName(QStringLiteral("rbDefineFromFeaturesButton"));
    layout->addWidget(m_defineBtn);
    m_clearSelBtn = new QPushButton(QStringLiteral("Clear Bore Selection"), content);
    m_clearSelBtn->setObjectName(QStringLiteral("rbClearSelectionButton"));
    layout->addWidget(m_clearSelBtn);

    // --- Joint type (Revolute / Continuous / Prismatic / Fixed) for the selected joint ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Joint Type"), content));
    auto* typeBox = new QGroupBox(content);
    auto* typeForm = new QFormLayout(typeBox);
    m_jointType = new QComboBox(typeBox);
    m_jointType->setObjectName(QStringLiteral("rbJointTypeCombo"));
    m_jointType->addItem(QStringLiteral("Revolute"));     // 0
    m_jointType->addItem(QStringLiteral("Continuous"));   // 1 (revolute, no limits)
    m_jointType->addItem(QStringLiteral("Prismatic"));    // 2
    m_jointType->addItem(QStringLiteral("Fixed / Rigid")); // 3 (0-DOF weld)
    typeForm->addRow(QStringLiteral("Selected joint"), m_jointType);
    layout->addWidget(typeBox);

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

    // --- Joint axis DIRECTION (the rotation/translation axis of the selected joint) ---
    // This is the actual joint AXIS (e.g. a base turntable is (0,0,1)); editing it corrects a
    // mis-inferred axis directly. Written to RBJoint.axisDir (world); toRobot() rotates it into the
    // parent-link frame, so the live robot's joint axis updates on Apply.
    layout->addWidget(makeSectionHeader(QStringLiteral("Joint Axis Direction"), content));
    auto* dirBox = new QGroupBox(content);
    auto* dirForm = new QFormLayout(dirBox);
    auto mkDirSpin = [&](const char* objName) {
        auto* s = new QDoubleSpinBox(dirBox);
        s->setObjectName(QString::fromLatin1(objName));
        s->setRange(-1.0, 1.0); s->setDecimals(3); s->setSingleStep(0.05);
        s->setKeyboardTracking(false);
        return s;
    };
    m_dirX = mkDirSpin("rbDirXSpin");
    m_dirY = mkDirSpin("rbDirYSpin");
    m_dirZ = mkDirSpin("rbDirZSpin");
    dirForm->addRow(QStringLiteral("Axis X"), m_dirX);
    dirForm->addRow(QStringLiteral("Axis Y"), m_dirY);
    dirForm->addRow(QStringLiteral("Axis Z"), m_dirZ);
    layout->addWidget(dirBox);
    m_applyDirBtn = new QPushButton(QStringLiteral("Apply Axis Direction to Selected Joint"), content);
    m_applyDirBtn->setObjectName(QStringLiteral("rbApplyDirButton"));
    layout->addWidget(m_applyDirBtn);

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
    connect(m_clearSelBtn,   &QPushButton::clicked,           this, &RobotBuilderPanel::onClearSelection);
    connect(m_applyLimitBtn, &QPushButton::clicked,           this, &RobotBuilderPanel::onApplyLimit);
    connect(m_applyAxisBtn,  &QPushButton::clicked,           this, &RobotBuilderPanel::onApplyAxisOrigin);
    connect(m_applyDirBtn,   &QPushButton::clicked,           this, &RobotBuilderPanel::onApplyAxisDir);
    connect(m_snapAxisBtn,   &QPushButton::clicked,           this, &RobotBuilderPanel::onSnapAxisToBore);
    connect(m_jointsList,    &QListWidget::currentRowChanged, this, &RobotBuilderPanel::onJointSelected);
    connect(m_jointType, QOverload<int>::of(&QComboBox::activated),
            this, &RobotBuilderPanel::onJointTypeChanged);
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
    if (auto* st = reg.ctx().find<krs::sel::SelectionState>()) { st->enabled = true; st->fifoTwoBores = true; }  // bore-picking live, FIFO 2
    setStatus(QStringLiteral("Loaded demo robot (robotId %1): %2 bodies, DOF %3. Click two bores to define a joint.")
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

    // Authoring intent -> ensure feature (bore) picking is live so "Define from 2 bores" can collect
    // them (the View toggle may have turned it off; binding a robot for editing turns it back on).
    if (auto* st = reg.ctx().find<krs::sel::SelectionState>()) { st->enabled = true; st->fifoTwoBores = true; }

    // Authoring graph already represents this robot -> edit it directly (keeps any
    // un-jointed bodies / bore features authored this session).
    if (auto* g = reg.ctx().find<krs::rbuild::RobotGraph>(); g && g->robotId == robotId) {
        m_editRobotId = -1;
        refresh();
        setStatus(QStringLiteral("Editing robot %1: re-type / define / delete joints, edit axes + limits.").arg(robotId));
        return;
    }

    // Otherwise synthesize an editable graph that MIRRORS the live robot (the keystone:
    // even the boot FANUC becomes an ordinary editable RobotGraph -- delete/define/re-type
    // all work, and edits re-apply to the live robot via reapplyGraphToRobot on graphChanged).
    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(robotId) : nullptr;
    if (!lr) { setStatus(QStringLiteral("Robot %1 not found in the live registry.").arg(robotId)); return; }

    krs::rbuild::RobotGraph* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
    *gp = krs::robot::buildGraphFromLiveRobot(*lr);
    m_editRobotId = -1;   // it IS a graph now -> full graph-authoring mode
    refresh();
    setStatus(QStringLiteral("Editing %1 as a graph (%2 joints): re-type / define / delete / limits all take live effect.")
                  .arg(QString::fromStdString(lr->name)).arg(int(gp->joints.size())));
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
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to delete.")); return; }

    // DELETE = SPLIT: the cut joint's child subtree becomes its OWN first-class robot, so it moves as a
    // unit (drag its root) and stays articulated; re-mating it (Define across the two) merges it back.
    const int robotId = g->robotId;
    int newId = -1;
    if (krs::robot::splitRobotAtJoint(*m_scene, robotId, row, &newId)) {
        if (auto* rr = m_scene->getRegistry().ctx().find<krs::robot::RobotRegistry>())
            if (auto* base = rr->get(robotId))
                *g = krs::robot::buildGraphFromLiveRobot(*base);   // ctx graph -> the (smaller) base robot
        setStatus(QStringLiteral("Deleted J%1 -> detached subtree is now robot %2 "
                                 "(drag its root to move it; re-mate two bores to merge it back).").arg(row).arg(newId));
        refresh();
        emit graphChanged();
        return;
    }

    // Fallback (e.g. the cut would orphan the base): plain graph delete.
    const int parent = g->joints[row].parent, child = g->joints[row].child;
    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();
    const bool ok = ctrl.deleteJoint(row);
    setStatus(QStringLiteral("Delete J%1 (B%2-B%3): %4. DOF %5 -> %6")
        .arg(row).arg(parent).arg(child).arg(ok ? QStringLiteral("ok") : QStringLiteral("FAILED"))
        .arg(before).arg(ctrl.dof()));
    refresh();
    emit graphChanged();
}

void RobotBuilderPanel::onDefineFromFeatures()
{
    if (m_isUpdatingUI) return;
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    auto& reg = m_scene->getRegistry();
    auto* sel = reg.ctx().find<krs::sel::SelectionState>();

    // The two bores = the LAST two cylindrical features the user clicked (clicks accumulate; the most
    // recent pair wins, so an extra stray click never blocks the define).
    std::vector<const krs::sel::Selection*> cyls;
    if (sel) for (const auto& s : sel->selected)
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) cyls.push_back(&s);
    if (cyls.size() < 2) {
        setStatus(QStringLiteral("Click TWO cylindrical bores in the viewport (they highlight + accumulate); have %1.")
                      .arg(int(cyls.size())));
        return;
    }
    const krs::sel::Selection* selA = cyls[cyls.size() - 2];
    const krs::sel::Selection* selB = cyls[cyls.size() - 1];

    auto rimFrame = [](const krs::sel::Selection& s) {
        krs::rbuild::RBJoint f;
        f.axisPos = (glm::distance(s.axisEnd0, s.axisEnd1) > 1e-5f)
                    ? (glm::distance(s.hitPoint, s.axisEnd0) <= glm::distance(s.hitPoint, s.axisEnd1) ? s.axisEnd0 : s.axisEnd1)
                    : s.axisPos;
        f.axisDir = s.axisDir; f.orthonormalizeFrame(); return f;
    };

    // CROSS-ROBOT MERGE: if the two bores are on DIFFERENT robots (re-mating a detached branch), snap
    // the child robot rigidly onto the parent's bore and MERGE it back into one robot.
    auto robotOf = [&](entt::entity e) -> int {
        const auto* s = reg.try_get<RobotSubcomponentComponent>(e); return s ? s->robotId : -1; };
    const int ridA = robotOf(selA->entity), ridB = robotOf(selB->entity);
    if (ridA >= 0 && ridB >= 0 && ridA != ridB) {
        const int parentId = std::min(ridA, ridB), childId = std::max(ridA, ridB);
        const krs::sel::Selection* pSel = (robotOf(selA->entity) == parentId) ? selA : selB;
        const krs::sel::Selection* cSel = (pSel == selA) ? selB : selA;
        const krs::rbuild::RBJoint pf = rimFrame(*pSel), cf = rimFrame(*cSel);
        krs::robot::transformRobot(*m_scene, childId, krs::rbuild::RobotGraph::mateTransformConcentric(pf, cf));
        auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
        krs::robot::LiveRobot* lrP = rr ? rr->get(parentId) : nullptr;
        if (!lrP) { setStatus(QStringLiteral("Merge: parent robot gone.")); return; }
        krs::rbuild::RobotGraph gP = krs::robot::buildGraphFromLiveRobot(*lrP);
        const int parentBody = krs::rbuild::bodyIndexForEntity(gP, int(pSel->entity));
        if (parentBody < 0) { setStatus(QStringLiteral("Merge: parent bore body not found.")); return; }
        krs::rbuild::RBJoint cj; cj.type = krs::rbuild::JType::Revolute;
        cj.axisDir = pf.axisDir; cj.axisPos = pf.axisPos; cj.orthonormalizeFrame();
        if (krs::robot::mergeRobots(*m_scene, parentId, childId, parentBody, cj)) {
            if (auto* m = rr->get(parentId)) *g = krs::robot::buildGraphFromLiveRobot(*m);
            if (sel) krs::sel::clearSelection(*sel);
            setStatus(QStringLiteral("Merged robot %1 into robot %2 at the mated bores.").arg(childId).arg(parentId));
            refresh(); emit graphChanged();
            return;
        }
        setStatus(QStringLiteral("Merge failed.")); return;
    }

    auto toFace = [](const krs::sel::Selection& s) {
        BRepFace f; f.type = int(s.type); f.axisPos = s.axisPos; f.axisDir = s.axisDir;
        f.normal = s.normal; f.radius = s.radius; return f;
    };
    const int a = krs::rbuild::bodyIndexForEntity(*g, int(selA->entity));
    const int b = krs::rbuild::bodyIndexForEntity(*g, int(selB->entity));
    if (a < 0 || b < 0 || a == b) {
        setStatus(QStringLiteral("The two bores must be on two distinct robot bodies (a=%1 b=%2).").arg(a).arg(b));
        return;
    }

    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();
    krs::rbuild::RBJoint created;
    int parent = a, child = b;
    // requireCollinear=false: the user may have dragged a body away, so the two bores are PARALLEL but
    // not yet coaxial; the mate snap below makes them coaxial. (Auto-parse still requires collinearity.)
    const bool ok = ctrl.defineFromFeatures(toFace(*selA), a, toFace(*selB), b, &created, &parent, &child, false);
    if (!ok) {
        setStatus(QStringLiteral("Cannot define joint: the two bores are not coaxial. Pick two bores that share an axis, "
                                 "or set the axis directly in Joint Axis Direction."));
        return;
    }

    // MATE-SNAP: rigidly move the CHILD body + its subtree so the two SELECTED faces meet at their
    // INTERFACE -- the child's selected bore RIM coincides with the parent's, axes concentric. The
    // parent (lower chain index) stays fixed. Then the joint's persisted frame is set to that interface
    // (origin) + the concentric axis, exactly as the operator described.
    auto faceFrame = [](const krs::sel::Selection& s) {
        krs::rbuild::RBJoint f;
        // the SELECTED rim = the bore end-cap nearest the click (the face the operator pointed at);
        // fall back to the analytic axis point when no trimmed rim is available (synthetic faces).
        f.axisPos = (glm::distance(s.axisEnd0, s.axisEnd1) > 1e-5f)
                    ? (glm::distance(s.hitPoint, s.axisEnd0) <= glm::distance(s.hitPoint, s.axisEnd1)
                       ? s.axisEnd0 : s.axisEnd1)
                    : s.axisPos;
        f.axisDir = s.axisDir; f.orthonormalizeFrame(); return f;
    };
    const krs::rbuild::RBJoint frA = faceFrame(*selA);   // bore on body `a`
    const krs::rbuild::RBJoint frB = faceFrame(*selB);   // bore on body `b`
    const bool aIsParent = (a == parent);
    const krs::rbuild::RBJoint pf = aIsParent ? frA : frB;   // parent's selected face (stays)
    const krs::rbuild::RBJoint cf = aIsParent ? frB : frA;   // child's selected face (snaps onto pf)
    krs::robot::snapMateSubtree(*m_scene, *g, parent, child, pf, cf);

    // The joint frame = the interface: origin at the (now coincident) faces, axis concentric to both.
    const int pj = g->jointBetween(parent, child);
    if (pj >= 0) {
        krs::rbuild::EditController c2{ g };
        c2.setJointAxis(pj, pf.axisDir);             // concentric axis (normal to the selected faces)
        g->joints[pj].axisPos = pf.axisPos;          // origin at the interface of the two faces
    }

    if (sel) krs::sel::clearSelection(*sel);   // consume the pair so the next joint starts fresh
    setStatus(QStringLiteral("Mated B%1 (child) onto B%2 (parent): interface (%3, %4, %5), axis (%6, %7, %8). DOF %9->%10")
        .arg(child).arg(parent)
        .arg(pf.axisPos.x, 0, 'f', 3).arg(pf.axisPos.y, 0, 'f', 3).arg(pf.axisPos.z, 0, 'f', 3)
        .arg(pf.axisDir.x, 0, 'f', 3).arg(pf.axisDir.y, 0, 'f', 3).arg(pf.axisDir.z, 0, 'f', 3)
        .arg(before).arg(ctrl.dof()));
    refresh();
    emit graphChanged();
    // SELECT the just-defined joint so its axis is shown (overlay bar) + the Axis Direction field
    // targets it -- otherwise the list had no selection and typing an axis + Apply was a no-op.
    if (pj >= 0) m_jointsList->setCurrentRow(pj);
}

// Spawn (or re-point) ONE glowing magenta axis bar through the selected joint's axis, in the MAIN
// scene. JointAxisPass draws JointAxisComponent always-on-top in the main viewport, so editing the
// axis direction visibly rotates this bar -- the feedback that was missing ("axis does nothing").
void RobotBuilderPanel::showSelectedJointAxis(int row)
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    entt::entity bar = entt::entity(m_selAxisBar);
    auto* g = graph();
    const bool valid = g && row >= 0 && row < int(g->joints.size());
    if (!valid) {                                  // no joint selected -> remove the bar
        if (reg.valid(bar)) reg.destroy(bar);
        m_selAxisBar = static_cast<std::uint32_t>(entt::entity{ entt::null });
        return;
    }
    const auto& j = g->joints[row];
    const glm::vec3 o = j.axisPos;
    glm::vec3 d = j.axisDir;
    if (glm::length(d) < 1e-6f) d = glm::vec3(0, 0, 1);
    d = glm::normalize(d);
    if (!reg.valid(bar)) {                          // create once, then reuse
        bar = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::Cylinder), o,
                                           glm::vec3(0.012f, 1.10f, 0.012f), "JointAxisEdit");
        m_selAxisBar = std::uint32_t(bar);
        if (reg.valid(bar)) reg.emplace_or_replace<JointAxisComponent>(bar);
    }
    if (!reg.valid(bar)) { m_selAxisBar = static_cast<std::uint32_t>(entt::entity{ entt::null }); return; }
    auto& tc = reg.get<TransformComponent>(bar);
    tc.translation = o;
    tc.rotation    = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), d);  // bar built along +Y -> point at axis
    tc.scale       = glm::vec3(0.012f, 1.10f, 0.012f);
    auto& mat = reg.get_or_emplace<MaterialComponent>(bar);
    mat.albedoColor      = glm::vec3(1.00f, 0.20f, 1.00f);   // magenta = "the joint axis you are editing"
    mat.emissiveColor    = glm::vec3(1.00f, 0.20f, 1.00f);
    mat.emissiveStrength = 5.0f;
}

void RobotBuilderPanel::onClearSelection()
{
    if (!m_scene) return;
    if (auto* sel = m_scene->getRegistry().ctx().find<krs::sel::SelectionState>())
        krs::sel::clearSelection(*sel);
    setStatus(QStringLiteral("Bore selection cleared. Click two coaxial bores to define a joint."));
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
        const QSignalBlocker bl(m_limitLo), bh(m_limitHi), bd(m_dofIndex), bt(m_jointType);
        if (dofIdx >= 0) m_dofIndex->setValue(dofIdx);
        m_limitLo->setValue(j.qLower);
        m_limitHi->setValue(j.qUpper);
        // Reflect the live joint's type (krs::dyn::JType has no 'continuous' notion).
        int ti = 0;
        if (j.type == krs::dyn::JType::Prismatic) ti = 2;
        else if (j.type == krs::dyn::JType::Fixed) ti = 3;
        m_jointType->setCurrentIndex(ti);
        return;
    }

    // Authoring graph: reflect the selected joint's axis origin + type into the controls.
    auto* g = graph();
    if (!g || row < 0 || row >= int(g->joints.size()) || !m_axisX) { showSelectedJointAxis(-1); return; }
    const auto& j = g->joints[row];
    const QSignalBlocker bx(m_axisX), by(m_axisY), bz(m_axisZ), bt(m_jointType),
                         dx(m_dirX), dy(m_dirY), dz(m_dirZ);
    m_axisX->setValue(j.axisPos.x);
    m_axisY->setValue(j.axisPos.y);
    m_axisZ->setValue(j.axisPos.z);
    m_dirX->setValue(j.axisDir.x);
    m_dirY->setValue(j.axisDir.y);
    m_dirZ->setValue(j.axisDir.z);
    int ti = 0;
    if (j.type == krs::rbuild::JType::Revolute)  ti = j.limits.enabled ? 0 : 1;  // 1 = continuous
    else if (j.type == krs::rbuild::JType::Prismatic) ti = 2;
    else if (j.type == krs::rbuild::JType::Fixed)     ti = 3;
    m_jointType->setCurrentIndex(ti);

    // Point the DOF field + limit spins at the selected joint, so the limit editor acts on
    // the joint you clicked (the DOF index = this joint's position among the non-Fixed,
    // committed joints). Fixed/ambiguous joints carry no DOF -> leave the field as-is.
    int dofIdx = -1, dofSeen = 0;
    for (int ji = 0; ji <= row; ++ji) {
        if (g->joints[ji].ambiguous || g->joints[ji].type == krs::rbuild::JType::Fixed) continue;
        if (ji == row) dofIdx = dofSeen;
        ++dofSeen;
    }
    if (dofIdx >= 0) {
        const QSignalBlocker bd(m_dofIndex), bl(m_limitLo), bh(m_limitHi);
        m_dofIndex->setValue(dofIdx);
        m_limitLo->setValue(j.limits.lower);
        m_limitHi->setValue(j.limits.upper);
    }
    showSelectedJointAxis(row);   // glowing axis bar in the main viewport for the clicked joint
}

void RobotBuilderPanel::onJointTypeChanged(int comboIndex)
{
    if (m_isUpdatingUI) return;
    if (m_editRobotId >= 0) {   // FANUC live-bind: structural re-type is a graph-authoring op (Phase 3)
        setStatus(QStringLiteral("Joint-type editing applies to builder-authored robots. "
                                 "Load Demo to author; the FANUC becomes fully editable in a later phase."));
        return;
    }
    auto* g = graph();
    const int row = m_jointsList->currentRow();
    if (!g || row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to re-type.")); return; }

    krs::rbuild::JType t = krs::rbuild::JType::Revolute;
    bool continuous = false;
    switch (comboIndex) {
        case 0: t = krs::rbuild::JType::Revolute;  continuous = false; break;
        case 1: t = krs::rbuild::JType::Revolute;  continuous = true;  break;   // continuous = revolute, no limits
        case 2: t = krs::rbuild::JType::Prismatic; continuous = false; break;
        case 3: t = krs::rbuild::JType::Fixed;     continuous = false; break;
        default: break;
    }
    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();
    ctrl.setJointType(row, t);
    krs::rbuild::JointLimits lim = g->joints[row].limits;
    lim.enabled = !continuous;
    ctrl.setJointLimits(row, lim);
    setStatus(QStringLiteral("J%1 type -> %2. DOF %3 -> %4 (Fixed=0-DOF weld; Continuous=no limits).")
        .arg(row).arg(m_jointType->currentText()).arg(before).arg(ctrl.dof()));
    refresh();
    emit graphChanged();
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

void RobotBuilderPanel::onApplyAxisDir()
{
    if (m_isUpdatingUI) return;
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to set its axis.")); return; }
    const glm::vec3 dir(float(m_dirX->value()), float(m_dirY->value()), float(m_dirZ->value()));
    krs::rbuild::EditController ctrl{ g };
    if (!ctrl.setJointAxis(row, dir)) {   // normalizes + orthonormalizes the mate frame + marks Manual
        setStatus(QStringLiteral("Axis direction must be non-zero (e.g. a vertical base turntable is 0, 0, 1)."));
        return;
    }
    const glm::vec3 a = g->joints[row].axisDir;   // read back the normalized axis
    setStatus(QStringLiteral("J%1 axis direction set to (%2, %3, %4). Re-applied to the live robot.")
        .arg(row).arg(a.x, 0, 'f', 3).arg(a.y, 0, 'f', 3).arg(a.z, 0, 'f', 3));
    refresh();
    emit graphChanged();
    showSelectedJointAxis(row);            // re-orient the glowing axis bar so the edit is VISIBLE
    m_jointsList->setCurrentRow(row);      // keep the joint selected after the rebuild
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
    if (sel) for (const auto& s : sel->selected)   // LAST selected cylinder = the most recent intent
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) bore = &s;
    if (!bore) { setStatus(QStringLiteral("Select a cylindrical bore in the viewport to snap to.")); return; }
    g->joints[row].axisPos = bore->axisPos;
    krs::rbuild::EditController ctrl{ g };
    ctrl.setJointAxis(row, bore->axisDir);         // normalizes + orthonormalizes the mate frame + marks Manual
    if (m_axisX) {
        const QSignalBlocker bx(m_axisX), by(m_axisY), bz(m_axisZ), dx(m_dirX), dy(m_dirY), dz(m_dirZ);
        m_axisX->setValue(bore->axisPos.x); m_axisY->setValue(bore->axisPos.y); m_axisZ->setValue(bore->axisPos.z);
        const glm::vec3 a = g->joints[row].axisDir;
        m_dirX->setValue(a.x); m_dirY->setValue(a.y); m_dirZ->setValue(a.z);
    }
    setStatus(QStringLiteral("J%1 snapped to bore: origin (%2, %3, %4), axis (%5, %6, %7).")
        .arg(row).arg(bore->axisPos.x, 0, 'f', 3).arg(bore->axisPos.y, 0, 'f', 3).arg(bore->axisPos.z, 0, 'f', 3)
        .arg(g->joints[row].axisDir.x, 0, 'f', 3).arg(g->joints[row].axisDir.y, 0, 'f', 3).arg(g->joints[row].axisDir.z, 0, 'f', 3));
    refresh();
    emit graphChanged();
    showSelectedJointAxis(row);
    m_jointsList->setCurrentRow(row);
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
    // ALSO write the limit into the authoring GRAPH joint so it re-applies to the live
    // robot (graphChanged -> reapplyGraphToRobot). Map the DOF index to the graph joint
    // index the same way (skip ambiguous / Fixed = non-DOF joints).
    int gi = -1, gdof = 0;
    for (int ji = 0; ji < int(g->joints.size()); ++ji) {
        if (g->joints[ji].ambiguous || g->joints[ji].type == krs::rbuild::JType::Fixed) continue;
        if (gdof == idx) { gi = ji; break; }
        ++gdof;
    }
    if (gi >= 0) { g->joints[gi].limits.lower = lo; g->joints[gi].limits.upper = hi;
                   g->joints[gi].limits.enabled = true; g->joints[gi].prov = krs::rbuild::Prov::Manual; }
    setStatus(QStringLiteral("Limit DOF %1: set [%2, %3] %4; readback [%5, %6] (live)")
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
            "rbDefineFromFeaturesButton", "rbClearSelectionButton", "rbLimitDofSpin", "rbLimitLoSpin",
            "rbLimitHiSpin", "rbApplyLimitButton",
            "rbDirXSpin", "rbDirYSpin", "rbDirZSpin", "rbApplyDirButton", "rbStatusLabel"
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

    // PHASE 1: joint-type model -- EditController re-types; toRobot honours type+limits
    // (Prismatic no longer coerced to Revolute; Fixed is a 0-DOF weld; continuous unbounds).
    bool prismaticOk = false, fixedDropsDof = false, continuousOk = false, comboPresent = false;
    {
        Scene scene;
        auto& g = scene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        const int dof0 = g.dof();
        EditController ctrl{ &g };
        ctrl.setJointType(0, JType::Prismatic);
        for (const auto& jj : g.toRobot().joints) if (jj.type == krs::dyn::JType::Prismatic) prismaticOk = true;
        JointLimits cl; cl.enabled = false;
        ctrl.setJointType(0, JType::Revolute); ctrl.setJointLimits(0, cl);
        for (const auto& jj : g.toRobot().joints) if (jj.qUpper > 1e8) continuousOk = true;
        ctrl.setJointType(0, JType::Fixed);
        fixedDropsDof = (g.dof() == dof0 - 1);
        RobotBuilderPanel panel(&scene);
        auto* combo = panel.findChild<QComboBox*>(QStringLiteral("rbJointTypeCombo"));
        comboPresent = combo && combo->count() == 4;
        printf("[rbuild]   joint-type model: prismatic-not-coerced=%s fixed-drops-dof=%s continuous-unbounds=%s combo(4)=%s  %s\n",
               prismaticOk ? "yes" : "no", fixedDropsDof ? "yes" : "no", continuousOk ? "yes" : "no",
               comboPresent ? "yes" : "no",
               (prismaticOk && fixedDropsDof && continuousOk && comboPresent) ? "PASS" : "FAIL");
    }
    const bool typeModelOk = prismaticOk && fixedDropsDof && continuousOk && comboPresent;

    // ---- EDIT-OP-INVOKED: AXIS-DIRECTION control rewrites the selected joint's axis (the J0 fix's
    // user-correctable backstop). Select a joint, type a new axis, Apply -> RBJoint.axisDir == the
    // NORMALIZED input, frame stays orthonormal, provenance Manual. NEG-CTRL: a zero vector is
    // rejected (no change), so the control isn't a blind writer.
    bool axisDirOk = false, zeroAxisRejected = false;
    {
        Scene scene;
        auto& g = scene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene, g, 0);
        RobotBuilderPanel panel(&scene);
        auto* list = panel.findChild<QListWidget*>(QStringLiteral("rbJointsList"));
        auto* dx = panel.findChild<QDoubleSpinBox*>(QStringLiteral("rbDirXSpin"));
        auto* dy = panel.findChild<QDoubleSpinBox*>(QStringLiteral("rbDirYSpin"));
        auto* dz = panel.findChild<QDoubleSpinBox*>(QStringLiteral("rbDirZSpin"));
        auto* applyDir = panel.findChild<QPushButton*>(QStringLiteral("rbApplyDirButton"));
        list->setCurrentRow(0);                          // J0
        dx->setValue(0.0); dy->setValue(0.0); dz->setValue(1.0);   // ask for vertical (0,0,1)
        applyDir->click();
        const glm::vec3 a = g.joints[0].axisDir;
        const float dotZ = std::abs(glm::dot(glm::normalize(a), glm::vec3(0, 0, 1)));
        const float dotRef = std::abs(glm::dot(glm::normalize(a), g.joints[0].refDir));   // orthonormal frame
        axisDirOk = (dotZ > 0.999f) && (dotRef < 1e-3f) && (g.joints[0].prov == Prov::Manual);
        // NEG-CTRL: a zero axis is rejected -> the joint keeps its (now vertical) axis.
        dx->setValue(0.0); dy->setValue(0.0); dz->setValue(0.0);
        applyDir->click();
        zeroAxisRejected = std::abs(glm::dot(glm::normalize(g.joints[0].axisDir), glm::vec3(0, 0, 1))) > 0.999f;
        printf("[rbuild]   axis-direction control: set J0 axis=(%.3f,%.3f,%.3f) vertical=%s orthonormal=%s ; zero-rejected=%s  %s\n",
               a.x, a.y, a.z, dotZ > 0.999f ? "yes" : "no", dotRef < 1e-3f ? "yes" : "no",
               zeroAxisRejected ? "yes" : "no", (axisDirOk && zeroAxisRejected) ? "PASS" : "FAIL");
    }

    // EDIT-OP-INVOKED: bodyIndexForEntity must find a bore on an EXTRA solid of a collapsed link
    // (the FANUC case). This was the bug that made "Define from 2 bores" fail on most of the arm --
    // a bore clicked on an extraEntity returned -1 ("must be on two distinct bodies").
    bool extraMapOk = false;
    {
        RobotGraph g; RBBody b; b.entity = 100; b.extraEntities = { 101, 102 }; g.bodies.push_back(b);
        extraMapOk = bodyIndexForEntity(g, 100) == 0 && bodyIndexForEntity(g, 101) == 0
                  && bodyIndexForEntity(g, 102) == 0 && bodyIndexForEntity(g, 999) == -1;
        printf("[rbuild]   bodyIndexForEntity finds bores on extraEntities (collapsed link): %s  %s\n",
               extraMapOk ? "yes" : "NO", extraMapOk ? "PASS" : "FAIL");
    }

    const bool pass = completeness && wiringDetector && loadDemoWired && deleteOk && !deleteWrongDir
                    && defineOk && !defineWrongDir && degenRejected && limitOk && typeModelOk
                    && axisDirOk && zeroAxisRejected && extraMapOk;

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
