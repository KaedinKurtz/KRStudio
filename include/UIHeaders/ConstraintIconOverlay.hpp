#pragma once
// ===========================================================================
// CONSTRAINT ICON OVERLAY -- small hovering icons in the viewport, one per
// constraint, pinned (screen-space) to the constraint's anchor midpoint.
// Clickable: jumps to the constraint in the Constraints panel. Toggled
// globally by ConstraintGraphComponent.showIcons (the panel checkbox).
// Same overlay technique as the MeasureHud: plain Qt widgets over the GL
// viewport, theme-matched, no render-pass work.
// ===========================================================================
#include <QWidget>
#include <QVector>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

class Scene;
class QToolButton;
class QTimer;

namespace krs::ui {
// ctx handshake: an icon click writes the constraint id here; the ConstraintsPanel's poll picks it
// up, focuses the row and raises its dock. No cross-widget wiring, works from every viewport.
struct ConstraintFocusRequest { std::uint64_t id = 0; };
} // namespace krs::ui

class ConstraintIconOverlay : public QObject
{
    Q_OBJECT
public:
    // projector: world point -> screen pos in viewport coords; returns false when behind the camera.
    using Projector = std::function<bool(const glm::vec3&, QPoint&)>;
    ConstraintIconOverlay(Scene* scene, QWidget* viewport, Projector projector);

private:
    void tick();                                // 10 Hz: project anchors -> move/show/hide icons

    Scene* m_scene = nullptr;
    QWidget* m_viewport = nullptr;
    Projector m_project;
    QVector<QToolButton*> m_pool;               // reused icon buttons (grown on demand)
    QTimer* m_timer = nullptr;
};
