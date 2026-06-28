#include "OutlinerWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RobotModel.hpp"   // krs::robot::groupByRobot / SceneGrouping

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>

#include <functional>
#include <cstdint>

namespace {
constexpr int kEntityRole = Qt::UserRole;
constexpr int kRobotIdRole = Qt::UserRole + 1;

entt::entity itemEntity(const QTreeWidgetItem* it) {
    return it ? entt::entity(it->data(0, kEntityRole).toUInt()) : entt::null;
}
} // namespace

OutlinerWidget::OutlinerWidget(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
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
            const int rid = item->data(0, kRobotIdRole).toInt();
            if (rid >= 0 && robotId < 0) robotId = rid;   // first robot-affiliated pick
        }
        emit selectionEdited();
        emit robotSelected(robotId);
    });

    // Right-click: rename / delete (robot roots rename the Robot; bodies/loose as before).
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
                QLineEdit::Normal, item->text(0), &ok);
            if (ok && !text.isEmpty()) {
                auto& r = m_scene->getRegistry();
                r.emplace_or_replace<TagComponent>(e, text.toStdString());
                if (auto* rr = r.try_get<RobotRootComponent>(e)) rr->name = text.toStdString();
                m_lastSig.clear();   // force a rebuild
                refresh();
            }
        });
        menu.addAction(QStringLiteral("Delete"), [this, e]() {
            auto& r = m_scene->getRegistry();
            if (r.valid(e) && !r.any_of<CameraComponent, GridComponent>(e)) {
                r.destroy(e);
                m_lastSig.clear();
                refresh();
                emit selectionEdited();
            }
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

    // Structure signature (NOT selection): rebuild only when the tree shape changes,
    // so selection sync + user expansion are preserved between refreshes.
    QString sig;
    for (const auto& rb : g.robots) {
        sig += QStringLiteral("R%1:%2[").arg(rb.robotId).arg(QString::fromStdString(rb.name));
        for (auto e : rb.bodies) {
            const auto* tag = reg.try_get<TagComponent>(e);
            sig += QString::number(std::uint32_t(e)) + QLatin1Char('=')
                 + (tag ? QString::fromStdString(tag->tag) : QString()) + QLatin1Char(',');
        }
        sig += QLatin1Char(']');
    }
    for (auto e : g.loose) {
        const auto* tag = reg.try_get<TagComponent>(e);
        sig += QStringLiteral("L%1=%2;").arg(std::uint32_t(e)).arg(tag ? QString::fromStdString(tag->tag) : QString());
    }

    m_updating = true;
    if (sig != m_lastSig) {
        m_lastSig = sig;
        m_tree->clear();

        auto addLeaf = [&](QTreeWidgetItem* parent, entt::entity e, const QString& label, int robotId) {
            auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            it->setText(0, label);
            it->setData(0, kEntityRole, std::uint32_t(e));
            it->setData(0, kRobotIdRole, robotId);
            return it;
        };

        for (const auto& rb : g.robots) {
            QTreeWidgetItem* rootItem = addLeaf(nullptr, rb.root,
                QStringLiteral("%1  (robot · %2 bodies)").arg(QString::fromStdString(rb.name)).arg(rb.bodies.size()),
                rb.robotId);
            for (auto e : rb.bodies) {
                const auto* tag = reg.try_get<TagComponent>(e);
                addLeaf(rootItem, e, tag ? QString::fromStdString(tag->tag) : QStringLiteral("body"), rb.robotId);
            }
            rootItem->setExpanded(true);
        }
        for (auto e : g.loose) {
            const auto* tag = reg.try_get<TagComponent>(e);
            if (!tag || tag->tag.empty()) continue;
            addLeaf(nullptr, e, QString::fromStdString(tag->tag), -1);
        }
    }

    // Sync selection state in-place (no rebuild) from the ECS.
    std::function<void(QTreeWidgetItem*)> syncSel = [&](QTreeWidgetItem* it) {
        const entt::entity e = itemEntity(it);
        it->setSelected(reg.valid(e) && reg.any_of<SelectedComponent>(e));
        for (int i = 0; i < it->childCount(); ++i) syncSel(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) syncSel(m_tree->topLevelItem(i));

    m_updating = false;
}
