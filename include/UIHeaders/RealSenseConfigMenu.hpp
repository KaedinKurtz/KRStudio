#pragma once

#include "RealSenseManager.hpp"
#include <QWidget>
#include <memory>
#include <string>
#include <QVariant> // Add this include for QVariant
#include "IMenu.hpp"

// Forward declare all Qt classes to keep this header clean
class QTimer;
class QLabel;
class QToolButton;
class QComboBox;
class QTableWidget;
class QSlider;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QGroupBox;

// Make the StreamProfile struct usable with QVariant
Q_DECLARE_METATYPE(StreamProfile);

class RealSenseConfigMenu : public QWidget, public IMenu
{
    Q_OBJECT

public:
    explicit RealSenseConfigMenu(QWidget* parent = nullptr);
    ~RealSenseConfigMenu();
    void setupPreview();

    void initializeFresh() override;
    void initializeFromDatabase() override;
    void shutdownAndSave() override;
    QWidget* widget() override { return this; }

public slots:
    void onRefreshDevicesClicked();
    void onDeviceSelectionChanged(int index);
    void onStartStreamingClicked();
    void onStopStreamingClicked();
    void updatePreview();

signals:
    void pointCloudReady(const rs2::points& points, const rs2::video_frame& colorFrame);
    void startStreamingRequested(const std::string& serial,
        const std::vector<StreamProfile>& profiles);
    void stopStreamingRequested();
private:
    // --- Setup Methods ---
    void setupUi();
    void setupConnections();
    void logAllStreamProfiles(const std::string& serialNumber);
    // --- Helper Methods ---
    void populateDeviceList();
    void updateStreamControlsForDevice(const std::string& serialNumber);
    void updateDeviceInfoTable(const std::string& serialNumber);
    void linkSliderAndSpinBox(QSlider* slider, QDoubleSpinBox* spinBox, rs2_option option);
    void populateProfileDropdowns(const std::string& serialNumber);
    // --- Backend Logic ---
    std::unique_ptr<RealSenseManager> m_realSenseManager;
    rs2::colorizer m_colorizer;
    rs2::pointcloud m_pointcloud;
    rs2::align m_align;
    // --- UI State Caches ---
    std::vector<RealSenseDeviceInfo> m_availableDevices;
    // m_supportedProfiles is no longer needed as we query them on demand
    // std::vector<StreamProfile> m_supportedProfiles; 

    // --- Manually Declared UI Widgets ---
    // Top-level controls
    QToolButton* refreshRealSenseDevicesButton;
    QComboBox* RealSenseCameraSelectionComboBox;
    QTableWidget* RSActiveDevicePropertiesList;

    // Streaming controls
    QToolButton* startStreamingRSButton;
    QToolButton* stopStreamingRSButton;

    // Stream Configuration
    QToolButton* enableDepthStream_toggle_tool;
    QToolButton* enableRGBStream_toggle_tool;
    QToolButton* enableInfaredStream_toggle_tool;
    QComboBox* depthProfileCombo;
    QComboBox* colorProfileCombo;
    QComboBox* RSPreviewsourceComboBox;

    // NOTE: An infrared profile combo box would be needed to make the
    // "Enable Infrared Stream" toggle fully functional.

    // Sensor Controls
    QToolButton* RSAutoExposureToggleTool;
    QSlider* RSExposureSlider;
    QDoubleSpinBox* RSExposureSpinBox;
    QSlider* RSGainSlider;
    QDoubleSpinBox* RSGainSpinBox;
    QSlider* RSWhiteBalanceSlider;
    QDoubleSpinBox* RSWhiteBalanceSpinBox;

    // Advanced Toggles (placeholders)
    QToolButton* RSAllignStreamsToggleTool;
    QToolButton* RSTemporalFilterToggleTool;
    QToolButton* RSSpacialFilterToggleTool;

    // Preview
    QTimer* m_previewTimer;
    QLabel* m_previewLabel;
    QWidget* RSDeviceStreamPreview;
};