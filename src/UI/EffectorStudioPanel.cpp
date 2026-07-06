// EffectorStudioPanel.cpp -- see EffectorStudioPanel.hpp. The .kee authoring + attach dock.
#include "EffectorStudioPanel.hpp"
#include "Scene.hpp"
#include "RobotBuilder.hpp"       // krs::rbuild::RobotGraph (the edited effector graph)
#include "FaceRole.hpp"           // FaceRoleComponent (durable faceKey -> role)
#include "SelectionService.hpp"   // ambient face selections (role painting source)
#include "SnapSession.hpp"        // last snap commit -> TCP capture
#include "components.hpp"         // MateConnectorComponent, BRepFaceComponent, robots

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QSignalBlocker>
#include <glm/gtc/matrix_transform.hpp>   // glm::translate (attach delta compose)

namespace {
QLabel* hint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    return l;
}
entt::entity bodyEntity(const krs::rbuild::RobotGraph& g, int bodyIdx) {
    if (bodyIdx < 0 || bodyIdx >= int(g.bodies.size()) || g.bodies[std::size_t(bodyIdx)].entity < 0)
        return entt::null;
    return entt::entity(std::uint32_t(g.bodies[std::size_t(bodyIdx)].entity));
}
} // namespace

EffectorStudioPanel::EffectorStudioPanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    root->addWidget(hint(QStringLiteral(
        "An end effector IS a small robot: author its bodies + joints with Import CAD and the "
        "Robot Builder as usual. This studio layers the EFFECTOR semantics on the robot being "
        "edited and packages everything as a shareable .kee."), page));

    auto* g0 = new QGroupBox(QStringLiteral("1 · Document"), page);
    { auto* v = new QVBoxLayout(g0);
      auto* row = new QHBoxLayout();
      row->addWidget(new QLabel(QStringLiteral("Name:"), g0));
      m_name = new QLineEdit(QStringLiteral("gripper"), g0);
      row->addWidget(m_name, 1);
      v->addLayout(row);
      auto* row2 = new QHBoxLayout();
      auto* saveBtn = new QPushButton(QStringLiteral("Save .kee…"), g0);
      auto* loadBtn = new QPushButton(QStringLiteral("Load .kee…"), g0);
      row2->addWidget(saveBtn); row2->addWidget(loadBtn); row2->addStretch(1);
      v->addLayout(row2);
      connect(saveBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onSaveKee);
      connect(loadBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onLoadKee); }
    root->addWidget(g0);

    auto* g1 = new QGroupBox(QStringLiteral("2 · Face roles (grip semantics)"), page);
    { auto* v = new QVBoxLayout(g1);
      v->addWidget(hint(QStringLiteral(
          "Select faces in the viewport (click + Shift-click, any body of the effector), pick a "
          "role, Paint. Grip surfaces are where the solver may contact; Keep-out is forbidden."), g1));
      auto* row = new QHBoxLayout();
      m_roleCombo = new QComboBox(g1);
      m_roleCombo->addItems({ QStringLiteral("GripSurface"), QStringLiteral("KeepOut"),
                              QStringLiteral("Interaction") });
      auto* paintBtn = new QPushButton(QStringLiteral("Paint on selection"), g1);
      auto* clearBtn = new QPushButton(QStringLiteral("Clear on selection"), g1);
      row->addWidget(m_roleCombo, 1); row->addWidget(paintBtn); row->addWidget(clearBtn);
      v->addLayout(row);
      connect(paintBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onPaintRole);
      connect(clearBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onClearRole); }
    root->addWidget(g1);

    auto* g2 = new QGroupBox(QStringLiteral("3 · Interface mate + TCP"), page);
    { auto* v = new QVBoxLayout(g2);
      v->addWidget(hint(QStringLiteral(
          "The interface mate is the connector the robot's flange grabs (place one with the "
          "Constraints panel's Place Mate Connector). The TCP is captured from your last "
          "inference click (e.g. the point between the jaw tips)."), g2));
      auto* row = new QHBoxLayout();
      m_ifaceCombo = new QComboBox(g2);
      auto* ifaceBtn = new QPushButton(QStringLiteral("Set as interface"), g2);
      row->addWidget(m_ifaceCombo, 1); row->addWidget(ifaceBtn);
      v->addLayout(row);
      auto* row2 = new QHBoxLayout();
      m_tcpName = new QLineEdit(QStringLiteral("tcp"), g2);
      auto* tcpBtn = new QPushButton(QStringLiteral("TCP from last click"), g2);
      row2->addWidget(m_tcpName, 1); row2->addWidget(tcpBtn);
      v->addLayout(row2);
      m_tcpLabel = hint(QStringLiteral("No TCP captured yet."), g2);
      v->addWidget(m_tcpLabel);
      connect(ifaceBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onSetInterfaceMate);
      connect(tcpBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onCaptureTcp); }
    root->addWidget(g2);

    auto* g3 = new QGroupBox(QStringLiteral("4 · Actuation (how the jaws move)"), page);
    { auto* v = new QVBoxLayout(g3);
      auto mkSpin = [g3](double lo, double hi, double val, double step) {
          auto* s = new QDoubleSpinBox(g3);
          s->setRange(lo, hi); s->setDecimals(4); s->setSingleStep(step); s->setValue(val);
          return s;
      };
      auto* row = new QHBoxLayout();
      row->addWidget(new QLabel(QStringLiteral("Drive joint:"), g3));
      m_driveCombo = new QComboBox(g3);
      row->addWidget(m_driveCombo, 1);
      v->addLayout(row);
      auto* row2 = new QHBoxLayout();
      row2->addWidget(new QLabel(QStringLiteral("Open q:"), g3));
      m_openQ = mkSpin(-10, 10, 0.04, 0.005); row2->addWidget(m_openQ, 1);
      row2->addWidget(new QLabel(QStringLiteral("Closed q:"), g3));
      m_closedQ = mkSpin(-10, 10, 0.0, 0.005); row2->addWidget(m_closedQ, 1);
      v->addLayout(row2);
      auto* row3 = new QHBoxLayout();
      row3->addWidget(new QLabel(QStringLiteral("Mimic:"), g3));
      m_mimicCombo = new QComboBox(g3);
      row3->addWidget(m_mimicCombo, 1);
      row3->addWidget(new QLabel(QStringLiteral("ratio:"), g3));
      m_mimicRatio = mkSpin(-10, 10, -1.0, 0.1); row3->addWidget(m_mimicRatio);
      v->addLayout(row3);
      auto* row4 = new QHBoxLayout();
      row4->addWidget(new QLabel(QStringLiteral("Max grip force (N):"), g3));
      m_gripForce = mkSpin(0, 5000, 40.0, 5.0); row4->addWidget(m_gripForce, 1);
      v->addLayout(row4); }
    root->addWidget(g3);

    auto* g4 = new QGroupBox(QStringLiteral("5 · Validate + Attach"), page);
    { auto* v = new QVBoxLayout(g4);
      auto* valBtn = new QPushButton(QStringLiteral("Validate"), g4);
      v->addWidget(valBtn);
      m_validateList = new QListWidget(g4);
      m_validateList->setMaximumHeight(110);
      v->addWidget(m_validateList);
      v->addWidget(hint(QStringLiteral(
          "Attach mates the effector's interface connector onto a ROBOT connector by its "
          "persistent id — \"with end effector 2\" later resolves the same id."), g4));
      auto* row = new QHBoxLayout();
      m_attachCombo = new QComboBox(g4);
      auto* attachBtn = new QPushButton(QStringLiteral("Attach"), g4);
      auto* detachBtn = new QPushButton(QStringLiteral("Detach last"), g4);
      row->addWidget(m_attachCombo, 1); row->addWidget(attachBtn); row->addWidget(detachBtn);
      v->addLayout(row);
      connect(valBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onValidate);
      connect(attachBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onAttach);
      connect(detachBtn, &QPushButton::clicked, this, &EffectorStudioPanel::onDetach); }
    root->addWidget(g4);

    m_status = new QLabel(page);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#bbbbbb;"));
    root->addWidget(m_status);
    root->addStretch(1);
    scroll->setWidget(page);
    outer->addWidget(scroll);

    m_tick = new QTimer(this);
    m_tick->setInterval(700);
    connect(m_tick, &QTimer::timeout, this, &EffectorStudioPanel::onTick);
    m_tick->start();
}

