#pragma once

#include <QWidget>
#include <entt/entt.hpp>

class Scene;
class QListWidget;
class QTimer;

/**
 * @brief Blender-style outliner: lists every named scene object, mirrors the
 * current selection, and clicking an entry selects that entity.
 */
class OutlinerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OutlinerWidget(Scene* scene, QWidget* parent = nullptr);

signals:
    /// Emitted after the user picks an entity (selection already applied).
    void selectionEdited();

private slots:
    void refresh();

private:
    Scene* m_scene = nullptr;
    QListWidget* m_list = nullptr;
    QTimer* m_refreshTimer = nullptr;
    bool m_updating = false;
};
