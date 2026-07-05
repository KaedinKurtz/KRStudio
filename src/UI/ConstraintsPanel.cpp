// ConstraintsPanel.cpp -- see ConstraintsPanel.hpp. The Fusion-style constraint
// authoring dock over krs::constraint (Constraint.hpp).
#include "ConstraintsPanel.hpp"
#include "Constraint.hpp"            // krs::constraint -- the gated core (anchors, snap, graph)
#include "ConstraintIconOverlay.hpp" // krs::ui::ConstraintFocusRequest (icon-click handshake)
#include "SnapSession.hpp"           // krs::snapui -- P4 connector-authoring latch + commits
#include "Scene.hpp"
#include <DockWidget.h>              // raise our own ads dock on an icon click

#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>

using krs::constraint::CType;

namespace {
QLabel* hint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    return l;
}
// order matters: index -> CType for the combo
const CType kTypes[] = {
    CType::Coincident, CType::Concentric, CType::Parallel, CType::Perpendicular,
    CType::Tangent, CType::Flush, CType::Distance, CType::Angle, CType::Rigid,
    CType::Revolute, CType::Slider, CType::Cylindrical, CType::Ball, CType::PinSlot,
};
QString featureText(const krs::sel::Selection& s) {
    if (!s.valid) return QStringLiteral("—");
    const char* kind =
        s.type == krs::sel::FeatureType::Plane      ? "plane face"  :
        s.type == krs::sel::FeatureType::Cylinder   ? "bore/cyl"    :
        s.type == krs::sel::FeatureType::Cone       ? "cone face"   :
        s.type == krs::sel::FeatureType::Sphere     ? "sphere"      :
        s.type == krs::sel::FeatureType::EdgeCircle ? "circle edge" :
        s.type == krs::sel::FeatureType::EdgeLine   ? "line edge"   :
        s.type == krs::sel::FeatureType::Vertex     ? "vertex"      : "feature";
    return QStringLiteral("%1 · entity %2").arg(QLatin1String(kind)).arg(std::uint32_t(s.entity));
}
} // namespace

