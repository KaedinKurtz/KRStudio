#pragma once

#include <QWidget>
#include <map>
#include <string>

// Forward declarations
class QLabel;
class QVBoxLayout;
class QTimer;
class Robot;
class RenderingSystem;

/// Live engine diagnostics: per-stage GPU frame timings (GL_TIME_ELAPSED
/// queries resolved by the RenderingSystem) plus CPU frame stats, and the
/// original robot joint readout when a robot is loaded.
class DiagnosticsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    /// Hook up the rendering system; starts the polling timer.
    void setRenderingSystem(RenderingSystem* renderingSystem);

    void updateData(const Robot& robot);

private slots:
    void refreshTimings();

private:
    QVBoxLayout* m_layout = nullptr;
    QTimer* m_refreshTimer = nullptr;
    RenderingSystem* m_renderingSystem = nullptr;

    // Hardware / solver tier labels
    QLabel* m_hardwareLabel = nullptr;
    QLabel* m_backendLabel = nullptr;

    // GPU timing labels
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_cpuFrameLabel = nullptr;
    QLabel* m_geometryLabel = nullptr;
    QLabel* m_lightingLabel = nullptr;
    QLabel* m_postLabel = nullptr;
    QLabel* m_overlayLabel = nullptr;
    QLabel* m_gpuTotalLabel = nullptr;

    // Robot joint readout
    QVBoxLayout* m_jointLayout = nullptr;
    std::map<std::string, QLabel*> m_jointLabels;
};