void EffectorStudioPanel::onTick() { refreshCombos(); }

void EffectorStudioPanel::refreshCombos()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    const auto* g = reg.ctx().find<krs::rbuild::RobotGraph>();

    QString sig;
    if (g) {
        for (const auto& j : g->joints) sig += QString::fromStdString(j.name) + QLatin1Char(',');
        for (int bi = 0; bi < int(g->bodies.size()); ++bi) {
            const entt::entity e = bodyEntity(*g, bi);
            if (reg.valid(e))
                if (const auto* mcc = reg.try_get<MateConnectorComponent>(e))
                    for (const auto& mc : mcc->connectors)
                        sig += QStringLiteral("b%1#%2,").arg(bi).arg(mc.id);
        }
    }
    for (auto e : reg.view<RobotSubcomponentComponent, MateConnectorComponent>())
        for (const auto& mc : reg.get<MateConnectorComponent>(e).connectors)
            sig += QStringLiteral("r%1#%2,").arg(std::uint32_t(e)).arg(mc.id);
    if (sig == m_combosSig) return;
    m_combosSig = sig;

    // effector joints (drive + mimic)
    const QString keepDrive = m_driveCombo->currentText(), keepMimic = m_mimicCombo->currentText();
    { QSignalBlocker b1(m_driveCombo), b2(m_mimicCombo);
      m_driveCombo->clear(); m_mimicCombo->clear();
      m_driveCombo->addItem(QStringLiteral("(passive tool)"));
      m_mimicCombo->addItem(QStringLiteral("(none)"));
      if (g) for (const auto& j : g->joints) {
          m_driveCombo->addItem(QString::fromStdString(j.name));
          m_mimicCombo->addItem(QString::fromStdString(j.name));
      }
      if (!keepDrive.isEmpty()) m_driveCombo->setCurrentText(keepDrive);
      if (!keepMimic.isEmpty()) m_mimicCombo->setCurrentText(keepMimic); }

    // interface candidates: connectors on the edited graph's bodies (base first)
    { const QString keep = m_ifaceCombo->currentText();
      QSignalBlocker b(m_ifaceCombo);
      m_ifaceCombo->clear();
      if (g) for (int bi = 0; bi < int(g->bodies.size()); ++bi) {
          const entt::entity e = bodyEntity(*g, bi);
          if (!reg.valid(e)) continue;
          if (const auto* mcc = reg.try_get<MateConnectorComponent>(e))
              for (const auto& mc : mcc->connectors) {
                  m_ifaceCombo->addItem(
                      QStringLiteral("%1 · #%2 %3%4")
                          .arg(QString::fromStdString(g->bodies[std::size_t(bi)].name))
                          .arg(mc.id).arg(QString::fromStdString(mc.name))
                          .arg(bi == g->base ? QStringLiteral("  [base]") : QString()),
                      QPoint(bi, int(mc.id)));
              }
      }
      if (!keep.isEmpty()) m_ifaceCombo->setCurrentText(keep); }

    // attach targets: connectors on ROBOT member bodies (the flange), by persistent id
    { const QString keep = m_attachCombo->currentText();
      QSignalBlocker b(m_attachCombo);
      m_attachCombo->clear();
      for (auto e : reg.view<RobotSubcomponentComponent, MateConnectorComponent>()) {
          const int rid = reg.get<RobotSubcomponentComponent>(e).robotId;
          const auto* tag = reg.try_get<TagComponent>(e);
          for (const auto& mc : reg.get<MateConnectorComponent>(e).connectors)
              m_attachCombo->addItem(
                  QStringLiteral("robot %1 · %2 · mate #%3")
                      .arg(rid)
                      .arg(tag ? QString::fromStdString(tag->tag) : QStringLiteral("link"))
                      .arg(mc.id),
                  QPoint(int(std::uint32_t(e)), int(mc.id)));
      }
      if (!keep.isEmpty()) m_attachCombo->setCurrentText(keep); }
}

