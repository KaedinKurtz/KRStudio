#pragma once

#include <QWidget>

class RenderingSystem;
class QDoubleSpinBox;
class QSpinBox;
class QSlider;
class QPushButton;

/**
 * @brief Global gas (smoke/fire) controls: buoyancy, cooling, vorticity,
 * dissipation, optical density, combustion. Writes live into SmokeSystem.
 */
class SmokePropertiesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SmokePropertiesWidget(RenderingSystem* renderer, QWidget* parent = nullptr);

private:
    void syncFromSystem();
    void apply();

    RenderingSystem* m_renderer = nullptr;
    bool m_updating = false;

    QDoubleSpinBox* m_buoyancy = nullptr;
    QDoubleSpinBox* m_cooling = nullptr;
    QDoubleSpinBox* m_vorticity = nullptr;
    QDoubleSpinBox* m_dissipation = nullptr;
    QDoubleSpinBox* m_densityScale = nullptr;
    QDoubleSpinBox* m_burnRate = nullptr;
    QSpinBox* m_pressureIters = nullptr;
    QPushButton* m_colorButton = nullptr;
};
