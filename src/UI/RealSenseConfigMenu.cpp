#include "RealSenseConfigMenu.hpp"
#include "ui_RealSenseConfigMenu.h"
#include <QDebug>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <set>
#include <sstream>
#include <QRegularExpression>

RealSenseConfigMenu::RealSenseConfigMenu(QWidget* parent) :
    QWidget(parent),
    m_realSenseManager(std::make_unique<RealSenseManager>()),
    m_align(RS2_STREAM_COLOR)
{
    setupUi(); // Manually create and layout all widgets
    setupConnections(); // Connect signals and slots
    setupPreview();
    // Set default and initial states for UI toggles and buttons
    enableDepthStream_toggle_tool->setChecked(true);
    enableRGBStream_toggle_tool->setChecked(true);
    RSAutoExposureToggleTool->setChecked(true);
    stopStreamingRSButton->setEnabled(false);

    populateDeviceList();
}

RealSenseConfigMenu::~RealSenseConfigMenu()
{
    // In a code-only UI, you don't 'delete ui'. The QWidget parent/child
    // system handles memory management of the widgets.
}

void RealSenseConfigMenu::setupUi()
{
    // This function manually creates the UI, replacing the auto-generated one.
    // It can be verbose, which is the tradeoff for not using a .ui file.

    auto* mainLayout = new QGridLayout(this);
    this->setObjectName("RealSenseConfigMenu");

    // --- Device Selection Group ---
    auto* deviceGroup = new QGroupBox("Available RealSense Devices", this);
    deviceGroup->setAlignment(Qt::AlignCenter);
    auto* deviceLayout = new QVBoxLayout(deviceGroup);

    refreshRealSenseDevicesButton = new QToolButton(this);
    refreshRealSenseDevicesButton->setText("Refresh RealSense Devices");
    refreshRealSenseDevicesButton->setMinimumSize(QSize(150, 40));

    RealSenseCameraSelectionComboBox = new QComboBox(this);
    RealSenseCameraSelectionComboBox->setMinimumSize(QSize(0, 30));

    RSActiveDevicePropertiesList = new QTableWidget(this);
    RSActiveDevicePropertiesList->setColumnCount(1);
    RSActiveDevicePropertiesList->setHorizontalHeaderItem(0, new QTableWidgetItem("Active Device Value"));
    RSActiveDevicePropertiesList->setRowCount(4);
    RSActiveDevicePropertiesList->setVerticalHeaderItem(0, new QTableWidgetItem("Model Name"));
    RSActiveDevicePropertiesList->setVerticalHeaderItem(1, new QTableWidgetItem("Serial Number"));
    RSActiveDevicePropertiesList->setVerticalHeaderItem(2, new QTableWidgetItem("Firmware Version"));
    RSActiveDevicePropertiesList->setVerticalHeaderItem(3, new QTableWidgetItem("Product ID"));
    RSActiveDevicePropertiesList->horizontalHeader()->setStretchLastSection(true);

    deviceLayout->addWidget(refreshRealSenseDevicesButton);
    deviceLayout->addWidget(RealSenseCameraSelectionComboBox);
    deviceLayout->addWidget(RSActiveDevicePropertiesList);

    // --- Streaming Control Group ---
    auto* controlGroup = new QGroupBox("Streaming Controls", this);
    controlGroup->setAlignment(Qt::AlignCenter);
    auto* controlLayout = new QGridLayout(controlGroup);

    startStreamingRSButton = new QToolButton(this);
    startStreamingRSButton->setText("Start Streaming");
    startStreamingRSButton->setMinimumSize(QSize(0, 40));
    stopStreamingRSButton = new QToolButton(this);
    stopStreamingRSButton->setText("Stop Streaming");
    stopStreamingRSButton->setMinimumSize(QSize(0, 40));

    controlLayout->addWidget(startStreamingRSButton, 0, 0);
    controlLayout->addWidget(stopStreamingRSButton, 0, 1);

    // --- Stream Config Group ---
    auto* configGroup = new QGroupBox("Stream Configuration", this);
    auto* configLayout = new QFormLayout(configGroup);

    enableDepthStream_toggle_tool = new QToolButton(this);
    enableDepthStream_toggle_tool->setText("Enable Depth Stream");
    enableDepthStream_toggle_tool->setCheckable(true);
    enableRGBStream_toggle_tool = new QToolButton(this);
    enableRGBStream_toggle_tool->setText("Enable Color Stream");
    enableRGBStream_toggle_tool->setCheckable(true);
    enableInfaredStream_toggle_tool = new QToolButton(this);
    enableInfaredStream_toggle_tool->setText("Enable Infrared Stream");
    enableInfaredStream_toggle_tool->setCheckable(true);

    depthProfileCombo = new QComboBox(this);
    colorProfileCombo = new QComboBox(this);
    RSPreviewsourceComboBox = new QComboBox(this);

    configLayout->addRow(enableDepthStream_toggle_tool);
    configLayout->addRow(enableRGBStream_toggle_tool);
    configLayout->addRow(enableInfaredStream_toggle_tool);
    configLayout->addRow("Depth Profile:", depthProfileCombo);
    configLayout->addRow("Color Profile:", colorProfileCombo);
    configLayout->addRow("Preview Source:", RSPreviewsourceComboBox);

    // --- Sensor Control Group ---
    auto* sensorGroup = new QGroupBox("Sensor Controls", this);
    auto* sensorLayout = new QGridLayout(sensorGroup);

    RSAutoExposureToggleTool = new QToolButton(this);
    RSAutoExposureToggleTool->setText("Auto-Exposure");
    RSAutoExposureToggleTool->setCheckable(true);

    RSExposureSlider = new QSlider(Qt::Horizontal, this);
    RSExposureSpinBox = new QDoubleSpinBox(this);
    RSGainSlider = new QSlider(Qt::Horizontal, this);
    RSGainSpinBox = new QDoubleSpinBox(this);
    RSWhiteBalanceSlider = new QSlider(Qt::Horizontal, this);
    RSWhiteBalanceSpinBox = new QDoubleSpinBox(this);

    sensorLayout->addWidget(new QLabel("Exposure:"), 0, 0);
    sensorLayout->addWidget(RSExposureSlider, 0, 1);
    sensorLayout->addWidget(RSExposureSpinBox, 0, 2);
    sensorLayout->addWidget(new QLabel("Gain:"), 1, 0);
    sensorLayout->addWidget(RSGainSlider, 1, 1);
    sensorLayout->addWidget(RSGainSpinBox, 1, 2);
    sensorLayout->addWidget(new QLabel("White Balance:"), 2, 0);
    sensorLayout->addWidget(RSWhiteBalanceSlider, 2, 1);
    sensorLayout->addWidget(RSWhiteBalanceSpinBox, 2, 2);
    sensorLayout->addWidget(RSAutoExposureToggleTool, 3, 0, 1, 3);

    // --- Add all groups to the main layout ---
    mainLayout->addWidget(deviceGroup, 0, 0);
    mainLayout->addWidget(controlGroup, 1, 0);
    mainLayout->addWidget(configGroup, 2, 0);
    mainLayout->addWidget(sensorGroup, 3, 0);

    // --- Preview ---
    auto* previewGroup = new QGroupBox("Stream Preview", this);
    previewGroup->setAlignment(Qt::AlignCenter);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    RSDeviceStreamPreview = new QWidget(this);
    RSDeviceStreamPreview->setMinimumSize(QSize(0, 200));
    previewLayout->addWidget(RSDeviceStreamPreview);
    mainLayout->addWidget(previewGroup, 4, 0);
}

