// ConstraintIconOverlay.cpp -- see ConstraintIconOverlay.hpp.
#include "ConstraintIconOverlay.hpp"
#include "Constraint.hpp"        // krs::constraint -- graph + anchor world frames
#include "Scene.hpp"

#include <QToolButton>
#include <QTimer>

using krs::constraint::CType;

namespace {
const char* glyphOf(CType t) {
    switch (t) {
        case CType::Coincident:    return "⌖";   // ⌖
        case CType::Concentric:    return "◎";   // ◎
        case CType::Parallel:      return "∥";   // ∥
        case CType::Perpendicular: return "⊥";   // ⊥
        case CType::Tangent:       return "⌒";   // ⌒
        case CType::Flush:         return "▭";   // ▭
        case CType::Distance:      return "↔";   // ↔
        case CType::Angle:         return "∠";   // ∠
        case CType::Rigid:         return "⚓";   // ⚓
        case CType::Revolute:      return "⟳";   // ⟳
        case CType::Slider:        return "⇄";   // ⇄
        case CType::Cylindrical:   return "⥁";   // ⥁
        case CType::Ball:          return "●";   // ●
        case CType::PinSlot:       return "⊖";   // ⊖
    }
    return "?";
}
} // namespace

ConstraintIconOverlay::ConstraintIconOverlay(Scene* scene, QWidget* viewport, Projector projector)
    : QObject(viewport), m_scene(scene), m_viewport(viewport), m_project(std::move(projector))
{
    m_timer = new QTimer(this);
    m_timer->setInterval(100);   // 10 Hz screen-space re-pin
    connect(m_timer, &QTimer::timeout, this, [this] { tick(); });
    m_timer->start();
}

void ConstraintIconOverlay::tick()
{
    if (!m_scene || !m_viewport || !m_viewport->isVisible()) return;
    auto& reg = m_scene->getRegistry();
    auto& g = krs::constraint::constraintGraph(reg);

    int used = 0;
    if (g.showIcons) {
        for (const auto& c : g.constraints) {
            if (c.suppressed) continue;
            const krs::constraint::AnchorWorldFrame wa = krs::constraint::anchorWorldFrame(reg, c.a);
            const krs::constraint::AnchorWorldFrame wb = krs::constraint::anchorWorldFrame(reg, c.b);
            if (!wa.valid && !wb.valid) continue;
            const glm::vec3 mid = wa.valid && wb.valid ? 0.5f * (wa.pos + wb.pos)
                                                       : (wa.valid ? wa.pos : wb.pos);
            QPoint p;
            if (!m_project(mid, p)) continue;
            if (p.x() < -20 || p.y() < -20 || p.x() > m_viewport->width() + 20
                || p.y() > m_viewport->height() + 20) continue;

            if (used >= m_pool.size()) {
                auto* b = new QToolButton(m_viewport);
                b->setFixedSize(22, 22);
                b->setCursor(Qt::PointingHandCursor);
                b->setStyleSheet(QStringLiteral(
                    "QToolButton { background-color: rgba(53,59,70,220); color:#e8e8e8;"
                    "  border:1px solid #5a6474; border-radius:11px; font-size:12px; }"
                    "QToolButton:hover { background-color:#0078d7; border-color:#0078d7; }"));
                m_pool.push_back(b);
            }
            QToolButton* b = m_pool[used++];
            b->setText(QString::fromUtf8(glyphOf(c.type)));
            b->setToolTip(QString::fromStdString(krs::constraint::describe(c)));
            b->move(p.x() - 11, p.y() - 11);
            const std::uint64_t id = c.id;
            disconnect(b, &QToolButton::clicked, nullptr, nullptr);
            connect(b, &QToolButton::clicked, this, [this, id] {
                if (!m_scene) return;
                auto& fr = m_scene->getRegistry().ctx().emplace<krs::ui::ConstraintFocusRequest>();
                fr.id = id;
            });
            if (!b->isVisible()) b->show();
            b->raise();
        }
    }
    for (int i = used; i < m_pool.size(); ++i)
        if (m_pool[i]->isVisible()) m_pool[i]->hide();
}
