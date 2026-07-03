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
#include <QSlider>
#include <QTimer>
#include <QMessageBox>
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
    // Honest label: cutting a joint does not just remove an edge -- the child subtree becomes its
    // OWN robot (drag it by its root; re-mate two bores to merge it back). The old "Delete Selected
    // Joint" text hid a fairly dramatic side effect.
    m_deleteBtn = new QPushButton(QStringLiteral("Cut Selected Joint (splits off a new robot)"), content);
    m_deleteBtn->setObjectName(QStringLiteral("rbDeleteJointButton"));
    layout->addWidget(m_deleteBtn);

    // --- Define joint from features ---
    layout->addWidget(makeSectionHeader(QStringLiteral("Define Joint"), content));
    m_defineHint = new QLabel(QStringLiteral(
        "Click two bores in the viewport (parallel is enough -- Define snaps them coaxial; "
        "click a bore again to deselect, or use Clear)."),
        content);
    m_defineHint->setWordWrap(true);
    layout->addWidget(m_defineHint);
    // The two-bore WORKING SET, always visible -- the user no longer has to infer from a status
    // line how many bores are held and where they are. Filled by refreshBoreSlots() (selection poll).
    auto* slotBox = new QGroupBox(content);
    auto* slotForm = new QFormLayout(slotBox);
    m_boreA = new QLabel(QStringLiteral("—"), slotBox);
    m_boreA->setObjectName(QStringLiteral("rbBoreALabel"));
    m_boreB = new QLabel(QStringLiteral("—"), slotBox);
    m_boreB->setObjectName(QStringLiteral("rbBoreBLabel"));
    slotForm->addRow(QStringLiteral("Bore A"), m_boreA);
    slotForm->addRow(QStringLiteral("Bore B"), m_boreB);
    layout->addWidget(slotBox);
    // Alignment: how the child bore's axis meets the parent's when Define snaps them coaxial.
    auto* alignBox = new QGroupBox(content);
    auto* alignForm = new QFormLayout(alignBox);
    m_alignCombo = new QComboBox(alignBox);
    m_alignCombo->setObjectName(QStringLiteral("rbAlignCombo"));
    m_alignCombo->addItem(QStringLiteral("Auto (least rotation)"));       // 0
    m_alignCombo->addItem(QStringLiteral("Align axes (same way)"));       // 1
    m_alignCombo->addItem(QStringLiteral("Oppose axes (facing)"));        // 2
    m_alignCombo->setToolTip(QStringLiteral(
        "How the second (child) bore meets the first when Define snaps them coaxial:\n"
        "Auto = smallest rotation; Align = both axes point the same way;\n"
        "Oppose = axes face each other (child flipped 180°). Re-Define to change."));
    alignForm->addRow(QStringLiteral("Alignment"), m_alignCombo);
    layout->addWidget(alignBox);
    m_defineBtn = new QPushButton(QStringLiteral("Define Revolute from 2 Selected Bores"), content);
    m_defineBtn->setObjectName(QStringLiteral("rbDefineFromFeaturesButton"));
    layout->addWidget(m_defineBtn);
    m_clearSelBtn = new QPushButton(QStringLiteral("Clear Bore Selection"), content);
    m_clearSelBtn->setObjectName(QStringLiteral("rbClearSelectionButton"));
    layout->addWidget(m_clearSelBtn);

    // --- Jog (test-drive) the selected joint ---
    // The affordance every benchmark authoring tool has: define a joint, wiggle it SECONDS later to
    // verify the axis/limits -- no node graph, no Play. Writes through LiveRobot::q (the single
    // writer, limit-clamped), never the node command bus.
    layout->addWidget(makeSectionHeader(QStringLiteral("Jog Selected Joint"), content));
    m_jogSlider = new QSlider(Qt::Horizontal, content);
    m_jogSlider->setObjectName(QStringLiteral("rbJogSlider"));
    m_jogSlider->setRange(0, 1000);
    m_jogSlider->setEnabled(false);
    layout->addWidget(m_jogSlider);
    m_jogLabel = new QLabel(QStringLiteral("select a joint to jog"), content);
    m_jogLabel->setObjectName(QStringLiteral("rbJogLabel"));
    layout->addWidget(m_jogLabel);

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
    connect(m_jogSlider, &QSlider::valueChanged, this, &RobotBuilderPanel::onJogMoved);

    // SelectionState is a plain ctx struct mutated by the viewport with no change signal, so the
    // bore slots + button gating poll it (cheap: reads a tiny vector 5x/s, updates only on change).
    m_selPoll = new QTimer(this);
    m_selPoll->setInterval(200);
    connect(m_selPoll, &QTimer::timeout, this, &RobotBuilderPanel::refreshBoreSlots);
    m_selPoll->start();
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
        // Joint-centric: the NAME + CAN nodeId (the addressable identity the node editor drives) lead;
        // the bodies it connects + type are secondary detail.
        const QString nm = j.name.empty() ? QStringLiteral("J%1").arg(i) : QString::fromStdString(j.name);
        m_jointsList->addItem(QStringLiteral("%1  [node %2]   B%3->B%4  %5%6")
            .arg(nm).arg(j.nodeId).arg(j.parent).arg(j.child)
            .arg(QString::fromLatin1((t >= 0 && t <= 2) ? kJTypeName[t] : "?"))
            .arg(j.ambiguous ? QStringLiteral("  (ambiguous)") : QString()));
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
    refreshBoreSlots();   // sync slot readout + Define gating NOW (not on the next poll tick)
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
    // Park the outgoing robot's authoring graph (e.g. the FANUC's, with its bore faces) so loading
    // the demo doesn't destroy it -- clicking the robot in the outliner restores it.
    else if (!gp->bodies.empty() && gp->robotId != demoId) {
        auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>();
        if (!store) store = &reg.ctx().emplace<krs::rbuild::AuthoringGraphStore>();
        store->byRobot[gp->robotId] = *gp;
    }
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
    if (auto* st = reg.ctx().find<krs::sel::SelectionState>()) { st->enabled = true; st->fifoTwoBores = true; }  // bore-picking live, FIFO 2
    setStatus(QStringLiteral("Loaded demo robot (robotId %1): %2 bodies, DOF %3. Click two bores to define a joint.")
                  .arg(demoId).arg(int(gp->bodies.size())).arg(gp->dof()));
    refresh();
    emit graphChanged();
}