// All other methods from the previous implementation remain the same,
// just without the 'ui->' prefix. I am including them here for completeness.
// The logic inside them is identical.

void RealSenseConfigMenu::setupConnections()
{
    connect(refreshRealSenseDevicesButton, &QToolButton::clicked, this, &RealSenseConfigMenu::onRefreshDevicesClicked);
    connect(startStreamingRSButton, &QToolButton::clicked, this, &RealSenseConfigMenu::onStartStreamingClicked);
    connect(stopStreamingRSButton, &QToolButton::clicked, this, &RealSenseConfigMenu::onStopStreamingClicked);
    connect(RealSenseCameraSelectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RealSenseConfigMenu::onDeviceSelectionChanged);
    m_previewTimer = new QTimer(this);
    connect(m_previewTimer, &QTimer::timeout, this, &RealSenseConfigMenu::updatePreview);

    // --- NEW: Logic to make Depth and Infrared mutually exclusive ---
    connect(enableDepthStream_toggle_tool, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            // If Depth is checked, uncheck Infrared
            const QSignalBlocker blocker(enableInfaredStream_toggle_tool);
            enableInfaredStream_toggle_tool->setChecked(false);
        }
        });

    connect(enableInfaredStream_toggle_tool, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            // If Infrared is checked, uncheck Depth
            const QSignalBlocker blocker(enableDepthStream_toggle_tool);
            enableDepthStream_toggle_tool->setChecked(false);
        }
        });

    linkSliderAndSpinBox(RSExposureSlider, RSExposureSpinBox, RS2_OPTION_EXPOSURE);
    linkSliderAndSpinBox(RSGainSlider, RSGainSpinBox, RS2_OPTION_GAIN);
    linkSliderAndSpinBox(RSWhiteBalanceSlider, RSWhiteBalanceSpinBox, RS2_OPTION_WHITE_BALANCE);
    connect(RSAutoExposureToggleTool, &QToolButton::toggled, this, [this](bool checked) {
        m_realSenseManager->setSensorOption(RS2_OPTION_ENABLE_AUTO_EXPOSURE, checked ? 1.0f : 0.0f);
        });
}

