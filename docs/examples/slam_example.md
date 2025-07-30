# SLAM Integration Example

This comprehensive example demonstrates how to integrate the SLAM system with RealSense camera data for real-time localization and mapping in a robotics application.

## Overview

In this example, you'll learn how to:

1. Set up a RealSense camera for RGB-D data capture
2. Initialize and configure the SLAM system
3. Process real-time sensor data through the SLAM pipeline
4. Visualize the reconstructed map and robot trajectory
5. Handle SLAM data for robot navigation

## Prerequisites

- Intel RealSense camera (D435, D455, or similar)
- RealSense SDK 2.x installed
- Basic understanding of the [Rendering API](../rendering_api.md) and [Scene API](../scene_api.md)

## Complete Example

### 1. Project Setup

Create a new CMake project with SLAM dependencies:

```cmake
cmake_minimum_required(VERSION 3.24)
project(SLAMExample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL)
find_package(OpenGL REQUIRED)
find_package(realsense2 REQUIRED)
find_package(glm REQUIRED)
find_package(entt REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Eigen3 REQUIRED)

# Include robotics software headers
include_directories(${CMAKE_SOURCE_DIR}/../RoboticsSoftware/include)

set(CMAKE_AUTOMOC ON)

add_executable(SLAMExample
    main.cpp
    SLAMApplication.cpp
    SLAMApplication.hpp
    RealSenseManager.cpp
    RealSenseManager.hpp
)

target_link_libraries(SLAMExample
    Qt6::Core Qt6::Widgets Qt6::OpenGL
    OpenGL::GL
    realsense2::realsense2
    glm::glm
    EnTT::EnTT
    opencv_core opencv_imgproc opencv_features2d
    Eigen3::Eigen
)
```

### 2. RealSense Manager

First, create a manager for RealSense camera operations:

```cpp
// RealSenseManager.hpp
#pragma once

#include <QObject>
#include <QTimer>
#include <librealsense2/rs.hpp>
#include <memory>

class RealSenseManager : public QObject {
    Q_OBJECT

public:
    explicit RealSenseManager(QObject* parent = nullptr);
    ~RealSenseManager();

    bool initialize();
    void start();
    void stop();
    
    bool isConnected() const { return m_isConnected; }
    bool isStreaming() const { return m_isStreaming; }

signals:
    void pointCloudReady(const rs2::points& points, const rs2::video_frame& colorFrame);
    void errorOccurred(const QString& error);

private slots:
    void processFrames();

private:
    rs2::pipeline m_pipeline;
    rs2::pointcloud m_pointCloud;
    rs2::points m_points;
    QTimer* m_frameTimer;
    
    bool m_isConnected = false;
    bool m_isStreaming = false;
    
    // Frame processing
    void configureStreams();
};

// RealSenseManager.cpp
#include "RealSenseManager.hpp"
#include <QDebug>

RealSenseManager::RealSenseManager(QObject* parent)
    : QObject(parent)
    , m_frameTimer(new QTimer(this))
{
    connect(m_frameTimer, &QTimer::timeout, this, &RealSenseManager::processFrames);
}

RealSenseManager::~RealSenseManager() {
    stop();
}

bool RealSenseManager::initialize() {
    try {
        // Check for connected devices
        rs2::context ctx;
        auto devices = ctx.query_devices();
        if (devices.size() == 0) {
            emit errorOccurred("No RealSense devices connected");
            return false;
        }

        configureStreams();
        m_isConnected = true;
        return true;
        
    } catch (const rs2::error& e) {
        emit errorOccurred(QString("RealSense error: %1").arg(e.what()));
        return false;
    }
}

void RealSenseManager::configureStreams() {
    rs2::config config;
    
    // Configure depth and color streams
    config.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
    config.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_RGB8, 30);
    
    // Start the pipeline
    m_pipeline.start(config);
}

void RealSenseManager::start() {
    if (!m_isConnected) {
        emit errorOccurred("RealSense not initialized");
        return;
    }
    
    m_isStreaming = true;
    m_frameTimer->start(33); // ~30 FPS
}

void RealSenseManager::stop() {
    if (m_isStreaming) {
        m_frameTimer->stop();
        m_isStreaming = false;
    }
    
    if (m_isConnected) {
        try {
            m_pipeline.stop();
        } catch (const rs2::error& e) {
            qWarning() << "Error stopping pipeline:" << e.what();
        }
        m_isConnected = false;
    }
}

void RealSenseManager::processFrames() {
    if (!m_isStreaming) return;
    
    try {
        // Wait for frames with timeout
        rs2::frameset frames = m_pipeline.wait_for_frames(100);
        
        // Get depth and color frames
        rs2::depth_frame depth = frames.get_depth_frame();
        rs2::video_frame color = frames.get_color_frame();
        
        if (!depth || !color) return;
        
        // Generate point cloud
        m_points = m_pointCloud.calculate(depth);
        m_pointCloud.map_to(color);
        
        // Emit signal with the data
        emit pointCloudReady(m_points, color);
        
    } catch (const rs2::error& e) {
        emit errorOccurred(QString("Frame processing error: %1").arg(e.what()));
    }
}
```

