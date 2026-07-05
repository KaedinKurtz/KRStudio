// OutlinerWidget.cpp -- the Blender-style scene explorer (rebuild).
// Segmented, nested tree: GROUPS (macro objects, nesting recursively) / ROBOTS
// (root + links) / LIGHTS / OBJECTS. Three columns: Name | Type | visibility
// eye (toggles HiddenComponent -> the entity leaves the render AND the pick).
// Ctrl/Shift multi-span selection is native (ExtendedSelection) and mirrors
// into the ECS SelectedComponent set both ways.
#include "OutlinerWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "GroupOps.hpp"     // krs::group -- nesting + leaf expansion
#include "RobotModel.hpp"   // krs::robot::groupByRobot / SceneGrouping

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include <functional>
#include <cstdint>

namespace {
constexpr int kEntityRole  = Qt::UserRole;
constexpr int kRobotIdRole = Qt::UserRole + 1;
constexpr int kColName = 0, kColType = 1, kColEye = 2;

entt::entity itemEntity(const QTreeWidgetItem* it) {
    return it ? entt::entity(it->data(kColName, kEntityRole).toUInt()) : entt::null;
}

QString typeOf(entt::registry& reg, entt::entity e) {
    if (reg.all_of<GroupComponent>(e))             return QStringLiteral("Group");
    if (reg.all_of<RobotRootComponent>(e))         return QStringLiteral("Robot");
    if (reg.all_of<RobotSubcomponentComponent>(e)) return QStringLiteral("Link");
    if (reg.all_of<LightComponent>(e))             return QStringLiteral("Light");
    if (reg.all_of<FluidEmitterComponent>(e))      return QStringLiteral("Emitter");
    if (reg.all_of<FluidVolumeComponent>(e))       return QStringLiteral("Fluid");
    if (reg.all_of<CameraComponent>(e))            return QStringLiteral("Camera");
    if (reg.all_of<RenderableMeshComponent>(e))    return QStringLiteral("Mesh");
    return QStringLiteral("Empty");
}

bool entityHidden(entt::registry& reg, entt::entity e) {
    if (reg.all_of<GroupComponent>(e)) {
        const auto* gc = reg.try_get<GroupComponent>(e);
        return gc && !gc->visible;
    }
    return reg.all_of<HiddenComponent>(e);
}

// Toggle visibility: a plain entity flips its HiddenComponent; a GROUP flips every leaf below it
// (and records the state on the root for the eye glyph).
void setEntityHidden(entt::registry& reg, entt::entity e, bool hide) {
    if (auto* gc = reg.try_get<GroupComponent>(e)) {
        gc->visible = !hide;
        for (entt::entity m : krs::group::leafTargets(reg, e)) {
            if (hide) reg.emplace_or_replace<HiddenComponent>(m);
            else      reg.remove<HiddenComponent>(m);
        }
        // nested roots record the state too, so their eye glyphs agree
        for (entt::entity m : krs::group::groupMembers(reg, e))
            if (auto* mg = reg.try_get<GroupComponent>(m)) mg->visible = !hide;
        return;
    }
    if (hide) reg.emplace_or_replace<HiddenComponent>(e);
    else      reg.remove<HiddenComponent>(e);
}
} // namespace