void RealSenseConfigMenu::setupPreview()
{
    m_previewLabel = new QLabel("Stream preview will appear here...", this);
    m_previewLabel->setAlignment(Qt::AlignCenter);

    // Set scaledContents to false. We will handle the scaling manually.
    m_previewLabel->setScaledContents(false);

    auto* layout = new QVBoxLayout(RSDeviceStreamPreview);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_previewLabel);
    RSDeviceStreamPreview->setLayout(layout);
    RSPreviewsourceComboBox->clear();
    RSPreviewsourceComboBox->addItems({ "Color", "Depth" });
}

void RealSenseConfigMenu::onRefreshDevicesClicked()
{
    populateDeviceList();
}

void RealSenseConfigMenu::populateProfileDropdowns(const std::string& serialNumber)
{
    // Block signals to prevent updates while repopulating
    const QSignalBlocker depthBlocker(depthProfileCombo);
    const QSignalBlocker colorBlocker(colorProfileCombo);

    depthProfileCombo->clear(); // Clear previous profiles
    colorProfileCombo->clear(); // Clear previous profiles

    // Get all unique, supported profiles from the manager
    auto profiles = m_realSenseManager->getSupportedProfiles(serialNumber);

    for (const auto& p : profiles)
    {
        // Create a descriptive string for the user to see in the UI
        QString profileString = QString("%1 x %2 @ %3 FPS (%4)")
            .arg(p.stream_width)
            .arg(p.stream_height)
            .arg(p.stream_fps)
            .arg(rs2_format_to_string(p.stream_format));

        // Create a QVariant to hold the actual StreamProfile struct
        QVariant profileData = QVariant::fromValue(p);

        // Add the profile to the correct dropdown based on its stream type
        if (p.stream_type == RS2_STREAM_DEPTH && p.stream_format == RS2_FORMAT_Z16) {
            depthProfileCombo->addItem(profileString, profileData); // Add text and data
        }
        else if (p.stream_type == RS2_STREAM_COLOR && (p.stream_format == RS2_FORMAT_RGB8 || p.stream_format == RS2_FORMAT_BGR8)) {
            // Note: Use RGB8 for QImage conversion, but some cameras provide BGR8.
            // The manager should handle the stream request; the preview handles the data format.
            colorProfileCombo->addItem(profileString, profileData); // Add text and data
        }
        // NOTE: A similar block for RS2_STREAM_INFRARED would be needed to populate an IR dropdown.
    }
}

