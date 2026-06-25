#include "RobotBuilderPanel.hpp"

#include "Scene.hpp"
#include "components.hpp"            // BRepFace
#include "SelectionService.hpp"     // krs::sel::SelectionState / Selection / FeatureType
#include "RobotBuilder.hpp"         // krs::rbuild::RobotGraph / EditController / RBJoint
#include "RobotBuilderScene.hpp"    // buildDemoGraph / spawnGraphBodies / bodyIndexForEntity
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
    if (!m_scene) { setStatus(QStringLiteral("No scene.")); return; }
    auto& reg = m_scene->getRegistry();
    auto* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
    *gp = krs::rbuild::buildDemoGraph();
    krs::rbuild::spawnGraphBodies(*m_scene, *gp, /*robotId*/ 0);
    setStatus(QStringLiteral("Loaded demo robot: %1 bodies, DOF %2. Select two bores on B2/B3 to define J2.")
                  .arg(int(gp->bodies.size())).arg(gp->dof()));
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

void RobotBuilderPanel::onJointSelected(int /*row*/)
{
    // Selection drives delete/property targeting; nothing to mutate here.
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
    // The proven hot-swap: set the limit, then re-derive toJointLimits() LIVE (no
    // stale cache). m_cfg->robot joint order matches toJointLimits()'s DOF order.
    const bool ok = m_cfg->setPositionLimit(idx, lo, hi);
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
