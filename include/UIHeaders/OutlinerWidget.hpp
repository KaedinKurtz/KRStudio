#pragma once

#include <QWidget>
#include <QString>
#include <entt/entt.hpp>

class Scene;
class QTreeWidget;
class QTimer;

/**
 * @brief Blender-style outliner: a TREE that nests each robot's bodies under a
 * named Robot root (multi-robot), lists loose objects at the top level, mirrors
 * the current selection, and clicking an entry selects that entity. Selecting a
 * robot body or its root resolves to the owning robot (robotSelected).
 */
class OutlinerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OutlinerWidget(Scene* scene, QWidget* parent = nullptr);

signals:
    /// Emitted after the user picks an entity (selection already applied).
    void selectionEdited();
    /// Emitted when the picked entity belongs to a robot (its body or its root),
    /// so the builder/view can target the right robot. -1 if not a robot.
    void robotSelected(int robotId);

private slots:
    void refresh();

private:
    Scene* m_scene = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTimer* m_refreshTimer = nullptr;
    bool m_updating = false;
    QString m_lastSig;   // structure signature; rebuild the tree only when it changes
};