void RealSenseConfigMenu::populateDeviceList()
{
    const QSignalBlocker blocker(RealSenseCameraSelectionComboBox);
    RealSenseCameraSelectionComboBox->clear();
    m_availableDevices = m_realSenseManager->getAvailableDevices();
    if (m_availableDevices.empty()) {
        RealSenseCameraSelectionComboBox->addItem("No devices found");
        startStreamingRSButton->setEnabled(false);
    }
    else {
        for (const auto& device : m_availableDevices) {
            RealSenseCameraSelectionComboBox->addItem(QString::fromStdString(device.name));
        }
        startStreamingRSButton->setEnabled(true);
    }
    onDeviceSelectionChanged(RealSenseCameraSelectionComboBox->currentIndex());
}

void RealSenseConfigMenu::onDeviceSelectionChanged(int index)
{
    if (index < 0 || index >= m_availableDevices.size()) return;

    const std::string& serial = m_availableDevices[index].serialNumber;
    populateProfileDropdowns(serial); // Call the new population function
    updateDeviceInfoTable(serial);
}


void RealSenseConfigMenu::onStartStreamingClicked()
{
    if (m_availableDevices.empty()) return; // Exit if no devices are available

    // Get the serial number of the currently selected device
    const std::string& serial = m_availableDevices[RealSenseCameraSelectionComboBox->currentIndex()].serialNumber;

    // 1. Create a vector to hold the profiles selected in the UI.
    std::vector<StreamProfile> requested_profiles;

    // 2. Add the selected profiles to the vector if their streams are enabled.
    if (enableDepthStream_toggle_tool->isChecked() && depthProfileCombo->currentIndex() != -1) {
        // Retrieve the QVariant data and cast it back to a StreamProfile
        StreamProfile p = depthProfileCombo->currentData().value<StreamProfile>();
        requested_profiles.push_back(p);
    }
    if (enableRGBStream_toggle_tool->isChecked() && colorProfileCombo->currentIndex() != -1) {
        // Retrieve the QVariant data for the color profile
        StreamProfile p = colorProfileCombo->currentData().value<StreamProfile>();
        requested_profiles.push_back(p);
    }
    // TODO: Add logic for infrared stream if an IR profile selection dropdown is added.

    if (requested_profiles.empty()) {
        QMessageBox::warning(this, "No Streams Selected", "Please enable and select a profile for at least one stream.");
        return;
    }

    // 3. Call the startStreaming method with the serial number and the list of profiles.
    if (m_realSenseManager->startStreaming(serial, requested_profiles)) {
        startStreamingRSButton->setEnabled(false); // Disable start button
        stopStreamingRSButton->setEnabled(true);   // Enable stop button
        m_previewTimer->start(33); // Start the preview timer (for ~30 FPS)
    }
    else {
        // Show an error message if streaming fails to start
        QMessageBox::critical(this, "Streaming Error", "Failed to start streams. Check console for details.");
    }

    emit startStreamingRequested(serial, requested_profiles);
}

void RealSenseConfigMenu::onStopStreamingClicked()
{
    m_previewTimer->stop(); // Stop the preview update timer
    m_realSenseManager->stopStreaming(); // Tell the manager to stop the pipeline
    startStreamingRSButton->setEnabled(true); // Re-enable the start button
    stopStreamingRSButton->setEnabled(false); // Disable the stop button
    m_previewLabel->setText("Stream stopped."); // Update the preview label
}

