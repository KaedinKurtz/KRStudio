#include "FluidPropertiesWidget.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QColorDialog>
#include <QScrollArea>

#include <algorithm>

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

    // ---- Solver backend ----
    layout->addWidget(header(QStringLiteral("Solver"), content));
    {
        auto* box = new QGroupBox(content);
        auto* g = new QGridLayout(box);
        g->setContentsMargins(6, 6, 6, 6);
        g->setVerticalSpacing(3);

        g->addWidget(new QLabel(QStringLiteral("Backend"), box), 0, 0);
        m_backend = new QComboBox(box);
        m_backend->addItems({ QStringLiteral("Auto (best for hardware)"),
                              QStringLiteral("PBF — GPU compute (interactive)"),
                              QStringLiteral("DFSPH — CPU reference (real units)") });
        m_backend->setToolTip(QStringLiteral(
            "PBF: position-based fluids on GL compute — interactive on any GPU.\n"
            "DFSPH: SPlisHSPlasH divergence-free SPH — engineering fidelity with\n"
            "real SI units (Pa·s viscosity, N/m surface tension); CPU-bound, slower."));
        g->addWidget(m_backend, 0, 1);
        layout->addWidget(box);
    }

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
        m_density->setDecimals(1);
        g->addWidget(m_density, 0, 1);

        g->addWidget(new QLabel(QStringLiteral("Viscosity (XSPH)"), box), 1, 0);
        m_viscosity = new QDoubleSpinBox(box);
        m_viscosity->setRange(0.0, 1.0);
        m_viscosity->setSingleStep(0.01);
        m_viscosity->setDecimals(3);
        m_viscosity->setToolTip(QStringLiteral(
            "PBF backend only — artistic damping blend (0 = inviscid, 0.5 = syrup).\n"
            "For real viscosity use the DFSPH backend and the Pa·s field below."));
        g->addWidget(m_viscosity, 1, 1);

        g->addWidget(new QLabel(QStringLiteral("Dynamic viscosity μ (Pa·s)"), box), 2, 0);
        m_viscosityPaS = new QDoubleSpinBox(box);
        m_viscosityPaS->setRange(0.00001, 100.0);
        m_viscosityPaS->setDecimals(5);
        m_viscosityPaS->setSingleStep(0.001);
        m_viscosityPaS->setToolTip(QStringLiteral(
            "DFSPH backend (Weiler et al. 2018 implicit viscosity).\n"
            "Water 0.001, olive oil 0.07, honey ~5, mercury 0.0015"));
        g->addWidget(m_viscosityPaS, 2, 1);

        g->addWidget(new QLabel(QStringLiteral("Surface tension σ (N/m)"), box), 3, 0);
        m_surfaceTension = new QDoubleSpinBox(box);
        m_surfaceTension->setRange(0.0, 1.0);
        m_surfaceTension->setDecimals(4);
        m_surfaceTension->setSingleStep(0.005);
        m_surfaceTension->setToolTip(QStringLiteral(
            "DFSPH backend (Akinci et al. 2013). Water-air 0.0728, mercury 0.485"));
        g->addWidget(m_surfaceTension, 3, 1);

        g->addWidget(new QLabel(QStringLiteral("Material preset"), box), 4, 0);
        m_materialPreset = new QComboBox(box);
        m_materialPreset->addItems({ QStringLiteral("Custom"), QStringLiteral("Water (20 °C)"),
                                     QStringLiteral("Olive oil"), QStringLiteral("Honey"),
                                     QStringLiteral("Mercury") });
        g->addWidget(m_materialPreset, 4, 1);

        g->addWidget(new QLabel(QStringLiteral("Incompressibility iterations"), box), 5, 0);
        m_iterations = new QSpinBox(box);
        m_iterations->setRange(1, 10);
        m_iterations->setToolTip(QStringLiteral("Higher = stiffer (less compressible) water, more GPU cost"));
        g->addWidget(m_iterations, 5, 1);

        g->addWidget(new QLabel(QStringLiteral("Particle radius (m)"), box), 6, 0);
        m_radius = new QDoubleSpinBox(box);
        m_radius->setRange(0.01, 0.05);
        m_radius->setSingleStep(0.005);
        m_radius->setDecimals(3);
        g->addWidget(m_radius, 6, 1);

        g->addWidget(new QLabel(QStringLiteral("Gravity Y (m/s²)"), box), 7, 0);
        m_gravityY = new QDoubleSpinBox(box);
        m_gravityY->setRange(-50.0, 50.0);
        m_gravityY->setSingleStep(0.1);
        m_gravityY->setDecimals(2);
        g->addWidget(m_gravityY, 7, 1);

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

    connect(m_backend, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updating) return;
        FluidSystem* fluid = m_renderer ? m_renderer->getFluidSystem() : nullptr;
        if (!fluid) return;
        const FluidBackend tiers[] = { FluidBackend::Auto, FluidBackend::PbfGpu,
                                       FluidBackend::DfsphCpu };
        fluid->setRequestedBackend(tiers[std::clamp(idx, 0, 2)]);
        if (fluid->activeBackend() != fluid->requestedBackend()
            && fluid->requestedBackend() != FluidBackend::Auto)
            qWarning() << "[Fluid] requested backend unavailable — using"
                       << fluidBackendName(fluid->activeBackend());
    });
    connect(m_materialPreset, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updating || idx == 0) return;
        // rho [kg/m3], mu [Pa.s], sigma [N/m] at ~20-25 C
        struct Mat { double rho, mu, sigma; };
        static const Mat mats[] = { { 998.2, 1.002e-3, 0.0728 },   // water
                                    { 915.0, 0.07, 0.032 },        // olive oil
                                    { 1420.0, 5.0, 0.07 },         // honey (sigma ill-defined)
                                    { 13546.0, 1.53e-3, 0.485 } }; // mercury
        const Mat& m = mats[std::clamp(idx - 1, 0, 3)];
        m_density->setValue(m.rho);
        m_viscosityPaS->setValue(m.mu);
        m_surfaceTension->setValue(m.sigma);
        applyPhysics();
    });
    for (auto* s : { m_density, m_viscosity, m_viscosityPaS, m_surfaceTension, m_radius, m_gravityY })
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
    m_viscosityPaS->setValue(p.dynamicViscosityPaS);
    m_surfaceTension->setValue(p.surfaceTensionNpm);
    m_iterations->setValue(p.solverIterations);
    m_radius->setValue(p.particleRadius);
    m_gravityY->setValue(p.gravity.y);
    switch (fluid->requestedBackend()) {
    case FluidBackend::PbfGpu:   m_backend->setCurrentIndex(1); break;
    case FluidBackend::DfsphCpu: m_backend->setCurrentIndex(2); break;
    default:                     m_backend->setCurrentIndex(0); break;
    }
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
    p.dynamicViscosityPaS = float(m_viscosityPaS->value());
    p.surfaceTensionNpm = float(m_surfaceTension->value());
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