// Bind the builder to a first-class robot selected elsewhere (the outliner). If the
// authoring graph already represents that robot, edit it directly. Otherwise synthesize
// an editable graph that MIRRORS the live robot -- even the boot FANUC becomes an
// ordinary editable RobotGraph, and edits re-apply via reapplyGraphToRobot on
// graphChanged. (The old m_editRobotId "live-bind" mode was dead code: nothing ever set
// it >= 0, so its ~150 lines of branches -- including three "not editable here" status
// messages -- were unreachable. Removed.)
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
        refresh();
        setStatus(QStringLiteral("Editing robot %1: re-type / define / delete joints, edit axes + limits.").arg(robotId));
        return;
    }

    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(robotId) : nullptr;
    if (!lr) { setStatus(QStringLiteral("Robot %1 not found in the live registry.").arg(robotId)); return; }

    auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>();
    if (!store) store = &reg.ctx().emplace<krs::rbuild::AuthoringGraphStore>();

    krs::rbuild::RobotGraph* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
    // PARK the outgoing robot's graph -- switching must never destroy its authoring state
    // (bore faces, un-jointed bodies, part names -- a bare live mirror has none of them).
    else if (!gp->bodies.empty()) store->byRobot[gp->robotId] = *gp;

    // RESTORE this robot's parked graph if we have one whose SHAPE still matches the live robot
    // (same body count; every joint id present live) -- placements/joint frames are refreshed from
    // the live mirror since the robot may have moved/been edited while parked. On any mismatch
    // (split/merge changed the structure) fall back to a fresh mirror, exactly the old behavior.
    krs::rbuild::RobotGraph fresh = krs::robot::buildGraphFromLiveRobot(*lr);
    bool restored = false;
    if (auto it = store->byRobot.find(robotId); it != store->byRobot.end()) {
        krs::rbuild::RobotGraph& parked = it->second;
        // The parked graph may legitimately hold MORE than the live mirror (un-jointed bodies,
        // ambiguous joints are exactly the authoring state worth preserving) -- align its chain
        // PREFIX to the mirror by ENTITY id (stable within the session) + joint identity; any
        // mismatch (split/merge changed the structure while parked) falls back to the fresh
        // mirror, which is exactly the old behavior.
        std::vector<krs::rbuild::RBJoint*> committed;      // ambiguous joints never reach the live
        for (auto& j : parked.joints)                      // mirror -- compare committed ones only
            if (!j.ambiguous) committed.push_back(&j);
        bool shapeOk = parked.bodies.size() >= fresh.bodies.size()
                    && committed.size() == fresh.joints.size()
                    && !fresh.bodies.empty();
        for (size_t i = 0; shapeOk && i < committed.size(); ++i)
            shapeOk = committed[i]->id == fresh.joints[i].id;
        for (size_t i = 0; shapeOk && i < fresh.bodies.size(); ++i)
            shapeOk = parked.bodies[i].entity == fresh.bodies[i].entity;
        if (shapeOk) {
            // kinematic truth from the live mirror (the robot may have moved while parked)...
            for (size_t i = 0; i < fresh.bodies.size(); ++i)
                parked.bodies[i].placement = fresh.bodies[i].placement;
            for (size_t i = 0; i < committed.size(); ++i) {
                committed[i]->axisPos = fresh.joints[i].axisPos;
                committed[i]->axisDir = fresh.joints[i].axisDir;
                committed[i]->refDir  = fresh.joints[i].refDir;
            }
            // ...authoring truth (faces, names, extra bodies, provenance) from the parked graph.
            *gp = parked;
            restored = true;
        }
        store->byRobot.erase(it);   // active again -> out of the garage either way
    }
    if (!restored) *gp = std::move(fresh);
    refresh();
    setStatus(QStringLiteral("Editing %1 as a graph (%2 joints)%3: re-type / define / delete / limits all take live effect.")
                  .arg(QString::fromStdString(lr->name)).arg(int(gp->joints.size()))
                  .arg(restored ? QStringLiteral(" [session authoring restored]") : QString()));
}

