#include "LightingPropertiesWidget.hpp"
#include "RenderingSystem.hpp"

#include <QFormLayout>
#include <QDoubleSpinBox>

LightingPropertiesWidget::LightingPropertiesWidget(RenderingSystem* renderer, QWidget* parent)
    : QWidget(parent), m_renderer(renderer)
{
    auto* form = new QFormLayout(this);

    // Ambient / image-based lighting fill from the HDR environment.
    m_ibl = new QDoubleSpinBox(this);
    m_ibl->setRange(0.0, 2.0);
    m_ibl->setSingleStep(0.05);
    m_ibl->setDecimals(3);
    m_ibl->setToolTip(QStringLiteral("Ambient (image-based) lighting fill from the HDR environment."));
    form->addRow(QStringLiteral("Ambient (IBL)"), m_ibl);

    // Directional sun (key light) intensity.
    m_sun = new QDoubleSpinBox(this);
    m_sun->setRange(0.0, 10.0);
    m_sun->setSingleStep(0.1);
    m_sun->setDecimals(2);
    m_sun->setToolTip(QStringLiteral("Directional sun (key light) intensity."));
    form->addRow(QStringLiteral("Sun"), m_sun);

    syncFromSystem();

    connect(m_ibl, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
    connect(m_sun, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
}

void LightingPropertiesWidget::syncFromSystem()
{
    if (!m_renderer) return;
    m_updating = true;
    m_ibl->setValue(m_renderer->getIblIntensity());
    m_sun->setValue(m_renderer->getSunIntensity());
    m_updating = false;
}

void LightingPropertiesWidget::apply()
{
    if (m_updating || !m_renderer) return;
    m_renderer->setIblIntensity(static_cast<float>(m_ibl->value()));
    m_renderer->setSunIntensity(static_cast<float>(m_sun->value()));
}
