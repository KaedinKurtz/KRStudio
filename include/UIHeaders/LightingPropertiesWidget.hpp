#pragma once

#include <QWidget>

class RenderingSystem;
class QDoubleSpinBox;

/**
 * @brief Live controls for the object lighting: IBL/ambient fill and the
 * directional sun (key light) intensity. Writes straight into RenderingSystem;
 * the continuous engine frame reads the values each frame, so changes are live.
 */
class LightingPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LightingPropertiesWidget(RenderingSystem* renderer, QWidget* parent = nullptr);

private:
    void syncFromSystem();   // populate controls from the renderer (no feedback)
    void apply();            // push control values into the renderer

    RenderingSystem* m_renderer = nullptr;
    bool m_updating = false;  // guard so syncFromSystem() doesn't trigger apply()

    QDoubleSpinBox* m_ibl = nullptr;
    QDoubleSpinBox* m_sun = nullptr;
};