void EffectorStudioPanel::onPaintRole()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    const auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st) return;
    FaceRoleEntry entry;
    entry.role = faceRoleFromName(m_roleCombo->currentText().toStdString().c_str());
    int painted = 0;
    for (const auto& s : st->selected) {
        if (!s.valid || s.faceId < 0 || !reg.valid(s.entity)) continue;
        const auto* fc = reg.try_get<BRepFaceComponent>(s.entity);
        if (!fc || s.faceId >= int(fc->faces.size())) continue;
        const std::uint64_t key = fc->faces[std::size_t(s.faceId)].faceKey;
        if (key == 0) continue;                       // synthetic face: no durable identity
        reg.get_or_emplace<FaceRoleComponent>(s.entity).byFaceKey[key] = entry;
        ++painted;
    }
    m_status->setText(painted > 0
        ? QStringLiteral("Painted %1 as %2 face(s): %3.")
              .arg(painted).arg(m_roleCombo->currentText()).arg(painted)
        : QStringLiteral("Nothing painted — select faces first (click + Shift-click)."));
}

void EffectorStudioPanel::onClearRole()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    const auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st) return;
    int cleared = 0;
    for (const auto& s : st->selected) {
        if (!s.valid || s.faceId < 0 || !reg.valid(s.entity)) continue;
        const auto* fc = reg.try_get<BRepFaceComponent>(s.entity);
        if (!fc || s.faceId >= int(fc->faces.size())) continue;
        if (auto* rc = reg.try_get<FaceRoleComponent>(s.entity))
            cleared += int(rc->byFaceKey.erase(fc->faces[std::size_t(s.faceId)].faceKey));
    }
    m_status->setText(QStringLiteral("Cleared roles on %1 face(s).").arg(cleared));
}