void RealSenseConfigMenu::updatePreview()
{
    rs2::frameset frames;
    if (!m_realSenseManager->pollFrames(frames)) {
        return;
    }

    // 1. Align the frames. This creates a new, synthetic frameset where the
    // depth and color images are perfectly matched.
    auto aligned_frames = m_align.process(frames);

    rs2::video_frame color_frame = aligned_frames.get_color_frame();
    rs2::depth_frame depth_frame = aligned_frames.get_depth_frame();

    if (!color_frame || !depth_frame) {
        return; // Exit if we don't have both frames
    }

    // --- Point Cloud Generation ---
    // Tell the pointcloud object to map its points to the color frame
    m_pointcloud.map_to(color_frame);
    // Generate the point cloud from the depth frame
    rs2::points points = m_pointcloud.calculate(depth_frame);

    // --- Emit the data for other parts of your application ---
    emit pointCloudReady(points, color_frame);

    // --- The rest of this function is for the UI preview ONLY ---
    // You can keep this preview logic as-is.
    rs2::frame frame_for_preview = nullptr;
    std::string preview_source = RSPreviewsourceComboBox->currentText().toStdString();

    if (preview_source == "Depth") {
        frame_for_preview = m_colorizer.colorize(depth_frame);
    }
    else {
        frame_for_preview = color_frame;
    }

    if (!frame_for_preview) {
        m_previewLabel->setText("Waiting for '" + QString::fromStdString(preview_source) + "' frame...");
        return;
    };

    // Cast the generic frame to a video_frame to get dimensions and data
    if (rs2::video_frame video_frame = frame_for_preview.as<rs2::video_frame>()) {

        // Determine the QImage format based on the stream format
        QImage::Format format = QImage::Format_RGB888; // Default to RGB888
        if (video_frame.get_profile().format() == RS2_FORMAT_BGR8) {
            format = QImage::Format_BGR888;
        }

        // Create a QImage wrapper around the librealsense frame data
        QImage qimg(
            static_cast<const uchar*>(video_frame.get_data()),
            video_frame.get_width(),
            video_frame.get_height(),
            video_frame.get_stride_in_bytes(),
            format
        );

        QPixmap pixmap = QPixmap::fromImage(qimg);
        QPixmap scaledPixmap = pixmap.scaled(m_previewLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        // Display the QImage in the label. A copy is made to ensure thread safety.
        m_previewLabel->setPixmap(scaledPixmap);
    }
}

void RealSenseConfigMenu::updateDeviceInfoTable(const std::string& serialNumber)
{
    rs2::context ctx;
    for (auto&& dev : ctx.query_devices()) {
        if (serialNumber == dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
            RSActiveDevicePropertiesList->setItem(0, 0, new QTableWidgetItem(dev.get_info(RS2_CAMERA_INFO_NAME)));
            RSActiveDevicePropertiesList->setItem(1, 0, new QTableWidgetItem(dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)));
            RSActiveDevicePropertiesList->setItem(2, 0, new QTableWidgetItem(dev.get_info(RS2_CAMERA_INFO_FIRMWARE_VERSION)));
            RSActiveDevicePropertiesList->setItem(3, 0, new QTableWidgetItem(dev.get_info(RS2_CAMERA_INFO_PRODUCT_ID)));
            return;
        }
    }
}

void RealSenseConfigMenu::linkSliderAndSpinBox(QSlider* slider, QDoubleSpinBox* spinBox, rs2_option option)
{
    connect(slider, &QSlider::valueChanged, this, [this, spinBox, option](int value) {
        const QSignalBlocker blocker(spinBox);
        spinBox->setValue(value);
        m_realSenseManager->setSensorOption(option, static_cast<float>(value));
        });
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider, option](double value) {
        const QSignalBlocker blocker(slider);
        slider->setValue(static_cast<int>(value));
        m_realSenseManager->setSensorOption(option, static_cast<float>(value));
        });
}

void RealSenseConfigMenu::logAllStreamProfiles(const std::string& serialNumber)
{
    rs2::context ctx;
    for (auto&& dev : ctx.query_devices())
    {
        if (serialNumber != dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
            continue; // Skip devices that don't match the serial
        }

        qDebug() << "--- DEBUG: AVAILABLE STREAM PROFILES for"
            << dev.get_info(RS2_CAMERA_INFO_NAME)
            << "(" << serialNumber << ") ---";

        for (auto& sensor : dev.query_sensors())
        {
            qDebug() << "  SENSOR:" << sensor.get_info(RS2_CAMERA_INFO_NAME);
            for (auto& profile : sensor.get_stream_profiles())
            {
                // We are only interested in video streams for this UI
                if (auto video_profile = profile.as<rs2::video_stream_profile>())
                {
                    std::stringstream ss;
                    ss << "    - PROFILE: " << rs2_stream_to_string(video_profile.stream_type())
                        << " #" << video_profile.stream_index()
                        << "\tFormat: " << rs2_format_to_string(video_profile.format())
                        << "\tResolution: " << video_profile.width() << "x" << video_profile.height()
                        << "\tFPS: " << video_profile.fps();
                    qDebug().noquote() << QString::fromStdString(ss.str());
                }
            }
        }
        qDebug() << "--- END DEBUG ---";
        return; // We found our device, no need to loop further
    }
}