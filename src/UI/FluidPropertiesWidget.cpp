#include "FluidPropertiesWidget.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QColorDialog>
#include <QScrollArea>

namespace {
QWidget* header(const QString& text, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 6, 0, 0);
    l->setSpacing(2);
    auto* label = new QLabel(text, w);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-weight: bold;");
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    l->addWidget(label);
    l->addWidget(line);
    return w;
}

QSlider* percentSlider(QWidget* parent)
{
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(0, 100);
    return s;
}
} // namespace

FluidPropertiesWidget::FluidPropertiesWidget(RenderingSystem* renderer, QWidget* parent)
    : QWidget(parent), m_renderer(renderer)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    // ---- Physical parameters ----
    layout->addWidget(header(QStringLiteral("Fluid Physics"), content));
    {
        auto* box = new QGroupBox(content);
        auto* g = new QGridLayout(box);
        g->setContentsMargins(6, 6, 6, 6);
        g->setVerticalSpacing(3);

        g->addWidget(new QLabel(QStringLiteral("Rest density (kg/m³)"), box), 0, 0);
        m_density = new QDoubleSpinBox(box);
        m_density->setRange(50.0, 20000.0);
        m_density->setSingleStep(50.0);
        m_density->setDecimals(0);
        g->addWidget(m_density, 0, 1);

        g->addWidget(new QLabel(QStringLiteral("Viscosity (XSPH)"), box), 1, 0);
        m_viscosity = new QDoubleSpinBox(box);
        m_viscosity->setRange(0.0, 1.0);
        m_viscosity->setSingleStep(0.01);
        m_viscosity->setDecimals(3);
        m_viscosity->setToolTip(QStringLiteral("0 = inviscid, 0.05 = water-like, 0.5 = syrup"));
        g->addWidget(m_viscosity, 1, 1);

        g->addWidget(new QLabel(QStringLiteral("Incompressibility iterations"), box), 2, 0);
        m_iterations = new QSpinBox(box);
        m_iterations->setRange(1, 10);
        m_iterations->setToolTip(QStringLiteral("Higher = stiffer (less compressible) water, more GPU cost"));
        g->addWidget(m_iterations, 2, 1);

        g->addWidget(new QLabel(QStringLiteral("Particle radius (m)"), box), 3, 0);
        m_radius = new QDoubleSpinBox(box);
        m_radius->setRange(0.01, 0.05);
        m_radius->setSingleStep(0.005);
        m_radius->setDecimals(3);
        g->addWidget(m_radius, 3, 1);

        g->addWidget(new QLabel(QStringLiteral("Gravity Y (m/s²)"), box), 4, 0);
        m_gravityY = new QDoubleSpinBox(box);
        m_gravityY->setRange(-50.0, 50.0);
        m_gravityY->setSingleStep(0.1);
        m_gravityY->setDecimals(2);
        g->addWidget(m_gravityY, 4, 1);

        layout->addWidget(box);
    }

    // ---- Appearance ----
    layout->addWidget(header(QStringLiteral("Fluid Appearance"), content));
    {
        auto* box = new QGroupBox(content);
        auto* g = new QGridLayout(box);
        g->setContentsMargins(6, 6, 6, 6);
        g->setVerticalSpacing(3);

        g->addWidget(new QLabel(QStringLiteral("Colour"), box), 0, 0);
        m_colorButton = new QPushButton(box);
        m_colorButton->setFixedHeight(22);
        g->addWidget(m_colorButton, 0, 1);

        g->addWidget(new QLabel(QStringLiteral("Turbidity"), box), 1, 0);
        m_turbidity = percentSlider(box);
        g->addWidget(m_turbidity, 1, 1);

        g->addWidget(new QLabel(QStringLiteral("Emissivity"), box), 2, 0);
        m_emissivity = percentSlider(box);
        g->addWidget(m_emissivity, 2, 1);

        g->addWidget(new QLabel(QStringLiteral("Foam"), box), 3, 0);
        m_foam = percentSlider(box);
        g->addWidget(m_foam, 3, 1);

        g->addWidget(new QLabel(QStringLiteral("Sprite size ×"), box), 4, 0);
        m_sizeScale = new QDoubleSpinBox(box);
        m_sizeScale->setRange(0.2, 4.0);
        m_sizeScale->setSingleStep(0.1);
        m_sizeScale->setDecimals(2);
        g->addWidget(m_sizeScale, 4, 1);

        layout->addWidget(box);
    }

    scroll->setWidget(content);
    outer->addWidget(scroll);

    syncFromSystem();

    for (auto* s : { m_density, m_viscosity, m_radius, m_gravityY })
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]() { applyPhysics(); });
    connect(m_iterations, &QSpinBox::valueChanged, this, [this]() { applyPhysics(); });
    connect(m_sizeScale, &QDoubleSpinBox::valueChanged, this, [this]() { applyAppearance(); });
    for (auto* s : { m_turbidity, m_emissivity, m_foam })
        connect(s, &QSlider::valueChanged, this, [this]() { applyAppearance(); });
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        FluidSystem* fluid = m_renderer ? m_renderer->getFluidSystem() : nullptr;
        if (!fluid) return;
        const auto& c = fluid->appearance().color;
        QColor initial = QColor::fromRgbF(c.r, c.g, c.b);
        QColor picked = QColorDialog::getColor(initial, this, QStringLiteral("Fluid colour"));
        if (!picked.isValid()) return;
        fluid->appearance().color = { float(picked.redF()), float(picked.greenF()), float(picked.blueF()) };
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(picked.name()));
    });
}

void FluidPropertiesWidget::syncFromSystem()
{
    FluidSystem* fluid = m_renderer ? m_renderer->getFluidSystem() : nullptr;
    if (!fluid) { setEnabled(false); return; }
    m_updating = true;
    const auto& p = fluid->params();
    m_density->setValue(p.restDensity);
    m_viscosity->setValue(p.viscosity);
    m_iterations->setValue(p.solverIterations);
    m_radius->setValue(p.particleRadius);
    m_gravityY->setValue(p.gravity.y);
    const auto& a = fluid->appearance();
    m_turbidity->setValue(int(a.turbidity * 100));
    m_emissivity->setValue(int(a.emissivity * 100));
    m_foam->setValue(int(a.foaminess * 100));
    m_sizeScale->setValue(a.sizeScale);
    const QColor c = QColor::fromRgbF(a.color.r, a.color.g, a.color.b);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name()));
    m_updating = false;
}

void FluidPropertiesWidget::applyPhysics()
{
    if (m_updating) return;
    FluidSystem* fluid = m_renderer ? m_renderer->getFluidSystem() : nullptr;
    if (!fluid) return;
    auto& p = fluid->params();
    p.restDensity = float(m_density->value());
    p.viscosity = float(m_viscosity->value());
    p.solverIterations = m_iterations->value();
    p.particleRadius = float(m_radius->value());
    p.gravity.y = float(m_gravityY->value());
}

void FluidPropertiesWidget::applyAppearance()
{
    if (m_updating) return;
    FluidSystem* fluid = m_renderer ? m_renderer->getFluidSystem() : nullptr;
    if (!fluid) return;
    auto& a = fluid->appearance();
    a.turbidity = m_turbidity->value() / 100.0f;
    a.emissivity = m_emissivity->value() / 100.0f;
    a.foaminess = m_foam->value() / 100.0f;
    a.sizeScale = float(m_sizeScale->value());
}