void RobotBuilderPanel::onDeleteJoint()
{
    if (m_isUpdatingUI) return;
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to delete.")); return; }

    // DESTRUCTIVE + no undo yet -> confirm, naming the joint and the consequence. Suppressed when the
    // panel is not shown (the headless gates drive this button via click() on a never-shown panel).
    if (isVisible()) {
        const QString jn = g->joints[row].name.empty() ? QStringLiteral("J%1").arg(row)
                                                       : QString::fromStdString(g->joints[row].name);
        const auto r = QMessageBox::question(this, QStringLiteral("Cut joint?"),
            QStringLiteral("Cut %1 (B%2-B%3)?\n\nThe child subtree becomes its OWN robot "
                           "(drag it by its root; re-mate two bores to merge it back). "
                           "There is no undo yet.")
                .arg(jn).arg(g->joints[row].parent).arg(g->joints[row].child),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Yes) { setStatus(QStringLiteral("Cut cancelled.")); return; }
    }

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

    // STALENESS FIX: committed Selections are click-time world snapshots; if the robot moved since
    // (an FK tick, an IK drag, a prior merge's transformRobot), the stored frames describe where the
    // bore WAS and the joint would be authored at a stale interface. Re-derive each from the CURRENT
    // entity transform at consume time (resolveFace); the user's rim CHOICE from click time (nearest
    // end) is preserved and re-expressed on the fresh frame. Synthetic selections (no faceId, e.g.
    // gate fixtures) keep their snapshot.
    auto freshen = [&](const krs::sel::Selection& s) -> krs::sel::Selection {
        if (s.faceId < 0 || !reg.valid(s.entity)) return s;
        krs::sel::Selection f = krs::sel::resolveFace(reg, s.entity, s.faceId);
        if (!f.valid) return s;
        const bool nearEnd0 = glm::distance(s.hitPoint, s.axisEnd0) <= glm::distance(s.hitPoint, s.axisEnd1);
        f.hitPoint = nearEnd0 ? f.axisEnd0 : f.axisEnd1;
        return f;
    };
    const krs::sel::Selection freshA = freshen(*selA), freshB = freshen(*selB);
    selA = &freshA; selB = &freshB;

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
        // PICK ORDER decides the merge direction: the FIRST-picked bore's robot is the PARENT
        // (stays put), the second-picked robot snaps onto it -- the CAD mate convention. The old
        // min(robotId) rule was arbitrary: re-mating a detached branch could yank the MAIN robot
        // onto the branch just because of id ordering.
        const int parentId = ridA, childId = ridB;
        const krs::sel::Selection* pSel = selA;
        const krs::sel::Selection* cSel = selB;
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

    // Carry the FULL identity through the Selection -> BRepFace boundary: faceKey (the durable
    // topological id the mate connector anchors to) and the rim centres. The old copy dropped both.
    auto toFace = [](const krs::sel::Selection& s) {
        BRepFace f; f.type = int(s.type); f.axisPos = s.axisPos; f.axisDir = s.axisDir;
        f.normal = s.normal; f.radius = s.radius; f.faceKey = s.faceKey;
        f.axisEnd0 = s.axisEnd0; f.axisEnd1 = s.axisEnd1; return f;
    };
    const int a = krs::rbuild::bodyIndexForEntity(*g, int(selA->entity));
    const int b = krs::rbuild::bodyIndexForEntity(*g, int(selB->entity));
    if (a < 0 || b < 0 || a == b) {
        setStatus(QStringLiteral("The two bores must be on two distinct robot bodies (a=%1 b=%2).").arg(a).arg(b));
        return;
    }

    krs::rbuild::EditController ctrl{ g };
    const int before = ctrl.dof();

    // SNAP FIRST, define second. defineFromFeatures requires near-parallel axes (~5 deg) even with
    // requireCollinear=false -- but on a POSED arm the two picked bores are almost never parallel,
    // so the old define-then-snap order rejected the pair and Define appeared to do nothing. The
    // mate snap (mateTransformConcentric) handles ARBITRARY rotations, so: rotate the child subtree
    // coaxial first, THEN define from the now-clean pair.
    //
    // parent = the body NEARER the base in chain order (stays put; the child snaps) -- the same
    // normalization defineFromFeatures applies, computed up front so the snap knows who moves.
    const krs::rbuild::RobotGraph::ChainOrder co = g->chainOrder();
    auto idxOf = [&](int bIdx) {
        for (size_t i = 0; i < co.order.size(); ++i) if (co.order[i] == bIdx) return int(i);
        return 1000000 + bIdx;   // unreached -> after ordered, stable by body index
    };
    int parent = a, child = b;
    if (idxOf(b) < idxOf(a)) { parent = b; child = a; }
    const bool aIsParent = (a == parent);

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
    const krs::rbuild::RBJoint pf = aIsParent ? faceFrame(*selA) : faceFrame(*selB);  // parent face (stays)
    krs::rbuild::RBJoint       cf = aIsParent ? faceFrame(*selB) : faceFrame(*selA);  // child face (snaps)

    // ALIGNMENT control: how the child bore's axis meets the parent's after the snap.
    //   Auto        = least rotation (flip the child sense if it points away)
    //   Align axes  = both axes end pointing the SAME way
    //   Oppose axes = the axes end FACING each other (child flipped 180 deg)
    const int alignMode = m_alignCombo ? m_alignCombo->currentIndex() : 0;
    if (alignMode == 0)      { if (glm::dot(cf.axisDir, pf.axisDir) < 0.0f) cf.flipAxis(); }
    else if (alignMode == 2) cf.flipAxis();

    // MATE-SNAP: rigidly rotate+translate the CHILD body + its subtree so the two SELECTED faces
    // meet at their interface -- rims coincident, axes concentric. The parent stays fixed.
    krs::robot::snapMateSubtree(*m_scene, *g, parent, child, pf, cf);

    // The child moved: re-resolve both picks at the POST-snap transforms so the define and the
    // minted connectors see a consistent, coaxial pair.
    const krs::sel::Selection snapA = freshen(*selA);
    const krs::sel::Selection snapB = freshen(*selB);

    krs::rbuild::RBJoint created;
    const bool ok = ctrl.defineFromFeatures(toFace(snapA), a, toFace(snapB), b, &created, &parent, &child, false);
    if (!ok) {
        setStatus(QStringLiteral("Cannot define joint: the pair stayed degenerate even after the mate snap "
                                 "(non-cylindrical or invalid geometry). The child body WAS snapped coaxial."));
        return;
    }

    // PERSISTENT MATE (decision-doc wiring): mint a body-LOCAL MateConnector on EACH picked body +
    // record the MateConstraint in the ctx mate graph -- the durable, faceKey-anchored provenance the
    // architecture prescribes. RBJoint stays the immediate joint author (defineFromFeatures above).
    // Minted from the POST-snap world faces against the POST-snap entity transforms (a consistent
    // pair), so the stored body-local frames are exact.
    {
        auto* mgp = reg.ctx().find<MateGraphComponent>();
        if (!mgp) mgp = &reg.ctx().emplace<MateGraphComponent>();
        auto& mcA = reg.get_or_emplace<MateConnectorComponent>(selA->entity);
        auto& mcB = reg.get_or_emplace<MateConnectorComponent>(selB->entity);
        auto eigOf = [&](entt::entity e) {
            Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
            if (const auto* tc = reg.try_get<TransformComponent>(e)) {
                const glm::mat4 T = tc->getTransform();
                for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M(r, c) = double(T[c][r]);  // glm is column-major
            }
            return M;
        };
        const std::uint64_t mateId = krs::rbuild::authorConcentricMate(
            *mgp, selA->entity, mcA, eigOf(selA->entity), toFace(snapA),
                  selB->entity, mcB, eigOf(selB->entity), toFace(snapB));
        (void)mateId;
    }

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
    setStatus(QStringLiteral("Bore selection cleared. Click two bores to define a joint."));
    refreshBoreSlots();
}

