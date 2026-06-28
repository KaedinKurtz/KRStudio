#include "LightingPropertiesWidget.hpp"
#include "RenderingSystem.hpp"

#include <QFormLayout>
#include <QDoubleSpinBox>

LightingPropertiesWidget::LightingPropertiesWidget(RenderingSystem* renderer, QWidget* parent)
    : QWidget(parent), m_renderer(renderer)
{
    auto* form = new QFormLayout(this);

    // Camera exposure (physically-based): EV100. Lower = brighter. This is the master
    // knob that brings the photometric scene (lux/cd/nits) into the display range.
    m_exposure = new QDoubleSpinBox(this);
    m_exposure->setRange(0.0, 20.0);
    m_exposure->setSingleStep(0.25);
    m_exposure->setDecimals(2);
    m_exposure->setToolTip(QStringLiteral("Camera exposure (EV100). Lower = brighter image. ~10 indoor, ~15 daylight."));
    form->addRow(QStringLiteral("Exposure (EV)"), m_exposure);

    // Ambient / image-based lighting fill from the HDR environment, in nits (luminance).
    m_ibl = new QDoubleSpinBox(this);
    m_ibl->setRange(0.0, 5000.0);
    m_ibl->setSingleStep(5.0);
    m_ibl->setDecimals(1);
    m_ibl->setToolTip(QStringLiteral("Ambient (image-based) fill luminance from the HDR environment, in nits."));
    form->addRow(QStringLiteral("Ambient (IBL) [nits]"), m_ibl);

    // Directional sun (key light) illuminance in lux.
    m_sun = new QDoubleSpinBox(this);
    m_sun->setRange(0.0, 150000.0);
    m_sun->setSingleStep(100.0);
    m_sun->setDecimals(0);
    m_sun->setToolTip(QStringLiteral("Directional sun (key light) illuminance, in lux (~1000 office, ~100000 daylight)."));
    form->addRow(QStringLiteral("Sun [lux]"), m_sun);

    syncFromSystem();

    connect(m_exposure, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
    connect(m_ibl, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
    connect(m_sun, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
}

void LightingPropertiesWidget::syncFromSystem()
{
    if (!m_renderer) return;
    m_updating = true;
    m_exposure->setValue(m_renderer->getExposureEV());
    m_ibl->setValue(m_renderer->getIblIntensity());
    m_sun->setValue(m_renderer->getSunIntensity());
    m_updating = false;
}

void LightingPropertiesWidget::apply()
{
    if (m_updating || !m_renderer) return;
    m_renderer->setExposureEV(static_cast<float>(m_exposure->value()));
    m_renderer->setIblIntensity(static_cast<float>(m_ibl->value()));
    m_renderer->setSunIntensity(static_cast<float>(m_sun->value()));
}