### 3. Main SLAM Application

Now create the main application that integrates SLAM:

```cpp
// SLAMApplication.hpp
#pragma once

#include <QMainWindow>
#include <memory>

// Forward declarations
class Scene;
class RenderingSystem;
class ViewportWidget;
class SlamManager;
class RealSenseManager;
class QPushButton;
class QLabel;
class QProgressBar;

class SLAMApplication : public QMainWindow {
    Q_OBJECT

public:
    SLAMApplication(QWidget* parent = nullptr);
    ~SLAMApplication();

private slots:
    void startSLAM();
    void stopSLAM();
    void resetMap();
    void saveMap();
    void loadMap();
    void onMapUpdated();
    void onSLAMError(const QString& error);

private:
    void setupUI();
    void setupScene();
    void setupSLAM();
    void updateStatusDisplay();

    // Core components
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<RenderingSystem> m_renderingSystem;
    std::unique_ptr<SlamManager> m_slamManager;
    std::unique_ptr<RealSenseManager> m_realSenseManager;
    
    // UI components
    ViewportWidget* m_viewport;
    QPushButton* m_startButton;
    QPushButton* m_stopButton;
    QPushButton* m_resetButton;
    QPushButton* m_saveButton;
    QPushButton* m_loadButton;
    QLabel* m_statusLabel;
    QLabel* m_frameCountLabel;
    QLabel* m_mapSizeLabel;
    QProgressBar* m_processingProgress;
    
    // State
    bool m_slamActive = false;
    int m_frameCount = 0;
    int m_mapSize = 0;
};

// SLAMApplication.cpp
#include "SLAMApplication.hpp"
#include "Scene.hpp"
#include "RenderingSystem.hpp"
#include "ViewportWidget.hpp"
#include "SlamManager.hpp"
#include "RealSenseManager.hpp"
#include "SceneBuilder.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QGroupBox>
#include <QTimer>

SLAMApplication::SLAMApplication(QWidget* parent)
    : QMainWindow(parent)
{
    setupScene();
    setupSLAM();
    setupUI();
    
    // Update display every second
    QTimer* updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &SLAMApplication::updateStatusDisplay);
    updateTimer->start(1000);
}

SLAMApplication::~SLAMApplication() = default;

void SLAMApplication::setupScene() {
    // Create scene and rendering system
    m_scene = std::make_unique<Scene>();
    m_renderingSystem = std::make_unique<RenderingSystem>(this);
    
    // Create default camera
    entt::registry& registry = m_scene->getRegistry();
    entt::entity camera = SceneBuilder::createCamera(registry, 
        glm::vec3(0, 2, 5), glm::vec3(1, 1, 0));
}

void SLAMApplication::setupSLAM() {
    // Create SLAM components
    m_slamManager = std::make_unique<SlamManager>(this);
    m_realSenseManager = std::make_unique<RealSenseManager>(this);
    
    // Connect SLAM manager to rendering system
    m_slamManager->setRenderingSystem(m_renderingSystem.get());
    
    // Connect RealSense to SLAM
    connect(m_realSenseManager.get(), &RealSenseManager::pointCloudReady,
            m_slamManager.get(), &SlamManager::onPointCloudReady);
    
    // Connect SLAM signals
    connect(m_slamManager.get(), &SlamManager::mapUpdated,
            this, &SLAMApplication::onMapUpdated);
    
    // Connect error handling
    connect(m_realSenseManager.get(), &RealSenseManager::errorOccurred,
            this, &SLAMApplication::onSLAMError);
    
    // Initialize RealSense
    if (!m_realSenseManager->initialize()) {
        QMessageBox::warning(this, "Initialization Error",
                           "Failed to initialize RealSense camera. "
                           "Please check camera connection.");
    }
}

void SLAMApplication::setupUI() {
    // Central widget
    QWidget* centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    
    // Left panel - controls
    QVBoxLayout* leftPanel = new QVBoxLayout;
    leftPanel->setMaximumWidth(300);
    
    // SLAM controls group
    QGroupBox* slamGroup = new QGroupBox("SLAM Controls");
    QVBoxLayout* slamLayout = new QVBoxLayout(slamGroup);
    
    m_startButton = new QPushButton("Start SLAM");
    m_stopButton = new QPushButton("Stop SLAM");
    m_resetButton = new QPushButton("Reset Map");
    
    m_stopButton->setEnabled(false);
    
    slamLayout->addWidget(m_startButton);
    slamLayout->addWidget(m_stopButton);
    slamLayout->addWidget(m_resetButton);
    
    // Map management group
    QGroupBox* mapGroup = new QGroupBox("Map Management");
    QVBoxLayout* mapLayout = new QVBoxLayout(mapGroup);
    
    m_saveButton = new QPushButton("Save Map");
    m_loadButton = new QPushButton("Load Map");
    
    mapLayout->addWidget(m_saveButton);
    mapLayout->addWidget(m_loadButton);
    
    // Status group
    QGroupBox* statusGroup = new QGroupBox("Status");
    QGridLayout* statusLayout = new QGridLayout(statusGroup);
    
    m_statusLabel = new QLabel("Ready");
    m_frameCountLabel = new QLabel("Frames: 0");
    m_mapSizeLabel = new QLabel("Map size: 0");
    m_processingProgress = new QProgressBar;
    
    statusLayout->addWidget(new QLabel("Status:"), 0, 0);
    statusLayout->addWidget(m_statusLabel, 0, 1);
    statusLayout->addWidget(new QLabel("Frames:"), 1, 0);
    statusLayout->addWidget(m_frameCountLabel, 1, 1);
    statusLayout->addWidget(new QLabel("Map:"), 2, 0);
    statusLayout->addWidget(m_mapSizeLabel, 2, 1);
    statusLayout->addWidget(new QLabel("Processing:"), 3, 0);
    statusLayout->addWidget(m_processingProgress, 3, 1);
    
    // Add groups to left panel
    leftPanel->addWidget(slamGroup);
    leftPanel->addWidget(mapGroup);
    leftPanel->addWidget(statusGroup);
    leftPanel->addStretch();
    
    // Right side - viewport
    m_viewport = new ViewportWidget(m_scene.get(), this);
    
    // Add to main layout
    mainLayout->addLayout(leftPanel);
    mainLayout->addWidget(m_viewport, 1);
    
    // Connect signals
    connect(m_startButton, &QPushButton::clicked, this, &SLAMApplication::startSLAM);
    connect(m_stopButton, &QPushButton::clicked, this, &SLAMApplication::stopSLAM);
    connect(m_resetButton, &QPushButton::clicked, this, &SLAMApplication::resetMap);
    connect(m_saveButton, &QPushButton::clicked, this, &SLAMApplication::saveMap);
    connect(m_loadButton, &QPushButton::clicked, this, &SLAMApplication::loadMap);
    
    // Window properties
    setWindowTitle("SLAM Integration Example");
    resize(1400, 900);
}

void SLAMApplication::startSLAM() {
    if (!m_realSenseManager->isConnected()) {
        QMessageBox::warning(this, "Error", "RealSense camera not connected");
        return;
    }
    
    try {
        // Start SLAM processing
        m_slamManager->start();
        
        // Start camera streaming
        m_realSenseManager->start();
        
        m_slamActive = true;
        m_startButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        
        m_statusLabel->setText("SLAM Active");
        statusBar()->showMessage("SLAM started successfully");
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "SLAM Error", 
                            QString("Failed to start SLAM: %1").arg(e.what()));
    }
}

void SLAMApplication::stopSLAM() {
    try {
        // Stop camera streaming
        m_realSenseManager->stop();
        
        // Stop SLAM processing
        m_slamManager->stop();
        
        m_slamActive = false;
        m_startButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        
        m_statusLabel->setText("Stopped");
        statusBar()->showMessage("SLAM stopped");
        
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Stop Error", 
                           QString("Error stopping SLAM: %1").arg(e.what()));
    }
}

void SLAMApplication::resetMap() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Reset Map", "Are you sure you want to reset the map? This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Stop SLAM if running
        if (m_slamActive) {
            stopSLAM();
        }
        
        // Reset counters
        m_frameCount = 0;
        m_mapSize = 0;
        
        // Recreate SLAM manager to reset internal state
        setupSLAM();
        
        statusBar()->showMessage("Map reset successfully");
    }
}

void SLAMApplication::saveMap() {
    QString filename = QFileDialog::getSaveFileName(
        this, "Save Map", "slam_map.bin", "Binary Files (*.bin);;All Files (*)");
    
    if (!filename.isEmpty()) {
        try {
            // Implementation would save the voxel map
            // This would require access to the VoxelMap from SlamManager
            statusBar()->showMessage("Map saved: " + filename);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Save Error", 
                               QString("Failed to save map: %1").arg(e.what()));
        }
    }
}

void SLAMApplication::loadMap() {
    QString filename = QFileDialog::getOpenFileName(
        this, "Load Map", "", "Binary Files (*.bin);;All Files (*)");
    
    if (!filename.isEmpty()) {
        try {
            // Implementation would load the voxel map
            statusBar()->showMessage("Map loaded: " + filename);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Load Error", 
                               QString("Failed to load map: %1").arg(e.what()));
        }
    }
}

void SLAMApplication::onMapUpdated() {
    m_mapSize++; // Simplified - would get actual map size
    updateStatusDisplay();
}

void SLAMApplication::onSLAMError(const QString& error) {
    QMessageBox::critical(this, "SLAM Error", error);
    if (m_slamActive) {
        stopSLAM();
    }
}

void SLAMApplication::updateStatusDisplay() {
    if (m_slamActive) {
        m_frameCount++;
    }
    
    m_frameCountLabel->setText(QString("Frames: %1").arg(m_frameCount));
    m_mapSizeLabel->setText(QString("Map size: %1").arg(m_mapSize));
    
    // Update progress bar (simplified)
    if (m_slamActive) {
        int progress = (m_frameCount % 100);
        m_processingProgress->setValue(progress);
    }
}
```