ConstraintsPanel::ConstraintsPanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    root->addWidget(hint(QStringLiteral(
        "Constrain two features: latch Pick A, click a face/edge in the viewport, latch Pick B, "
        "click the second feature, choose the relation, Apply. Body B snaps to satisfy it. "
        "KINEMATIC types (Revolute, Slider, …) leave their DOF free — on Play they become physics "
        "joints, so an undriven linkage articulates when the robot pushes it."), this));

    auto* rowA = new QHBoxLayout();
    m_pickABtn = new QPushButton(QStringLiteral("Pick A"), this);
    m_pickABtn->setCheckable(true);
    m_pickALabel = new QLabel(QStringLiteral("—"), this);
    rowA->addWidget(m_pickABtn); rowA->addWidget(m_pickALabel, 1);
    root->addLayout(rowA);
    auto* rowB = new QHBoxLayout();
    m_pickBBtn = new QPushButton(QStringLiteral("Pick B (this body snaps)"), this);
    m_pickBBtn->setCheckable(true);
    m_pickBLabel = new QLabel(QStringLiteral("—"), this);
    rowB->addWidget(m_pickBBtn); rowB->addWidget(m_pickBLabel, 1);
    root->addLayout(rowB);

    m_type = new QComboBox(this);
    for (CType t : kTypes) m_type->addItem(QString::fromStdString(krs::constraint::cTypeName(t)));
    root->addWidget(m_type);

    m_offsetRow = new QWidget(this);
    { auto* h = new QHBoxLayout(m_offsetRow); h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel(QStringLiteral("Offset (m):"), m_offsetRow));
      m_offset = new QDoubleSpinBox(m_offsetRow);
      m_offset->setRange(-100.0, 100.0); m_offset->setDecimals(4); m_offset->setSingleStep(0.01);
      h->addWidget(m_offset, 1); }
    root->addWidget(m_offsetRow);
    m_angleRow = new QWidget(this);
    { auto* h = new QHBoxLayout(m_angleRow); h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(new QLabel(QStringLiteral("Angle (°):"), m_angleRow));
      m_angle = new QDoubleSpinBox(m_angleRow);
      m_angle->setRange(-360.0, 360.0); m_angle->setDecimals(2); m_angle->setSingleStep(5.0);
      h->addWidget(m_angle, 1); }
    root->addWidget(m_angleRow);

    m_applyBtn = new QPushButton(QStringLiteral("Apply Constraint"), this);
    m_applyBtn->setEnabled(false);
    root->addWidget(m_applyBtn);

    // ---- persistent MATE-CONNECTOR authoring (mate-selector P4) ----
    root->addWidget(hint(QStringLiteral(
        "Or author a durable MATE CONNECTOR: latch, hover a face -- inference dots wake up "
        "(square = centroid, plus = centre, triangle = midpoint, circle = vertex, diamond = "
        "axis) -- and click the one you want. F flips +Z, R turns the secondary axis 90°, "
        "Shift locks the face, Ctrl reveals hole axes."), this));
    m_placeConnBtn = new QPushButton(QStringLiteral("Place Mate Connector"), this);
    m_placeConnBtn->setCheckable(true);
    root->addWidget(m_placeConnBtn);

    root->addWidget(hint(QStringLiteral("Constraints in the scene (select to suppress/delete; "
                                        "click a hovering icon in the viewport to jump here):"), this));
    m_list = new QListWidget(this);
    root->addWidget(m_list, 1);
    auto* rowOps = new QHBoxLayout();
    m_suppress = new QCheckBox(QStringLiteral("Suppressed"), this);
    auto* delBtn = new QPushButton(QStringLiteral("Delete"), this);
    m_showIcons = new QCheckBox(QStringLiteral("Show icons"), this);
    m_showIcons->setChecked(true);
    rowOps->addWidget(m_suppress); rowOps->addWidget(delBtn); rowOps->addStretch(1); rowOps->addWidget(m_showIcons);
    root->addLayout(rowOps);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#bbbbbb;"));
    root->addWidget(m_status);

    connect(m_pickABtn, &QPushButton::toggled, this, &ConstraintsPanel::onPickA);
    connect(m_pickBBtn, &QPushButton::toggled, this, &ConstraintsPanel::onPickB);
    connect(m_placeConnBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        auto& ss = krs::snapui::snapSession(reg);
        ss.connectorAuthoring = on;
        ss.hasResult = false;                                   // stale commits never fire the latch
        auto* st = reg.ctx().find<krs::sel::SelectionState>();
        if (!st) st = &reg.ctx().emplace<krs::sel::SelectionState>();
        if (on) {
            if (m_pickABtn->isChecked()) { QSignalBlocker b(m_pickABtn); m_pickABtn->setChecked(false); }
            if (m_pickBBtn->isChecked()) { QSignalBlocker b(m_pickBBtn); m_pickBBtn->setChecked(false); }
            m_armWhich = 0;
            st->enabled = true; st->fifoTwoBores = false; st->measureMode = false; st->boreQuota = 0;
            m_status->setText(QStringLiteral("Hover a face -- dots wake up; click the inference "
                                             "point you want (F flip, R rotate, Shift lock, Ctrl axes)."));
        } else {
            st->enabled = false;
        }
    });
    connect(m_applyBtn, &QPushButton::clicked, this, &ConstraintsPanel::onApply);
    connect(delBtn, &QPushButton::clicked, this, &ConstraintsPanel::onDeleteSelected);
    connect(m_suppress, &QCheckBox::toggled, this, &ConstraintsPanel::onSuppressToggled);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConstraintsPanel::onTypeChanged);
    connect(m_showIcons, &QCheckBox::toggled, this, [this](bool on) {
        if (m_scene) krs::constraint::constraintGraph(m_scene->getRegistry()).showIcons = on;
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_scene || row < 0) return;
        auto& g = krs::constraint::constraintGraph(m_scene->getRegistry());
        if (row < int(g.constraints.size())) {
            QSignalBlocker b(m_suppress);
            m_suppress->setChecked(g.constraints[std::size_t(row)].suppressed);
        }
    });
    onTypeChanged(0);

    m_tick = new QTimer(this);
    m_tick->setInterval(200);
    connect(m_tick, &QTimer::timeout, this, &ConstraintsPanel::onTick);
    m_tick->start();
}

void ConstraintsPanel::onPickA(bool on)
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st) st = &reg.ctx().emplace<krs::sel::SelectionState>();
    if (on) {
        if (m_pickBBtn->isChecked()) { QSignalBlocker b(m_pickBBtn); m_pickBBtn->setChecked(false); }
        krs::sel::clearSelection(*st);
        st->enabled = true; st->fifoTwoBores = false; st->measureMode = false; st->boreQuota = 0;
        m_armWhich = 1;
        m_status->setText(QStringLiteral("Click the FIRST feature in the viewport (faces and edges pick)."));
    } else if (m_armWhich == 1) {
        st->enabled = false;
        m_armWhich = 0;
    }
}

