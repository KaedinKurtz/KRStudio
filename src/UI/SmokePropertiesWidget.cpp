#include "SmokePropertiesWidget.hpp"
#include "RenderingSystem.hpp"
#include "SmokeSystem.hpp"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFrame>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QColorDialog>
#include <QTimer>

SmokePropertiesWidget::SmokePropertiesWidget(RenderingSystem* renderer, QWidget* parent)
    : QWidget(parent), m_renderer(renderer)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* title = new QLabel(QStringLiteral("Gas (Smoke / Fire)"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-weight: bold;");
    layout->addWidget(title);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    auto* box = new QGroupBox(this);
    auto* g = new QGridLayout(box);
    g->setContentsMargins(6, 6, 6, 6);
    g->setVerticalSpacing(3);
    int row = 0;
    auto addSpin = [&](const QString& label, double lo, double hi, double step, int dec,
                       const QString& tip) {
        g->addWidget(new QLabel(label, box), row, 0);
        auto* s = new QDoubleSpinBox(box);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setKeyboardTracking(false);
        s->setToolTip(tip);
        g->addWidget(s, row++, 1);
        return s;
    };

    m_buoyancy = addSpin(QStringLiteral("Buoyancy"), 0.0, 12.0, 0.2, 2,
                         QStringLiteral("Upward force from heat — taller, faster plumes"));
    m_cooling = addSpin(QStringLiteral("Cooling /s"), 0.0, 5.0, 0.1, 2,
                        QStringLiteral("How fast the gas loses heat (and rises less)"));
    m_vorticity = addSpin(QStringLiteral("Turbulence"), 0.0, 30.0, 0.5, 1,
                          QStringLiteral("Vorticity confinement: curls and small-scale detail"));
    m_dissipation = addSpin(QStringLiteral("Dissipation /s"), 0.0, 2.0, 0.05, 2,
                            QStringLiteral("How fast smoke density fades away"));
    m_densityScale = addSpin(QStringLiteral("Optical density"), 1.0, 120.0, 2.0, 1,
                             QStringLiteral("Render thickness — higher = more opaque smoke"));
    m_burnRate = addSpin(QStringLiteral("Burn rate /s"), 0.0, 8.0, 0.2, 2,
                         QStringLiteral("Fire only: fuel -> heat + soot conversion speed"));

    g->addWidget(new QLabel(QStringLiteral("Pressure iters"), box), row, 0);
    m_pressureIters = new QSpinBox(box);
    m_pressureIters->setRange(8, 60);
    m_pressureIters->setToolTip(QStringLiteral("Incompressibility solve quality (more = stiffer, costlier)"));
    g->addWidget(m_pressureIters, row++, 1);

    g->addWidget(new QLabel(QStringLiteral("Smoke colour"), box), row, 0);
    m_colorButton = new QPushButton(box);
    m_colorButton->setFixedHeight(22);
    g->addWidget(m_colorButton, row++, 1);

    layout->addWidget(box);
    layout->addStretch();

    syncFromSystem();

    for (auto* s : { m_buoyancy, m_cooling, m_vorticity, m_dissipation, m_densityScale, m_burnRate })
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]() { apply(); });
    connect(m_pressureIters, &QSpinBox::valueChanged, this, [this]() { apply(); });
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        SmokeSystem* sm = m_renderer ? m_renderer->getSmokeSystem() : nullptr;
        if (!sm) return;
        const auto& c = sm->params().smokeColor;
        QColor picked = QColorDialog::getColor(QColor::fromRgbF(c.r, c.g, c.b), this,
                                               QStringLiteral("Smoke colour"));
        if (!picked.isValid()) return;
        sm->params().smokeColor = { float(picked.redF()), float(picked.greenF()), float(picked.blueF()) };
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(picked.name()));
    });
}

void SmokePropertiesWidget::syncFromSystem()
{
    SmokeSystem* sm = m_renderer ? m_renderer->getSmokeSystem() : nullptr;
    if (!sm) {
        // SmokeSystem is created on the engine context after this panel —
        // retry until it exists (mirrors FluidPropertiesWidget).
        setEnabled(false);
        QTimer::singleShot(500, this, &SmokePropertiesWidget::syncFromSystem);
        return;
    }
    setEnabled(true);
    m_updating = true;
    const auto& p = sm->params();
    m_buoyancy->setValue(p.buoyancy);
    m_cooling->setValue(p.cooling);
    m_vorticity->setValue(p.vorticity);
    m_dissipation->setValue(p.densityDissipation);
    m_densityScale->setValue(p.densityScale);
    m_burnRate->setValue(p.burnRate);
    m_pressureIters->setValue(p.pressureIterations);
    const QColor c = QColor::fromRgbF(p.smokeColor.r, p.smokeColor.g, p.smokeColor.b);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name()));
    m_updating = false;
}

void SmokePropertiesWidget::apply()
{
    if (m_updating) return;
    SmokeSystem* sm = m_renderer ? m_renderer->getSmokeSystem() : nullptr;
    if (!sm) return;
    auto& p = sm->params();
    p.buoyancy = float(m_buoyancy->value());
    p.cooling = float(m_cooling->value());
    p.vorticity = float(m_vorticity->value());
    p.densityDissipation = float(m_dissipation->value());
    p.densityScale = float(m_densityScale->value());
    p.burnRate = float(m_burnRate->value());
    p.pressureIterations = m_pressureIters->value();
}