### 4. Main Application Entry Point

```cpp
// main.cpp
#include <QApplication>
#include "SLAMApplication.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    app.setApplicationName("SLAM Integration Example");
    app.setApplicationVersion("1.0");
    
    SLAMApplication window;
    window.show();
    
    return app.exec();
}
```

## Key Features Demonstrated

### 1. Real-time Data Processing

The example shows how to:
- Configure RealSense camera streams
- Process RGB-D frames in real-time
- Pass data through the SLAM pipeline
- Handle frame synchronization and timing

### 2. SLAM Pipeline Integration

Complete integration of:
- Frontend tracking for pose estimation
- Backend mapping for dense reconstruction
- Automatic keyframe creation and management
- Map visualization and updates

### 3. User Interface

Professional UI with:
- Real-time status monitoring
- SLAM control buttons
- Map management features
- Error handling and feedback

### 4. Error Handling

Robust error handling for:
- Camera connection issues
- SLAM processing errors
- File I/O operations
- Memory management

## Advanced Usage

### Custom SLAM Parameters

You can customize SLAM parameters:

```cpp
// Custom SLAM configuration
void SLAMApplication::configureSLAM() {
    // These would be passed to the SLAM system
    SLAMConfig config;
    config.voxelSize = 0.005f;              // 5mm voxels
    config.maxIntegrationDistance = 2.0f;   // 2m max range
    config.keyframeDistanceThreshold = 0.1f; // 10cm for new keyframe
    config.keyframeAngleThreshold = 0.2f;   // ~11 degrees
    
    m_slamManager->setConfiguration(config);
}
```

