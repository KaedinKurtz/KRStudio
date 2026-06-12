#include "DiagnosticsPanel.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "HardwareCaps.hpp"
#include "Robot.hpp" // Include Robot to use its data

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <iomanip>
#include <sstream>

namespace {
QLabel* makeHeader(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet("font-weight: bold; padding-top: 4px;");
    return label;
}
} // namespace

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignTop);

    // --- Hardware / solver tiers ---
    m_layout->addWidget(makeHeader("Hardware", this));
    m_hardwareLabel = new QLabel("GPU compute: probing...", this);
    m_backendLabel = new QLabel("Fluid solver: --", this);
    m_layout->addWidget(m_hardwareLabel);
    m_layout->addWidget(m_backendLabel);

    // --- Frame stats ---
    m_layout->addWidget(makeHeader("Frame", this));
    m_fpsLabel = new QLabel("FPS: --", this);
    m_cpuFrameLabel = new QLabel("CPU frame: -- ms", this);
    m_layout->addWidget(m_fpsLabel);
    m_layout->addWidget(m_cpuFrameLabel);

    // --- GPU stage timings ---
    m_layout->addWidget(makeHeader("GPU passes (ms)", this));
    m_geometryLabel = new QLabel("Geometry: --", this);
    m_lightingLabel = new QLabel("Lighting: --", this);
    m_postLabel = new QLabel("Post-processing: --", this);
    m_overlayLabel = new QLabel("Overlay: --", this);
    m_fluidSimLabel = new QLabel("Fluid sim: --", this);
    m_gpuTotalLabel = new QLabel("GPU total: --", this);
    m_gpuTotalLabel->setStyleSheet("font-weight: bold;");
    m_layout->addWidget(m_geometryLabel);
    m_layout->addWidget(m_lightingLabel);
    m_layout->addWidget(m_postLabel);
    m_layout->addWidget(m_overlayLabel);
    m_layout->addWidget(m_fluidSimLabel);
    m_layout->addWidget(m_gpuTotalLabel);

    // --- Robot joints (populated when a robot is loaded) ---
    m_layout->addWidget(makeHeader("Robot joints", this));
    m_jointLayout = new QVBoxLayout();
    m_jointLayout->setSpacing(2);
    m_layout->addLayout(m_jointLayout);
}

void DiagnosticsPanel::setRenderingSystem(RenderingSystem* renderingSystem)
{
    m_renderingSystem = renderingSystem;
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        connect(m_refreshTimer, &QTimer::timeout, this, &DiagnosticsPanel::refreshTimings);
        m_refreshTimer->start(250);
    }
}

void DiagnosticsPanel::refreshTimings()
{
    if (!m_renderingSystem || !isVisible()) return;

    const auto& caps = krs::hardwareCaps();
    m_hardwareLabel->setText(caps.cudaPhysics
        ? QString("CUDA: %1 (GPU physics tiers on)").arg(QString::fromStdString(caps.cudaDeviceName))
        : QStringLiteral("CUDA: none (CPU/GL-compute tiers)"));
    if (FluidSystem* fluid = m_renderingSystem->getFluidSystem())
        m_backendLabel->setText(QString("Fluid solver: %1")
                                    .arg(fluidBackendName(fluid->activeBackend())));

    const GpuTimings t = m_renderingSystem->getGpuTimings();
    m_fpsLabel->setText(QString("FPS: %1").arg(m_renderingSystem->getFPS(), 0, 'f', 1));
    m_cpuFrameLabel->setText(QString("CPU frame: %1 ms").arg(m_renderingSystem->getFrameTime(), 0, 'f', 2));
    m_geometryLabel->setText(QString("Geometry: %1").arg(t.geometryMs, 0, 'f', 2));
    m_lightingLabel->setText(QString("Lighting: %1").arg(t.lightingMs, 0, 'f', 2));
    m_postLabel->setText(QString("Post-processing: %1").arg(t.postMs, 0, 'f', 2));
    m_overlayLabel->setText(QString("Overlay: %1").arg(t.overlayMs, 0, 'f', 2));
    m_fluidSimLabel->setText(QString("Fluid sim: %1").arg(t.fluidSimMs, 0, 'f', 2));
    m_gpuTotalLabel->setText(QString("GPU total: %1").arg(t.totalMs(), 0, 'f', 2));
}

void DiagnosticsPanel::updateData(const Robot& robot)
{
    auto jointStates = robot.getJointStates();

    // On first run, create labels for each joint
    if (m_jointLabels.empty() && !jointStates.empty()) {
        for (const auto& [name, angle] : jointStates) {
            m_jointLabels[name] = new QLabel(this);
            m_jointLayout->addWidget(m_jointLabels[name]);
        }
    }

    // Update the text of each label
    for (const auto& [name, angle] : jointStates) {
        if (m_jointLabels.count(name)) {
            std::stringstream stream;
            stream << std::fixed << std::setprecision(2) << glm::degrees(angle);
            std::string label_text = name + ": " + stream.str() + " deg";
            m_jointLabels[name]->setText(QString::fromStdString(label_text));
        }
    }
}