// joints-list row -> chain DOF index: the joint's position among the committed, non-Fixed
// joints (the DOF the limit editor and jog slider act on). -1 = carries no DOF.
int RobotBuilderPanel::dofIndexOfRow(int row) const
{
    auto* g = graph();
    if (!g || row < 0 || row >= int(g->joints.size())) return -1;
    int dofSeen = 0;
    for (int ji = 0; ji <= row; ++ji) {
        if (g->joints[ji].ambiguous || g->joints[ji].type == krs::rbuild::JType::Fixed) continue;
        if (ji == row) return dofSeen;
        ++dofSeen;
    }
    return -1;
}

void RobotBuilderPanel::onJointSelected(int row)
{
    // Authoring graph: reflect the selected joint's axis origin + type into the controls.
    auto* g = graph();
    if (!g || row < 0 || row >= int(g->joints.size()) || !m_axisX) {
        showSelectedJointAxis(-1);
        syncJogToSelected(-1);
        return;
    }
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
    // the joint you clicked. Fixed/ambiguous joints carry no DOF -> leave the field as-is.
    const int dofIdx = dofIndexOfRow(row);
    if (dofIdx >= 0) {
        const QSignalBlocker bd(m_dofIndex), bl(m_limitLo), bh(m_limitHi);
        m_dofIndex->setValue(dofIdx);
        m_limitLo->setValue(j.limits.lower);
        m_limitHi->setValue(j.limits.upper);
    }
    showSelectedJointAxis(row);   // glowing axis bar in the main viewport for the clicked joint
    syncJogToSelected(row);       // point the jog slider at this joint's DOF + current q
}