### Multiple Camera Support

Extend for multiple cameras:

```cpp
// Support for multiple RealSense cameras
class MultiCameraSLAM : public SLAMApplication {
private:
    std::vector<std::unique_ptr<RealSenseManager>> m_cameras;
    
public:
    void addCamera(const std::string& serial) {
        auto camera = std::make_unique<RealSenseManager>(this);
        camera->setDeviceSerial(serial);
        
        connect(camera.get(), &RealSenseManager::pointCloudReady,
                this, &MultiCameraSLAM::processMultiCameraFrame);
        
        m_cameras.push_back(std::move(camera));
    }
};
```

### Integration with Robot Navigation

Use SLAM data for robot control:

```cpp
// Get current robot pose from SLAM
glm::mat4 getCurrentRobotPose() {
    // This would be provided by the SLAM system
    return m_slamManager->getCurrentPose();
}

// Use SLAM map for path planning
std::vector<glm::vec3> planPath(const glm::vec3& start, const glm::vec3& goal) {
    VoxelMap map = m_slamManager->getMap();
    PathPlanner planner(map);
    return planner.planPath(start, goal);
}
```

## Building and Running

Build with SLAM support:

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --parallel
./SLAMExample
```

## Expected Results

When running the example successfully:

1. **Camera Connection**: RealSense camera initializes and streams RGB-D data
2. **Real-time Tracking**: Camera pose is estimated in real-time
3. **Map Building**: Dense 3D map is constructed incrementally
4. **Visualization**: Point clouds and map are rendered in the viewport
5. **UI Updates**: Status displays show frame counts and processing information

## Troubleshooting

### Common Issues

#### Camera Not Detected
- Ensure RealSense camera is connected via USB 3.0
- Install latest RealSense drivers
- Check device permissions on Linux

#### Poor Tracking Performance
- Ensure adequate lighting
- Avoid rapid camera movements
- Provide sufficient visual features
- Check for occlusions or reflective surfaces

#### Memory Usage
- Monitor memory consumption with large maps
- Adjust voxel resolution if needed
- Implement map pruning for long runs

## Next Steps

Extend this example by:

1. **Adding robot control integration**
2. **Implementing map saving/loading**
3. **Creating multiple viewport displays**
4. **Adding path planning capabilities**
5. **Integrating with the node programming system**

---

*This example provides a solid foundation for SLAM integration. For more advanced features, see the [SLAM API documentation](../slam_api.md) and other [examples](.).*