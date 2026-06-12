#pragma once

#include <QWidget>

class RenderingSystem;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QPushButton;
class QSlider;

/**
 * @brief Global fluid controls: physical parameters (density, viscosity,
 * compressibility, gravity) and appearance (colour, turbidity, emissivity,
 * foam, sprite size). Writes live into the FluidSystem.
 */
class FluidPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FluidPropertiesWidget(RenderingSystem* renderer, QWidget* parent = nullptr);

private:
    void syncFromSystem();
    void applyPhysics();
    void applyAppearance();

    RenderingSystem* m_renderer = nullptr;
    bool m_updating = false;

    // solver
    QComboBox* m_backend = nullptr;
    QComboBox* m_materialPreset = nullptr;

    // physics
    QDoubleSpinBox* m_density = nullptr;
    QDoubleSpinBox* m_viscosity = nullptr;
    QDoubleSpinBox* m_viscosityPaS = nullptr;
    QDoubleSpinBox* m_surfaceTension = nullptr;
    QSpinBox* m_iterations = nullptr;
    QDoubleSpinBox* m_radius = nullptr;
    QDoubleSpinBox* m_gravityY = nullptr;

    // appearance
    QComboBox* m_renderMode = nullptr;
    QPushButton* m_colorButton = nullptr;
    QSlider* m_turbidity = nullptr;
    QSlider* m_emissivity = nullptr;
    QSlider* m_foam = nullptr;
    QDoubleSpinBox* m_sizeScale = nullptr;
    QDoubleSpinBox* m_iorSpin = nullptr;
    QDoubleSpinBox* m_absorption = nullptr;
    QDoubleSpinBox* m_refraction = nullptr;

    // sim cache
    QCheckBox* m_recordCheck = nullptr;
    QSlider* m_scrubSlider = nullptr;
    QLabel* m_cacheInfo = nullptr;
    QPushButton* m_clearCache = nullptr;
};