// Point the jog slider at the selected joint's DOF: range = its limits, position = the live q.
// Disabled for Fixed/ambiguous joints (no DOF) or when no live robot backs the graph.
void RobotBuilderPanel::syncJogToSelected(int row)
{
    if (!m_jogSlider) return;
    auto* g = graph();
    const int dofIdx = dofIndexOfRow(row);
    krs::robot::LiveRobot* lr = nullptr;
    if (g && m_scene)
        if (auto* rr = m_scene->getRegistry().ctx().find<krs::robot::RobotRegistry>())
            lr = rr->get(g->robotId);
    if (!g || dofIdx < 0 || !lr || dofIdx >= lr->ndof()) {
        const QSignalBlocker bs(m_jogSlider);
        m_jogSlider->setEnabled(false);
        if (m_jogLabel) m_jogLabel->setText(QStringLiteral("select a drivable joint to jog"));
        return;
    }
    const auto& lim = g->joints[row].limits;
    const double lo = lim.enabled ? lim.lower : -3.14159265;
    const double hi = lim.enabled ? lim.upper :  3.14159265;
    const double q  = lr->q[dofIdx];
    const QSignalBlocker bs(m_jogSlider);
    m_jogSlider->setEnabled(true);
    m_jogSlider->setValue((hi > lo) ? int(std::round((q - lo) / (hi - lo) * 1000.0)) : 500);
    if (m_jogLabel)
        m_jogLabel->setText(QStringLiteral("q = %1 rad (%2°)   range [%3, %4]")
            .arg(q, 0, 'f', 3).arg(q * 57.2957795, 0, 'f', 1).arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2));
}