OutlinerWidget::OutlinerWidget(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Type"), QString() });
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);   // ctrl = add, shift = span
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kColType, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kColEye, QHeaderView::Fixed);
    m_tree->setColumnWidth(kColEye, 28);
    layout->addWidget(m_tree);

    // Selection -> ECS SelectedComponent, and resolve the owning robot.
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_updating) return;
        auto& reg = m_scene->getRegistry();
        for (auto e : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(e);
        int robotId = -1;
        for (QTreeWidgetItem* item : m_tree->selectedItems()) {
            const entt::entity e = itemEntity(item);
            if (reg.valid(e)) reg.emplace_or_replace<SelectedComponent>(e);
            const int rid = item->data(kColName, kRobotIdRole).toInt();
            if (rid >= 0 && robotId < 0) robotId = rid;   // first robot-affiliated pick
        }
        emit selectionEdited();
        emit robotSelected(robotId);
    });

    // The EYE column: click toggles visibility (whole subtree for groups/robots).
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int col) {
        if (col != kColEye || m_updating) return;
        auto& reg = m_scene->getRegistry();
        const entt::entity e = itemEntity(item);
        if (!reg.valid(e)) return;
        const bool hide = !entityHidden(reg, e);
        setEntityHidden(reg, e, hide);
        // a ROBOT root eye hides/shows every link (render-only; kinematics unaffected)
        if (const auto* rootC = reg.try_get<RobotRootComponent>(e)) {
            for (auto le : reg.view<RobotSubcomponentComponent>())
                if (reg.get<RobotSubcomponentComponent>(le).robotId == rootC->robotId)
                    setEntityHidden(reg, le, hide);
        }
        m_lastSig.clear();     // visibility is part of the signature -> refresh glyphs
        refresh();
    });

    // Right-click: rename / visibility / ungroup / delete.
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_tree->itemAt(pos);
        if (!item) return;
        const entt::entity e = itemEntity(item);
        auto& reg = m_scene->getRegistry();
        if (!reg.valid(e)) return;

        QMenu menu(this);
        menu.addAction(QStringLiteral("Rename…"), [this, e, item]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, QStringLiteral("Rename"), QStringLiteral("Name:"),
                QLineEdit::Normal, item->text(kColName), &ok);
            if (ok && !text.isEmpty()) {
                auto& r = m_scene->getRegistry();
                r.emplace_or_replace<TagComponent>(e, text.toStdString());
                if (auto* rr = r.try_get<RobotRootComponent>(e)) rr->name = text.toStdString();
                if (auto* gc = r.try_get<GroupComponent>(e))     gc->name = text.toStdString();
                m_lastSig.clear();
                refresh();
            }
        });
        menu.addAction(entityHidden(reg, e) ? QStringLiteral("Show") : QStringLiteral("Hide"),
                       [this, e]() {
            auto& r = m_scene->getRegistry();
            setEntityHidden(r, e, !entityHidden(r, e));
            m_lastSig.clear();
            refresh();
        });
        if (reg.all_of<GroupComponent>(e)) {
            menu.addAction(QStringLiteral("Ungroup"), [this, e]() {
                auto& r = m_scene->getRegistry();
                krs::group::ungroup(r, e);
                m_lastSig.clear();
                refresh();
                emit selectionEdited();
            });
        }
        menu.addAction(QStringLiteral("Delete"), [this, e]() {
            auto& r = m_scene->getRegistry();
            if (!r.valid(e) || r.any_of<CameraComponent, GridComponent>(e)) return;
            // ROBOT-MEMBER GUARD: destroying a link solid leaves the joint listed and drivable
            // while its geometry silently vanishes. Route through the Robot Builder instead.
            if (r.any_of<RobotSubcomponentComponent, RobotRootComponent>(e)) {
                QMessageBox::information(this, QStringLiteral("Part of a robot"),
                    QStringLiteral("This entity belongs to a robot's kinematic chain and can't be "
                                   "deleted on its own.\nCut a joint in the Robot Builder to detach "
                                   "a subtree instead."));
                return;
            }
            if (r.all_of<GroupComponent>(e)) {              // group: the whole subtree goes
                std::vector<entt::entity> doomed = krs::group::leafTargets(r, e);
                for (entt::entity m : krs::group::groupMembers(r, e))
                    if (r.all_of<GroupComponent>(m)) doomed.push_back(m);
                doomed.push_back(e);
                for (entt::entity d : doomed)
                    if (r.valid(d) && !r.any_of<CameraComponent, GridComponent,
                                                RobotSubcomponentComponent, RobotRootComponent>(d))
                        r.destroy(d);
            } else {
                r.destroy(e);
            }
            m_lastSig.clear();
            refresh();
            emit selectionEdited();
        });
        menu.exec(m_tree->mapToGlobal(pos));
    });

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &OutlinerWidget::refresh);
    m_refreshTimer->start(500);
    refresh();
}

