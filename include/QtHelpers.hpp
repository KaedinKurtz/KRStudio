// ────────────────────────────────────────────────────────────────
//  WidgetHelpers.hpp      (include *after* <QtWidgets>)
// ────────────────────────────────────────────────────────────────
#pragma once
#include <QtWidgets>
#include <limits>
#include <utility>     // std::pair

namespace WH   // Widget Helpers
{
    // ─── Spin-Box helpers ──────────────────────────────────────────
    inline void unbounded(QDoubleSpinBox* sb,
        int decimals = 3,
        double step = 0.5)          // ±DBL_MAX
    {
        sb->setRange(-std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max());
        sb->setDecimals(decimals); sb->setSingleStep(step);
    }

    inline void bounded(QDoubleSpinBox* sb,
        std::pair<double, double> range,
        int decimals = 3,
        double step = 0.1)
    {
        sb->setRange(range.first, range.second);
        sb->setDecimals(decimals); sb->setSingleStep(step);
    }

    inline void boundedInt(QSpinBox* sb,
        std::pair<int, int> range,
        int step = 1)
    {
        sb->setRange(range.first, range.second);
        sb->setSingleStep(step);
    }

    inline void range180deg(QDoubleSpinBox* sb,
        int decimals = 1)
    {
        bounded(sb, { -180.0, 180.0 }, decimals, 5.0);
        sb->setSuffix(QStringLiteral("°"));
    }

    inline void positiveLength(QDoubleSpinBox* sb,
        double max = 1e6,
        int decimals = 3,
        double step = 0.01)
    {
        bounded(sb, { 0.0, max }, decimals, step);
        sb->setPrefix(QStringLiteral("⟂ "));
    }

    inline void percent(QDoubleSpinBox* sb,
        int decimals = 1)
    {
        bounded(sb, { 0.0, 100.0 }, decimals, 0.1);
        sb->setSuffix(QStringLiteral("%"));
    }

    // ─── Slider helpers ────────────────────────────────────────────
    inline void slider0_1(QSlider* sl)
    {
        sl->setOrientation(Qt::Horizontal);
        sl->setRange(0, 1000);          // maps   0-1000 → 0-1
        sl->setSingleStep(10);
        sl->setTickInterval(100);
        sl->setTickPosition(QSlider::TicksBelow);
    }

    inline void sliderInt(QSlider* sl,
        std::pair<int, int> range,
        int tick = 1,
        Qt::Orientation ori = Qt::Horizontal)
    {
        sl->setOrientation(ori);
        sl->setRange(range.first, range.second);
        sl->setSingleStep(tick);
        sl->setTickPosition(QSlider::TicksBelow);
    }

    // ─── Button helpers ────────────────────────────────────────────
    inline QButtonGroup* makeExclusive(std::initializer_list<QAbstractButton*> buttons,
        QWidget* parent = nullptr)
    {
        auto* grp = new QButtonGroup(parent);
        grp->setExclusive(true);
        int id = 0;
        for (auto* b : buttons)
            grp->addButton(b, id++);
        return grp;
    }

    inline void makeToggleDefault(QAbstractButton* btn,
        bool checked = true)
    {
        btn->setCheckable(true);
        btn->setChecked(checked);
    }

    // ─── Layout helpers ────────────────────────────────────────────
    template<class Layout>
    inline Layout* tightMargins(Layout* lay,
        int m = 0, int s = 0)
    {
        lay->setContentsMargins(m, m, m, m);
        lay->setSpacing(s);
        return lay;
    }

    // ─── Tooltip / palette helpers ─────────────────────────────────
    inline void defaultTooltip(QWidget* w, const QString& tip)
    {
        w->setToolTip(tip); w->setStatusTip(tip);
    }

    inline void dangerPalette(QWidget* w)
    {
        w->setPalette(QToolTip::palette()); w->setStyleSheet("color:#e74c3c;");
    }

    // ─── Connect helper: slider ↔ spin-box (float 0-1) ─────────────
    inline void bindSliderSpin(QSlider* sl, QDoubleSpinBox* sb)
    {
        slider0_1(sl); range180deg(sb);          // customise each control

        QObject::connect(sl, &QSlider::valueChanged,
            sb, [sb](int v) { sb->setValue(v * 0.001); });

        QObject::connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged),
            sl, [sl](double v) { QSignalBlocker b(sl);
        sl->setValue(int(v * 1000)); });
    }
} // namespace WH