// Jog = write the selected joint's DOF through the SINGLE writer (LiveRobot::setCommandedQ, which
// clamps) and refresh the FK viz. Never touches the node command bus (which is per-eval-pass and
// would fight a hand jog).
void RobotBuilderPanel::onJogMoved(int sliderValue)
{
    if (m_isUpdatingUI || !m_scene) return;
    auto* g = graph();
    const int row = m_jointsList ? m_jointsList->currentRow() : -1;
    const int dofIdx = dofIndexOfRow(row);
    if (!g || dofIdx < 0) return;
    auto* rr = m_scene->getRegistry().ctx().find<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot* lr = rr ? rr->get(g->robotId) : nullptr;
    if (!lr || dofIdx >= lr->ndof()) return;
    const auto& lim = g->joints[row].limits;
    const double lo = lim.enabled ? lim.lower : -3.14159265;
    const double hi = lim.enabled ? lim.upper :  3.14159265;
    Eigen::VectorXd qc = lr->q;
    qc[dofIdx] = lo + (hi - lo) * (double(sliderValue) / 1000.0);
    lr->setCommandedQ(qc);                                          // clamped by the joint's limits
    if (lr->useRobotFkViz) krs::robot::writeBackRobotViz(*m_scene, *lr);
    if (m_jogLabel)
        m_jogLabel->setText(QStringLiteral("q = %1 rad (%2°)   range [%3, %4]")
            .arg(lr->q[dofIdx], 0, 'f', 3).arg(lr->q[dofIdx] * 57.2957795, 0, 'f', 1)
            .arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2));
}