void EffectorStudioPanel::onSetInterfaceMate()
{
    const QPoint d = m_ifaceCombo->currentData().toPoint();
    if (m_ifaceCombo->count() == 0) {
        m_status->setText(QStringLiteral("No connectors on the effector yet — place one with the "
                                         "Constraints panel first."));
        return;
    }
    m_status->setText(QStringLiteral("Interface mate set: body %1, connector #%2. It will be "
                                     "written into the .kee on Save.").arg(d.x()).arg(d.y()));
}

void EffectorStudioPanel::onCaptureTcp()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    const auto& ss = krs::snapui::snapSession(reg);
    const auto* g = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!g || ss.result.entity == entt::null) {
        m_status->setText(QStringLiteral("Click an inference point first (the dot between the jaw "
                                         "tips is the classic TCP)."));
        return;
    }
    // world -> BASE-BODY-LOCAL
    glm::mat4 inv(1.0f);
    const entt::entity base = bodyEntity(*g, g->base);
    if (reg.valid(base))
        if (const auto* xf = reg.try_get<TransformComponent>(base))
            inv = glm::inverse(xf->getTransform());
    const glm::mat3 invR(inv);
    krs::kee::TcpFrame t;
    t.name = m_tcpName->text().toStdString();
    t.pos = glm::vec3(inv * glm::vec4(ss.result.pos, 1.0f));
    t.z = glm::normalize(invR * ss.result.z);
    t.x = glm::normalize(invR * ss.result.x);
    m_tcps.push_back(t);
    m_tcpLabel->setText(QStringLiteral("%1 TCP(s) captured; last '%2' at (%3, %4, %5) base-local.")
                            .arg(m_tcps.size()).arg(m_tcpName->text())
                            .arg(t.pos.x, 0, 'f', 4).arg(t.pos.y, 0, 'f', 4).arg(t.pos.z, 0, 'f', 4));
}

bool EffectorStudioPanel::assembleDoc(krs::kee::EffectorDoc& out, QString* err)
{
    auto& reg = m_scene->getRegistry();
    const auto* g = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!g || g->bodies.empty()) { if (err) *err = QStringLiteral("No robot graph is being edited."); return false; }
    out.name = m_name->text();
    out.graph = *g;
    const QPoint iface = m_ifaceCombo->currentData().toPoint();
    out.interfaceMateId = (m_ifaceCombo->count() > 0) ? std::uint32_t(iface.y()) : 0u;
    out.tcps = m_tcps;
    out.actuation.driveJointName = (m_driveCombo->currentIndex() <= 0)
        ? std::string() : m_driveCombo->currentText().toStdString();
    out.actuation.openQ = m_openQ->value();
    out.actuation.closedQ = m_closedQ->value();
    out.actuation.mimicJointName = (m_mimicCombo->currentIndex() <= 0)
        ? std::string() : m_mimicCombo->currentText().toStdString();
    out.actuation.mimicRatio = m_mimicRatio->value();
    out.actuation.maxGripForceN = m_gripForce->value();
    out.faceRolesByBody.clear();
    out.connectorsByBody.clear();
    for (int bi = 0; bi < int(g->bodies.size()); ++bi) {
        const entt::entity e = bodyEntity(*g, bi);
        if (!reg.valid(e)) continue;
        if (const auto* rc = reg.try_get<FaceRoleComponent>(e); rc && !rc->byFaceKey.empty())
            out.faceRolesByBody[bi] = rc->byFaceKey;
        if (const auto* mcc = reg.try_get<MateConnectorComponent>(e); mcc && !mcc->connectors.empty())
            out.connectorsByBody[bi] = mcc->connectors;
    }
    return true;
}