void ConstraintsPanel::onPickB(bool on)
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st) st = &reg.ctx().emplace<krs::sel::SelectionState>();
    if (on) {
        if (m_pickABtn->isChecked()) { QSignalBlocker b(m_pickABtn); m_pickABtn->setChecked(false); }
        krs::sel::clearSelection(*st);
        st->enabled = true; st->fifoTwoBores = false; st->measureMode = false; st->boreQuota = 0;
        m_armWhich = 2;
        m_status->setText(QStringLiteral("Click the SECOND feature — its body is the one that snaps."));
    } else if (m_armWhich == 2) {
        st->enabled = false;
        m_armWhich = 0;
    }
}

void ConstraintsPanel::onTick()
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    // Harvest the armed pick: the newest committed selection becomes A or B, then disarm.
    if (m_armWhich != 0) {
        if (auto* st = reg.ctx().find<krs::sel::SelectionState>()) {
            const krs::sel::Selection* newest = nullptr;
            for (auto it = st->selected.rbegin(); it != st->selected.rend(); ++it)
                if (it->valid) { newest = &*it; break; }
            if (newest) {
                if (m_armWhich == 1) { m_selA = *newest; m_haveA = true;
                                       QSignalBlocker b(m_pickABtn); m_pickABtn->setChecked(false); }
                else                 { m_selB = *newest; m_haveB = true;
                                       QSignalBlocker b(m_pickBBtn); m_pickBBtn->setChecked(false); }
                m_armWhich = 0;
                st->enabled = false;
                krs::sel::clearSelection(*st);
                updatePickLabels();
                m_status->setText((m_haveA && m_haveB)
                    ? QStringLiteral("Both features picked — choose the relation and Apply.")
                    : QStringLiteral("First feature locked in. Now latch the other pick."));
            }
        }
    }
    // P4: a snap commit while the Place-Connector latch is armed becomes a PERSISTENT
    // MateConnector on the picked body (body-LOCAL frame + durable feature key -- the
    // Onshape implicit-connector persistence model).
    {
        auto& ss = krs::snapui::snapSession(reg);
        if (ss.connectorAuthoring && ss.hasResult) {
            ss.hasResult = false;
            const krs::snap::SnapCandidate c = ss.result;
            if (reg.valid(c.entity)) {
                glm::mat4 inv(1.0f);
                if (const auto* xf = reg.try_get<TransformComponent>(c.entity))
                    inv = glm::inverse(xf->getTransform());
                auto& mcc = reg.get_or_emplace<MateConnectorComponent>(c.entity);
                MateConnector mc;
                mc.id = mcc.nextConnectorId++;
                mc.name = "MC_" + std::to_string(mc.id);
                mc.localPos = glm::vec3(inv * glm::vec4(c.pos, 1.0f));
                const glm::mat3 invR(inv);
                mc.localZ = glm::normalize(invR * c.z);
                mc.localX = glm::normalize(invR * c.x);
                mc.sourceFaceKey = c.key;
                mc.sourceFaceType = (c.edgeId >= 0) ? 1 : 0;   // edge-derived vs face-derived
                mcc.connectors.push_back(mc);
                m_status->setText(QStringLiteral("Connector %1 placed on entity %2 (key %3) -- it "
                                                 "re-anchors across re-import and rides the body.")
                                      .arg(QString::fromStdString(mc.name))
                                      .arg(std::uint32_t(c.entity))
                                      .arg(QString::number(c.key, 16)));
                QSignalBlocker b(m_placeConnBtn);              // one placement per latch
                m_placeConnBtn->setChecked(false);
                ss.connectorAuthoring = false;
                if (auto* st2 = reg.ctx().find<krs::sel::SelectionState>()) st2->enabled = false;
            }
        }
    }

    m_applyBtn->setEnabled(m_haveA && m_haveB);
    refreshList();
    // keep the icons checkbox honest if something else flips the ctx flag
    const bool icons = krs::constraint::constraintGraph(reg).showIcons;
    if (m_showIcons->isChecked() != icons) { QSignalBlocker b(m_showIcons); m_showIcons->setChecked(icons); }
    // an in-scene icon click parked a focus request in the ctx -- take it and surface ourselves
    if (auto* fr = reg.ctx().find<krs::ui::ConstraintFocusRequest>(); fr && fr->id != 0) {
        const std::uint64_t id = fr->id;
        fr->id = 0;
        focusConstraint(id);
        for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
            if (auto* dock = qobject_cast<ads::CDockWidget*>(w)) {
                dock->toggleView(true);
                dock->setAsCurrentTab();
                dock->raise();
                break;
            }
        }
    }
}