// Bore A/B slot readout + Define/Snap enable gating. Polled (SelectionState has no change signal):
// shows WHICH two bores are armed (body, radius) and disables Define until the pair is valid --
// two cylinders on two distinct bodies (same robot) or on two different robots (re-mate/merge).
void RobotBuilderPanel::refreshBoreSlots()
{
    if (!m_scene || !m_boreA || !m_boreB || !m_defineBtn) return;
    auto& reg = m_scene->getRegistry();
    auto* sel = reg.ctx().find<krs::sel::SelectionState>();
    auto* g   = graph();

    std::vector<const krs::sel::Selection*> cyls;
    if (sel) for (const auto& s : sel->selected)
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) cyls.push_back(&s);
    const krs::sel::Selection* sA = cyls.size() >= 2 ? cyls[cyls.size() - 2]
                                  : cyls.size() == 1 ? cyls[0] : nullptr;
    const krs::sel::Selection* sB = cyls.size() >= 2 ? cyls[cyls.size() - 1] : nullptr;

    auto describe = [&](const krs::sel::Selection* s) -> QString {
        if (!s) return QStringLiteral("—");                        // em-dash: empty slot
        QString nm = QStringLiteral("entity %1").arg(std::uint32_t(s->entity));
        if (reg.valid(s->entity))
            if (const auto* tag = reg.try_get<TagComponent>(s->entity))
                if (!tag->tag.empty()) nm = QString::fromStdString(tag->tag);
        const int b = g ? krs::rbuild::bodyIndexForEntity(*g, int(std::uint32_t(s->entity))) : -1;
        return QStringLiteral("%1%2   r=%3 m")
            .arg(nm).arg(b >= 0 ? QStringLiteral("  (B%1)").arg(b) : QString())
            .arg(s->radius, 0, 'f', 4);
    };
    const QString a = describe(sA), b = describe(sB);
    if (m_boreA->text() != a) m_boreA->setText(a);
    if (m_boreB->text() != b) m_boreB->setText(b);

    // Define is valid for: two bores on two DISTINCT bodies of the edited graph, or two bores on
    // two DIFFERENT robots (the cross-robot re-mate/merge path).
    bool canDefine = false;
    if (sA && sB && g) {
        const int ba = krs::rbuild::bodyIndexForEntity(*g, int(std::uint32_t(sA->entity)));
        const int bb = krs::rbuild::bodyIndexForEntity(*g, int(std::uint32_t(sB->entity)));
        auto robotOf = [&](entt::entity e) -> int {
            const auto* sc = reg.try_get<RobotSubcomponentComponent>(e); return sc ? sc->robotId : -1; };
        const int ra = robotOf(sA->entity), rb = robotOf(sB->entity);
        canDefine = (ra >= 0 && rb >= 0 && ra != rb) || (ba >= 0 && bb >= 0 && ba != bb);
    }
    if (m_defineBtn->isEnabled() != canDefine) {
        m_defineBtn->setEnabled(canDefine);
        m_defineBtn->setToolTip(canDefine ? QString()
            : QStringLiteral("Pick two bores on two different bodies (they show in the Bore A/B slots above)."));
    }
    if (m_snapAxisBtn) {
        const bool canSnap = (sA || sB) && m_jointsList && m_jointsList->currentRow() >= 0;
        if (m_snapAxisBtn->isEnabled() != canSnap) m_snapAxisBtn->setEnabled(canSnap);
    }
}