void EffectorStudioPanel::onValidate()
{
    krs::kee::EffectorDoc doc;
    QString err;
    m_validateList->clear();
    if (!assembleDoc(doc, &err)) { m_validateList->addItem(QStringLiteral("FAIL  %1").arg(err)); return; }
    for (const auto& item : krs::kee::validate(doc)) {
        auto* it = new QListWidgetItem((item.ok ? QStringLiteral("PASS  ") : QStringLiteral("FAIL  ")) + item.what);
        it->setForeground(item.ok ? QColor(0x66, 0xcc, 0x66) : QColor(0xdd, 0x66, 0x55));
        m_validateList->addItem(it);
    }
}

void EffectorStudioPanel::onSaveKee()
{
    krs::kee::EffectorDoc doc;
    QString err;
    if (!assembleDoc(doc, &err)) { m_status->setText(err); return; }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Effector"),
        m_name->text() + QStringLiteral(".kee"), QStringLiteral("End effector (*.kee)"));
    if (path.isEmpty()) return;
    const krs::kee::Report rep = krs::kee::saveKee(doc, path.toStdString());
    m_status->setText(rep.ok
        ? QStringLiteral("Saved %1%2").arg(path,
              rep.warnings.isEmpty() ? QString() : QStringLiteral("  (validation: %1 warning(s))").arg(rep.warnings.size()))
        : QStringLiteral("Save failed: %1").arg(rep.error));
    onValidate();
}

void EffectorStudioPanel::onLoadKee()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load Effector"),
        QString(), QStringLiteral("End effector (*.kee)"));
    if (path.isEmpty()) return;
    krs::kee::EffectorDoc doc;
    const krs::kee::Report rep = krs::kee::loadKee(path.toStdString(), doc);
    if (!rep.ok) { m_status->setText(QStringLiteral("Load failed: %1").arg(rep.error)); return; }
    m_loaded = doc;
    m_haveLoaded = true;
    m_name->setText(doc.name);
    m_tcps = doc.tcps;
    m_openQ->setValue(doc.actuation.openQ);
    m_closedQ->setValue(doc.actuation.closedQ);
    m_mimicRatio->setValue(doc.actuation.mimicRatio);
    m_gripForce->setValue(doc.actuation.maxGripForceN);
    if (!doc.actuation.driveJointName.empty())
        m_driveCombo->setCurrentText(QString::fromStdString(doc.actuation.driveJointName));
    if (!doc.actuation.mimicJointName.empty())
        m_mimicCombo->setCurrentText(QString::fromStdString(doc.actuation.mimicJointName));
    m_tcpLabel->setText(QStringLiteral("%1 TCP(s) loaded from the document.").arg(m_tcps.size()));
    m_status->setText(QStringLiteral(
        "Loaded '%1' (%2 bodies, %3 joints). Metadata is live in the panel; instancing loaded "
        "geometry into the scene needs mesh source refs (kee/1.x follow-up) — author against the "
        "CURRENT edited robot for now.").arg(doc.name).arg(doc.graph.bodies.size()).arg(doc.graph.joints.size()));
}