void OutlinerWidget::refresh()
{
    if (!isVisible() || !m_scene) return;
    auto& reg = m_scene->getRegistry();
    const krs::robot::SceneGrouping g = krs::robot::groupByRobot(reg);

    // Partition the loose entities: group members render under their group; group roots and the
    // rest split into lights vs plain objects.
    std::vector<entt::entity> topGroups, lights, objects;
    for (auto e : reg.view<GroupComponent>())
        if (krs::group::topGroupOf(reg, e) == e) topGroups.push_back(e);
    for (auto e : g.loose) {
        if (reg.all_of<GroupComponent>(e)) continue;          // group roots handled above
        if (reg.all_of<GroupMemberComponent>(e)) continue;    // members nest under their group
        const auto* tag = reg.try_get<TagComponent>(e);
        if (!tag || tag->tag.empty()) continue;
        (reg.all_of<LightComponent>(e) ? lights : objects).push_back(e);
    }

    // Structure signature (name + type + visibility + nesting): rebuild only on change so user
    // expansion + in-progress interactions survive the 2 Hz refresh.
    QString sig;
    auto sigOf = [&](entt::entity e) {
        const auto* tag = reg.try_get<TagComponent>(e);
        return QString::number(std::uint32_t(e)) + QLatin1Char('=')
             + (tag ? QString::fromStdString(tag->tag) : QString())
             + (entityHidden(reg, e) ? QLatin1Char('h') : QLatin1Char('v'));
    };
    std::function<void(entt::entity)> sigGroup = [&](entt::entity root) {
        sig += QStringLiteral("G[") + sigOf(root);
        for (entt::entity m : krs::group::groupMembers(reg, root)) {
            if (reg.all_of<GroupComponent>(m)) sigGroup(m);
            else sig += sigOf(m) + QLatin1Char(',');
        }
        sig += QLatin1Char(']');
    };
    for (entt::entity e : topGroups) sigGroup(e);
    for (const auto& rb : g.robots) {
        sig += QStringLiteral("R%1:%2[").arg(rb.robotId).arg(QString::fromStdString(rb.name));
        for (auto e : rb.bodies) sig += sigOf(e) + QLatin1Char(',');
        sig += QLatin1Char(']');
    }
    for (auto e : lights)  sig += QStringLiteral("L") + sigOf(e);
    for (auto e : objects) sig += QStringLiteral("O") + sigOf(e);

    m_updating = true;
    if (sig != m_lastSig) {
        m_lastSig = sig;
        m_tree->clear();

        auto addSection = [&](const QString& title) {
            auto* it = new QTreeWidgetItem(m_tree);
            it->setText(kColName, title);
            it->setFlags(Qt::ItemIsEnabled);                  // not selectable: a header, not a node
            QFont f = it->font(kColName); f.setBold(true); it->setFont(kColName, f);
            it->setForeground(kColName, QColor(0x9a, 0x9a, 0x9a));
            // entt::null sentinel -- entity id 0 is a REAL entity (the first one created)
            it->setData(kColName, kEntityRole, std::uint32_t(entt::entity(entt::null)));
            return it;
        };
        auto addRow = [&](QTreeWidgetItem* parent, entt::entity e, const QString& label, int robotId) {
            auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            it->setText(kColName, label);
            it->setText(kColType, typeOf(reg, e));
            it->setForeground(kColType, QColor(0x8a, 0x90, 0x9a));
            it->setText(kColEye, entityHidden(reg, e) ? QStringLiteral("‒") : QStringLiteral("👁"));
            it->setTextAlignment(kColEye, Qt::AlignCenter);
            it->setToolTip(kColEye, QStringLiteral("Show/hide (hidden objects are unclickable too)"));
            it->setData(kColName, kEntityRole, std::uint32_t(e));
            it->setData(kColName, kRobotIdRole, robotId);
            return it;
        };
        auto nameOf = [&](entt::entity e) {
            const auto* tag = reg.try_get<TagComponent>(e);
            return tag && !tag->tag.empty() ? QString::fromStdString(tag->tag) : QStringLiteral("(unnamed)");
        };

        if (!topGroups.empty()) {
            QTreeWidgetItem* sec = addSection(QStringLiteral("Groups"));
            std::function<void(QTreeWidgetItem*, entt::entity)> addGroup =
                [&](QTreeWidgetItem* parent, entt::entity root) {
                    QTreeWidgetItem* gi = addRow(parent, root, nameOf(root), -1);
                    for (entt::entity m : krs::group::groupMembers(reg, root)) {
                        if (reg.all_of<GroupComponent>(m)) addGroup(gi, m);
                        else addRow(gi, m, nameOf(m), -1);
                    }
                    gi->setExpanded(true);
                };
            for (entt::entity e : topGroups) addGroup(sec, e);
            sec->setExpanded(true);
        }
        if (!g.robots.empty()) {
            QTreeWidgetItem* sec = addSection(QStringLiteral("Robots"));
            for (const auto& rb : g.robots) {
                QTreeWidgetItem* rootItem = addRow(sec, rb.root,
                    QStringLiteral("%1  (%2 bodies)").arg(QString::fromStdString(rb.name)).arg(rb.bodies.size()),
                    rb.robotId);
                for (auto e : rb.bodies) addRow(rootItem, e, nameOf(e), rb.robotId);
                rootItem->setExpanded(false);                 // links collapsed by default (Blender-ish)
            }
            sec->setExpanded(true);
        }
        if (!lights.empty()) {
            QTreeWidgetItem* sec = addSection(QStringLiteral("Lights"));
            for (auto e : lights) addRow(sec, e, nameOf(e), -1);
            sec->setExpanded(true);
        }
        if (!objects.empty()) {
            QTreeWidgetItem* sec = addSection(QStringLiteral("Objects"));
            for (auto e : objects) addRow(sec, e, nameOf(e), -1);
            sec->setExpanded(true);
        }
    }

    // Sync selection state in-place (no rebuild) from the ECS.
    std::function<void(QTreeWidgetItem*)> syncSel = [&](QTreeWidgetItem* it) {
        const entt::entity e = itemEntity(it);
        if (e != entt::null)
            it->setSelected(reg.valid(e) && reg.any_of<SelectedComponent>(e));
        for (int i = 0; i < it->childCount(); ++i) syncSel(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) syncSel(m_tree->topLevelItem(i));

    m_updating = false;
}