void RobotBuilderPanel::onJointTypeChanged(int comboIndex)
{
    if (m_isUpdatingUI) return;
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
    auto* g = graph();
    if (!g) { setStatus(QStringLiteral("No robot loaded.")); return; }
    const int row = m_jointsList->currentRow();
    if (row < 0 || row >= int(g->joints.size())) { setStatus(QStringLiteral("Select a joint to snap.")); return; }
    auto* sel = m_scene ? m_scene->getRegistry().ctx().find<krs::sel::SelectionState>() : nullptr;
    const krs::sel::Selection* bore = nullptr;
    if (sel) for (const auto& s : sel->selected)   // LAST selected cylinder = the most recent intent
        if (s.valid && s.type == krs::sel::FeatureType::Cylinder) bore = &s;
    if (!bore) { setStatus(QStringLiteral("Select a cylindrical bore in the viewport to snap to.")); return; }
    // Staleness fix (see onDefineFromFeatures): snap to where the bore IS, not where it was clicked.
    krs::sel::Selection freshBore = *bore;
    if (bore->faceId >= 0 && m_scene->getRegistry().valid(bore->entity)) {
        const krs::sel::Selection f = krs::sel::resolveFace(m_scene->getRegistry(), bore->entity, bore->faceId);
        if (f.valid) freshBore = f;
    }
    bore = &freshBore;
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
        // PERSISTENT MATE wiring: the define must ALSO mint a body-LOCAL connector on each picked
        // entity + one MateConstraint in the ctx graph (the durable provenance the decision doc
        // prescribes -- previously gate-only library code with no production caller).
        const auto* mgc = reg.ctx().find<MateGraphComponent>();
        const auto* ccA = reg.try_get<MateConnectorComponent>(s2.entity);
        const auto* ccB = reg.try_get<MateConnectorComponent>(s3.entity);
        const bool mateAuthored = mgc && mgc->mates.size() == 1
            && ccA && ccA->connectors.size() == 1 && ccB && ccB->connectors.size() == 1
            && mgc->mates[0].bodyA == s2.entity && mgc->mates[0].connA == ccA->connectors[0].id
            && mgc->mates[0].bodyB == s3.entity && mgc->mates[0].connB == ccB->connectors[0].id;
        defineOk = defineOk && frameOk && mateAuthored;
        printf("[rbuild]   define control: DOF %d -> %d (want %d), frame-matches=%s, mate+connectors minted=%s  %s\n",
               before, after, before + 1, frameOk ? "yes" : "no", mateAuthored ? "yes" : "no",
               defineOk ? "PASS" : "FAIL");

        // degenerate (NON-PARALLEL) bores -> rejected, DOF unchanged. (An offset-but-PARALLEL pair
        // is ACCEPTED by design since 8ed10f40: the manual define passes requireCollinear=false and
        // the mate snap makes the pair coaxial -- the old offset fixture asserted the pre-snap rule.)
        Scene scene2;
        auto& reg2 = scene2.getRegistry();
        auto& g2 = reg2.ctx().emplace<RobotGraph>(buildDemoGraph());
        spawnGraphBodies(scene2, g2, 0);
        auto& sel2 = reg2.ctx().emplace<krs::sel::SelectionState>();
        krs::sel::Selection o2 = s2; o2.entity = entt::entity(std::uint32_t(g2.bodies[2].entity));
        krs::sel::Selection o3 = s2; o3.entity = entt::entity(std::uint32_t(g2.bodies[3].entity));
        o3.axisDir = { 1.0f, 0.0f, 0.0f }; o3.normal = { 1.0f, 0.0f, 0.0f };   // perpendicular axis -> no revolute
        sel2.selected = { o2, o3 };
        RobotBuilderPanel panel2(&scene2);
        auto* def2 = panel2.findChild<QPushButton*>(QStringLiteral("rbDefineFromFeaturesButton"));
        const int b2 = g2.dof(); def2->click(); const int a2 = g2.dof();
        degenRejected = (a2 == b2);
        printf("[rbuild]   define degenerate (non-parallel) pair: DOF %d -> %d (want unchanged)  %s\n",
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

    // PARK/RESTORE: switching the Builder between robots must NOT destroy graph-only authoring
    // state. The old editRobot overwrote the single ctx graph with a bare live mirror (no bore
    // faces, no un-jointed bodies) on every outliner click of another robot.
    bool parkRestoreOk = false, parkNegOk = false;
    {
        Scene scene;
        auto& reg = scene.getRegistry();
        auto& g = reg.ctx().emplace<RobotGraph>(buildDemoGraph());       // robot 0 = the active graph
        spawnGraphBodies(scene, g, 0);
        krs::robot::instantiateFromGraph(scene, g, 0);
        RobotGraph g1 = buildDemoGraph();                                // robot 1 = switch target
        spawnGraphBodies(scene, g1, 1);
        krs::robot::instantiateFromGraph(scene, g1, 1);
        RobotBuilderPanel panel(&scene);
        size_t facesBefore = 0, bodiesBefore = 0;
        {
            const RobotGraph* gp = reg.ctx().find<RobotGraph>();
            for (const auto& b : gp->bodies) facesBefore += b.faces.size();
            bodiesBefore = gp->bodies.size();
        }
        panel.editRobot(1);                                              // away: parks robot 0's graph
        const RobotGraph* gp = reg.ctx().find<RobotGraph>();
        const bool switched = gp && gp->robotId == 1;
        // NEG-CTRL (documents the live-mirror baseline): the mirror of robot 1 carries NO faces.
        size_t mirrorFaces = 0; for (const auto& b : gp->bodies) mirrorFaces += b.faces.size();
        parkNegOk = switched && (mirrorFaces == 0) && (facesBefore > 0);
        panel.editRobot(0);                                              // back: restores the parked graph
        size_t facesAfter = 0; for (const auto& b : gp->bodies) facesAfter += b.faces.size();
        parkRestoreOk = gp && gp->robotId == 0
                     && gp->bodies.size() == bodiesBefore && facesAfter == facesBefore;
        printf("[rbuild]   park/restore: faces before=%zu -> after switch-away-and-back=%zu (bodies %zu) ; live-mirror-has-none neg-ctrl=%s  %s\n",
               facesBefore, facesAfter, bodiesBefore, parkNegOk ? "yes" : "NO",
               (parkRestoreOk && parkNegOk) ? "PASS" : "FAIL");
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
                    && axisDirOk && zeroAxisRejected && extraMapOk && parkRestoreOk && parkNegOk;

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