void ConstraintsPanel::updatePickLabels()
{
    m_pickALabel->setText(featureText(m_selA));
    m_pickBLabel->setText(featureText(m_selB));
}

void ConstraintsPanel::onTypeChanged(int idx)
{
    const CType t = kTypes[std::size_t(std::clamp(idx, 0, int(std::size(kTypes)) - 1))];
    m_offsetRow->setVisible(t == CType::Distance || t == CType::Flush || t == CType::Concentric
                            || t == CType::Revolute || t == CType::Slider || t == CType::Cylindrical);
    m_angleRow->setVisible(t == CType::Angle);
}

void ConstraintsPanel::onApply()
{
    if (!m_scene || !m_haveA || !m_haveB) return;
    auto& reg = m_scene->getRegistry();
    auto& g = krs::constraint::constraintGraph(reg);

    krs::constraint::Constraint c;
    c.id = g.nextId++;
    c.type = kTypes[std::size_t(std::clamp(m_type->currentIndex(), 0, int(std::size(kTypes)) - 1))];
    c.offset = m_offset->value();
    c.angleDeg = m_angle->value();
    c.a = krs::constraint::makeAnchor(reg, m_selA);
    c.b = krs::constraint::makeAnchor(reg, m_selB);
    if (c.a.body == entt::null || c.b.body == entt::null) {
        m_status->setText(QStringLiteral("A pick no longer resolves (body deleted?). Re-pick and try again."));
        return;
    }
    if (!krs::constraint::applyConstraintSnap(reg, c)) {
        m_status->setText(QStringLiteral("Refused: robot links are kinematically owned (constrain loose "
                                         "bodies; interact with robots through physics), or the pair is degenerate."));
        return;
    }
    g.constraints.push_back(c);
    m_haveA = m_haveB = false;
    m_selA = {}; m_selB = {};
    updatePickLabels();
    m_lastListSig.clear();
    refreshList();
    m_status->setText(QStringLiteral("%1 applied — body snapped; icon hovers at the anchor. Kinematic "
                                     "types articulate under Play.")
                          .arg(QString::fromStdString(krs::constraint::cTypeName(c.type))));
}

void ConstraintsPanel::onDeleteSelected()
{
    if (!m_scene) return;
    auto& g = krs::constraint::constraintGraph(m_scene->getRegistry());
    const int row = m_list->currentRow();
    if (row < 0 || row >= int(g.constraints.size())) return;
    g.constraints.erase(g.constraints.begin() + row);
    m_lastListSig.clear();
    refreshList();
    m_status->setText(QStringLiteral("Constraint deleted (bodies stay where they are)."));
}

void ConstraintsPanel::onSuppressToggled(bool on)
{
    if (!m_scene) return;
    auto& g = krs::constraint::constraintGraph(m_scene->getRegistry());
    const int row = m_list->currentRow();
    if (row < 0 || row >= int(g.constraints.size())) return;
    g.constraints[std::size_t(row)].suppressed = on;
    m_lastListSig.clear();
    refreshList();
}

void ConstraintsPanel::focusConstraint(std::uint64_t id)
{
    if (!m_scene) return;
    auto& g = krs::constraint::constraintGraph(m_scene->getRegistry());
    for (std::size_t i = 0; i < g.constraints.size(); ++i)
        if (g.constraints[i].id == id) { m_list->setCurrentRow(int(i)); break; }
}

void ConstraintsPanel::refreshList()
{
    if (!m_scene) return;
    auto& g = krs::constraint::constraintGraph(m_scene->getRegistry());
    QString sig;
    for (const auto& c : g.constraints)
        sig += QString::number(c.id) + (c.suppressed ? QLatin1Char('s') : QLatin1Char('a'));
    if (sig == m_lastListSig) return;
    m_lastListSig = sig;
    const int keep = m_list->currentRow();
    m_list->clear();
    for (const auto& c : g.constraints) {
        QString line = QString::fromStdString(krs::constraint::describe(c));
        if (c.suppressed) line += QStringLiteral("   [suppressed]");
        m_list->addItem(line);
    }
    if (keep >= 0 && keep < m_list->count()) m_list->setCurrentRow(keep);
}
