#include "OutlinerWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "GizmoSystem.hpp" // GizmoHandleComponent (filtered out of the list)

#include <QVBoxLayout>
#include <QListWidget>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>

OutlinerWidget::OutlinerWidget(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        if (m_updating) return;
        auto& reg = m_scene->getRegistry();
        for (auto e : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(e);
        for (auto* item : m_list->selectedItems()) {
            const auto e = entt::entity(item->data(Qt::UserRole).toUInt());
            if (reg.valid(e)) reg.emplace_or_replace<SelectedComponent>(e);
        }
        emit selectionEdited();
    });

    // Right-click: rename / delete straight from the list.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_list->itemAt(pos);
        if (!item) return;
        const auto e = entt::entity(item->data(Qt::UserRole).toUInt());
        auto& reg = m_scene->getRegistry();
        if (!reg.valid(e)) return;

        QMenu menu(this);
        menu.addAction(QStringLiteral("Rename…"), [this, e, item]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, QStringLiteral("Rename"), QStringLiteral("Name:"),
                QLineEdit::Normal, item->text(), &ok);
            if (ok && !text.isEmpty()) {
                m_scene->getRegistry().emplace_or_replace<TagComponent>(e, text.toStdString());
                refresh();
            }
        });
        menu.addAction(QStringLiteral("Delete"), [this, e]() {
            auto& r = m_scene->getRegistry();
            if (r.valid(e) && !r.any_of<CameraComponent, GridComponent>(e)) {
                r.destroy(e);
                refresh();
                emit selectionEdited();
            }
        });
        menu.exec(m_list->mapToGlobal(pos));
    });

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &OutlinerWidget::refresh);
    m_refreshTimer->start(500);
    refresh();
}

void OutlinerWidget::refresh()
{
    if (!isVisible() || !m_scene) return;
    m_updating = true;

    auto& reg = m_scene->getRegistry();

    // Gather (name, entity, selected) of everything user-meaningful.
    struct Row { QString name; entt::entity e; bool selected; };
    std::vector<Row> rows;
    for (auto e : reg.view<TagComponent>()) {
        if (reg.any_of<GizmoHandleComponent>(e)) continue; // internal
        const auto& tag = reg.get<TagComponent>(e).tag;
        if (tag.empty()) continue;
        rows.push_back({ QString::fromStdString(tag), e, reg.any_of<SelectedComponent>(e) });
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.name < b.name; });

    // Only rebuild when content changed (keeps interaction smooth).
    bool dirty = (m_list->count() != int(rows.size()));
    if (!dirty) {
        for (int i = 0; i < m_list->count(); ++i) {
            auto* item = m_list->item(i);
            if (item->text() != rows[i].name ||
                entt::entity(item->data(Qt::UserRole).toUInt()) != rows[i].e ||
                item->isSelected() != rows[i].selected) { dirty = true; break; }
        }
    }
    if (dirty) {
        m_list->clear();
        for (const auto& r : rows) {
            auto* item = new QListWidgetItem(r.name, m_list);
            item->setData(Qt::UserRole, uint32_t(r.e));
            item->setSelected(r.selected);
        }
    }

    m_updating = false;
}