void EffectorStudioPanel::onAttach()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    const auto* g = reg.ctx().find<krs::rbuild::RobotGraph>();
    if (!g || g->bodies.empty()) { m_status->setText(QStringLiteral("No effector is being edited.")); return; }
    if (m_ifaceCombo->count() == 0) {
        m_status->setText(QStringLiteral("The effector needs an interface connector first (step 3)."));
        return;
    }
    if (m_attachCombo->count() == 0) {
        m_status->setText(QStringLiteral("No robot connector to attach to — place a mate connector "
                                         "on the robot's flange first."));
        return;
    }
    const QPoint iface = m_ifaceCombo->currentData().toPoint();
    const QPoint target = m_attachCombo->currentData().toPoint();
    const entt::entity ifaceBody = bodyEntity(*g, iface.x());
    const entt::entity flange = entt::entity(std::uint32_t(target.x()));
    if (!reg.valid(ifaceBody) || !reg.valid(flange)) { m_status->setText(QStringLiteral("Stale bodies — refresh.")); return; }

    auto findMc = [&reg](entt::entity e, std::uint32_t id) -> const MateConnector* {
        const auto* mcc = reg.try_get<MateConnectorComponent>(e);
        if (mcc) for (const auto& mc : mcc->connectors) if (mc.id == id) return &mc;
        return nullptr;
    };
    const MateConnector* src = findMc(ifaceBody, std::uint32_t(iface.y()));
    const MateConnector* dst = findMc(flange, std::uint32_t(target.y()));
    if (!src || !dst) { m_status->setText(QStringLiteral("Connector ids no longer resolve.")); return; }

    // world frames; MATE = source +Z ANTI-aligned onto target +Z (faces close together), X aligned.
    auto worldFrame = [&reg](entt::entity e, const MateConnector& mc, glm::vec3& p, glm::vec3& z, glm::vec3& x) {
        glm::mat4 M(1.0f);
        if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
        const glm::mat3 R(M);
        p = glm::vec3(M * glm::vec4(mc.localPos, 1.0f));
        z = glm::normalize(R * mc.localZ);
        x = glm::normalize(R * mc.localX);
    };
    glm::vec3 sp, sz, sx, dp, dz, dx;
    worldFrame(ifaceBody, *src, sp, sz, sx);
    worldFrame(flange, *dst, dp, dz, dx);
    auto basis = [](const glm::vec3& z, const glm::vec3& x) {
        const glm::vec3 xo = glm::normalize(x - glm::dot(x, z) * z);
        return glm::mat3(xo, glm::cross(z, xo), z);
    };
    const glm::mat3 Rsrc = basis(sz, sx);
    const glm::mat3 Rdst = basis(-dz, dx);                 // anti-align Z, keep X
    const glm::mat3 Rdelta = Rdst * glm::transpose(Rsrc);
    const glm::mat4 delta = glm::translate(glm::mat4(1.0f), dp)
                          * glm::mat4(Rdelta)
                          * glm::translate(glm::mat4(1.0f), -sp);

    krs::ee::Attachment att;
    att.robotId = reg.all_of<RobotSubcomponentComponent>(flange)
        ? reg.get<RobotSubcomponentComponent>(flange).robotId : -1;
    att.flangeBody = flange;
    att.connectorId = std::uint32_t(target.y());
    att.effectorName = m_name->text();
    for (int bi = 0; bi < int(g->bodies.size()); ++bi) {
        const entt::entity e = bodyEntity(*g, bi);
        if (!reg.valid(e)) continue;
        if (auto* xf = reg.try_get<TransformComponent>(e)) {
            const glm::mat4 w = delta * xf->getTransform();
            const glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
            const glm::vec3 scale(glm::length(c0), glm::length(c1), glm::length(c2));
            xf->translation = glm::vec3(w[3]);
            xf->rotation = glm::normalize(glm::quat_cast(glm::mat3(c0 / scale.x, c1 / scale.y, c2 / scale.z)));
            xf->scale = scale;
        }
        att.effectorBodies.push_back(e);
    }
    auto& registry = reg.ctx().emplace<krs::ee::AttachedEffectors>();
    registry.list.push_back(att);
    m_status->setText(QStringLiteral(
        "Attached '%1' to robot %2, mate #%3 (%4 bodies snapped). The attachment is registered — "
        "\"with end effector %3\" resolves this id.")
            .arg(att.effectorName).arg(att.robotId).arg(att.connectorId).arg(att.effectorBodies.size()));
}

void EffectorStudioPanel::onDetach()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    auto* registry = reg.ctx().find<krs::ee::AttachedEffectors>();
    if (!registry || registry->list.empty()) { m_status->setText(QStringLiteral("Nothing attached.")); return; }
    const krs::ee::Attachment att = registry->list.back();
    registry->list.pop_back();
    m_status->setText(QStringLiteral("Detached '%1' from mate #%2 (bodies stay where they are).")
                          .arg(att.effectorName).arg(att.connectorId));
}
