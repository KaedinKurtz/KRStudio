# SLAM API

The SLAM (Simultaneous Localization and Mapping) system provides real-time localization and dense 3D mapping capabilities with RealSense camera integration. It features a modern multi-threaded architecture with frontend tracking, backend optimization, and efficient voxel-based mapping.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Core Classes](#core-classes)
4. [Data Structures](#data-structures)
5. [Integration](#integration)
6. [Usage Examples](#usage-examples)
7. [API Reference](#api-reference)

## Overview

The SLAM system is designed for real-time robotics applications requiring:

- **Real-time Tracking**: 6-DOF pose estimation from RGB-D sensor data
- **Dense Mapping**: Efficient voxel-based 3D reconstruction
- **Multi-threading**: Separate frontend and backend processing for optimal performance
- **RealSense Integration**: Native support for Intel RealSense RGB-D cameras
- **Rendering Integration**: Seamless integration with the rendering system for visualization

### Key Features

- Multi-threaded architecture (Frontend/Backend separation)
- Dense voxel-based mapping with surfel representation
- Real-time pose estimation and tracking
- RealSense RGB-D camera integration
- Thread-safe map access and updates
- Automatic keyframe creation and management
- Memory-efficient sparse voxel grid

## Architecture

The SLAM system follows a producer-consumer pattern with clear separation of concerns:

```
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐
│   RealSense     │───▶│ SlamManager  │───▶│ RenderingSystem │
│   Camera        │    │              │    │                 │
└─────────────────┘    └──────┬───────┘    └─────────────────┘
                              │
                    ┌─────────┼─────────┐
                    ▼                   ▼
            ┌───────────────┐   ┌───────────────┐
            │   Frontend    │   │   Backend     │
            │ (Tracking)    │   │ (Mapping)     │
            └───────┬───────┘   └───────┬───────┘
                    │                   │
                    ▼                   ▼
            ┌───────────────┐   ┌───────────────┐
            │   KeyFrame    │   │   VoxelMap    │
            │   Creation    │   │   (Dense 3D)  │
            └───────────────┘   └───────────────┘
```

### Threading Model

- **Main Thread**: UI, rendering, and coordination
- **Frontend Thread**: Real-time tracking and pose estimation
- **Backend Thread**: Map optimization and dense reconstruction

## Core Classes

### SlamManager

The central coordinator that manages the entire SLAM pipeline and coordinates between components.

```cpp
class SlamManager : public QObject {
    Q_OBJECT

public:
    explicit SlamManager(QObject* parent = nullptr);
    ~SlamManager();

    // Lifecycle management
    void start();
    void stop();
    void setRenderingSystem(RenderingSystem* renderer);

public slots:
    void onPointCloudReady(const rs2::points& points, 
                          const rs2::video_frame& colorFrame);
    void onPoseUpdatedForRender(const glm::mat4& pose, 
                               std::shared_ptr<rs2::points> points, 
                               std::shared_ptr<rs2::video_frame> colorFrame);

signals:
    void newFrameData(double timestamp, 
                     std::shared_ptr<rs2::points> points, 
                     std::shared_ptr<rs2::video_frame> colorFrame);
    void mapUpdated();

private:
    QThread m_frontend_thread;
    QThread m_backend_thread;
    Frontend* m_frontend = nullptr;
    Backend* m_backend = nullptr;
    std::shared_ptr<VoxelMap> m_voxel_map;
    RenderingSystem* m_rendering_system = nullptr;
};
```

#### Usage Example

```cpp
// Create and setup SLAM manager
SlamManager* slamManager = new SlamManager(this);
slamManager->setRenderingSystem(renderingSystem);

// Connect to RealSense data source
connect(realSenseManager, &RealSenseManager::pointCloudReady,
        slamManager, &SlamManager::onPointCloudReady);

// Connect to map updates for UI refresh
connect(slamManager, &SlamManager::mapUpdated,
        this, &MainWindow::onMapUpdated);

// Start SLAM processing
slamManager->start();
```

### Frontend

Handles real-time tracking, pose estimation, and keyframe creation. Runs in a separate thread for optimal performance.

```cpp
class Frontend : public QObject {
    Q_OBJECT

public:
    explicit Frontend(QObject* parent = nullptr);
    ~Frontend();

public slots:
    void processNewFrame(double timestamp, 
                        std::shared_ptr<rs2::points> points, 
                        std::shared_ptr<rs2::video_frame> colorFrame);

signals:
    void keyframeCreated(KeyFrame::Ptr keyframe);
    void poseUpdatedForRender(const glm::mat4& pose, 
                             std::shared_ptr<rs2::points> points, 
                             std::shared_ptr<rs2::video_frame> colorFrame);
};
```

#### Key Responsibilities

1. **Frame Processing**: Processes incoming RGB-D frames from sensors
2. **Pose Estimation**: Estimates 6-DOF camera pose for each frame
3. **Keyframe Detection**: Decides when to create new keyframes based on motion
4. **Data Preparation**: Converts sensor data to internal representations

### Backend

Handles dense mapping, map optimization, and data fusion. Operates asynchronously to maintain real-time performance.

```cpp
class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(std::shared_ptr<VoxelMap> map, QObject* parent = nullptr);
    ~Backend();

public slots:
    void processNewKeyframe(KeyFrame::Ptr keyframe);

signals:
    void mapUpdated();

private:
    std::shared_ptr<VoxelMap> m_map;
};
```

#### Key Responsibilities

1. **Keyframe Processing**: Processes keyframes from the frontend
2. **Map Fusion**: Integrates new data into the voxel map
3. **Map Optimization**: Optimizes map consistency and accuracy
4. **Memory Management**: Maintains efficient memory usage

### VoxelMap

Implements a memory-efficient sparse voxel grid for dense 3D mapping using surfel-based representation.

```cpp
class VoxelMap {
public:
    VoxelMap(float voxel_size = 0.01f); // Voxel size in meters

    // Map operations
    void fuse(const KeyFrame::Ptr& keyframe);
    std::vector<Surfel> getSurfels() const;

private:
    std::unordered_map<VoxelIndex, Surfel> m_grid;
    float m_voxel_size;
    float m_inv_voxel_size;
    mutable std::mutex m_map_mutex;
};
```

#### Features

- **Sparse Representation**: Only stores occupied voxels for memory efficiency
- **Surfel-based**: Uses surface elements for accurate surface representation
- **Thread-safe**: Mutex protection for concurrent access
- **Configurable Resolution**: Adjustable voxel size for different applications

#### Usage Example

```cpp
// Create voxel map with 1cm resolution
auto voxelMap = std::make_shared<VoxelMap>(0.01f);

// Fuse a keyframe into the map
KeyFrame::Ptr keyframe = createKeyFrame(pointCloud, pose, timestamp);
voxelMap->fuse(keyframe);

// Extract surfels for rendering
std::vector<Surfel> surfels = voxelMap->getSurfels();
for (const auto& surfel : surfels) {
    // Render or process each surfel
    renderSurfel(surfel.position, surfel.normal, surfel.radius);
}
```

## Data Structures

### KeyFrame

Represents a snapshot of sensor data with associated pose and metadata.

```cpp
struct KeyFrame {
    using Ptr = std::shared_ptr<KeyFrame>;

    long long id = 0;                                    // Unique identifier
    double timestamp = 0.0;                              // Capture timestamp
    Eigen::Isometry3f pose = Eigen::Isometry3f::Identity(); // 6-DOF pose
    
    // Point cloud data
    std::vector<Eigen::Vector3f> point_cloud;
    std::vector<Eigen::Vector2f> texture_coordinates;
    std::vector<uint8_t> color_data;
    int color_width = 0, color_height = 0;
    int color_bpp = 0; // Bytes per pixel
};
```

#### Creating KeyFrames

```cpp
// Create a new keyframe from sensor data
KeyFrame::Ptr createKeyFrame(const rs2::points& points,
                            const rs2::video_frame& colorFrame,
                            const Eigen::Isometry3f& pose,
                            double timestamp) {
    auto keyframe = std::make_shared<KeyFrame>();
    keyframe->id = generateUniqueId();
    keyframe->timestamp = timestamp;
    keyframe->pose = pose;
    
    // Convert RealSense points to Eigen vectors
    const rs2::vertex* vertices = points.get_vertices();
    for (size_t i = 0; i < points.size(); ++i) {
        if (vertices[i].z > 0) { // Valid depth
            keyframe->point_cloud.emplace_back(
                vertices[i].x, vertices[i].y, vertices[i].z
            );
        }
    }
    
    // Extract color data
    extractColorData(colorFrame, keyframe);
    
    return keyframe;
}
```

### Surfel

Surface elements that represent the reconstructed 3D map.

```cpp
struct Surfel {
    Eigen::Vector3f position;       // 3D position
    Eigen::Vector3f normal;         // Surface normal
    uint8_t r, g, b;               // RGB color
    float radius = 0.f;            // Surface extent
    float confidence = 0.f;        // Confidence measure
    double last_update_time = 0.0; // Last update timestamp
    int update_count = 0;          // Number of updates
};
```

### VoxelIndex

Hash key for the sparse voxel grid.

```cpp
struct VoxelIndex {
    int x, y, z;
    
    bool operator==(const VoxelIndex& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

// Custom hash function
namespace std {
    template <> struct hash<VoxelIndex> {
        size_t operator()(const VoxelIndex& i) const {
            return ((hash<int>()(i.x) ^ (hash<int>()(i.y) << 1)) >> 1) 
                   ^ (hash<int>()(i.z) << 1);
        }
    };
}
```

## Integration

### RealSense Integration

The SLAM system integrates seamlessly with Intel RealSense cameras:

```cpp
// Setup RealSense pipeline
rs2::pipeline pipe;
rs2::config cfg;
cfg.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
cfg.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_RGB8, 30);
pipe.start(cfg);

// Process frames
rs2::frameset frames = pipe.wait_for_frames();
rs2::depth_frame depth = frames.get_depth_frame();
rs2::video_frame color = frames.get_color_frame();

// Generate point cloud
rs2::pointcloud pc;
rs2::points points = pc.calculate(depth);
pc.map_to(color);

// Send to SLAM system
slamManager->onPointCloudReady(points, color);
```

### Rendering Integration

SLAM data is automatically integrated with the rendering system:

```cpp
// SLAM manager automatically updates rendering system
connect(slamManager, &SlamManager::mapUpdated,
        [this]() {
    // Map visualization is automatically updated
    renderingSystem->invalidateSceneCache();
});

// Point clouds are rendered in real-time
connect(slamManager, &SlamManager::poseUpdatedForRender,
        renderingSystem, &RenderingSystem::updatePointCloud);
```

## Usage Examples

### Basic SLAM Setup

```cpp
#include "SlamManager.hpp"
#include "RenderingSystem.hpp"

class RoboticsApplication : public QMainWindow {
private:
    std::unique_ptr<SlamManager> m_slamManager;
    std::unique_ptr<RenderingSystem> m_renderingSystem;

public:
    void setupSLAM() {
        // Create SLAM manager
        m_slamManager = std::make_unique<SlamManager>(this);
        
        // Connect to rendering system
        m_slamManager->setRenderingSystem(m_renderingSystem.get());
        
        // Connect map updates
        connect(m_slamManager.get(), &SlamManager::mapUpdated,
                this, &RoboticsApplication::onMapUpdated);
        
        // Start SLAM processing
        m_slamManager->start();
    }
    
private slots:
    void onMapUpdated() {
        // Handle map updates (e.g., update UI, save map, etc.)
        statusBar()->showMessage("Map updated");
    }
};
```

### Custom Frontend Processing

```cpp
class CustomFrontend : public Frontend {
    Q_OBJECT

private:
    cv::Mat m_lastFrame;
    std::vector<cv::KeyPoint> m_lastKeypoints;

public slots:
    void processNewFrame(double timestamp, 
                        std::shared_ptr<rs2::points> points, 
                        std::shared_ptr<rs2::video_frame> colorFrame) override {
        // Custom tracking algorithm
        cv::Mat currentFrame = convertToOpenCV(*colorFrame);
        
        // Feature detection and matching
        std::vector<cv::KeyPoint> keypoints;
        cv::Mat descriptors;
        detectAndCompute(currentFrame, keypoints, descriptors);
        
        // Pose estimation
        Eigen::Isometry3f estimatedPose = estimatePose(keypoints, descriptors);
        
        // Create keyframe if needed
        if (shouldCreateKeyframe(estimatedPose)) {
            auto keyframe = createKeyFrame(*points, *colorFrame, 
                                         estimatedPose, timestamp);
            emit keyframeCreated(keyframe);
        }
        
        // Update rendering
        glm::mat4 renderPose = eigenToGlm(estimatedPose.matrix());
        emit poseUpdatedForRender(renderPose, points, colorFrame);
        
        // Store for next iteration
        m_lastFrame = currentFrame;
        m_lastKeypoints = keypoints;
    }
};
```

### Map Extraction and Export

```cpp
// Extract map data for analysis or export
void exportMap(const VoxelMap& map, const QString& filename) {
    std::vector<Surfel> surfels = map.getSurfels();
    
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream out(&file);
        
        // Write header
        out << quint32(surfels.size());
        
        // Write surfels
        for (const auto& surfel : surfels) {
            out << surfel.position.x() << surfel.position.y() << surfel.position.z();
            out << surfel.normal.x() << surfel.normal.y() << surfel.normal.z();
            out << surfel.r << surfel.g << surfel.b;
            out << surfel.radius << surfel.confidence;
        }
    }
}

// Load map data
std::vector<Surfel> loadMap(const QString& filename) {
    std::vector<Surfel> surfels;
    
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        QDataStream in(&file);
        
        quint32 count;
        in >> count;
        
        surfels.reserve(count);
        for (quint32 i = 0; i < count; ++i) {
            Surfel surfel;
            in >> surfel.position.x() >> surfel.position.y() >> surfel.position.z();
            in >> surfel.normal.x() >> surfel.normal.y() >> surfel.normal.z();
            in >> surfel.r >> surfel.g >> surfel.b;
            in >> surfel.radius >> surfel.confidence;
            surfels.push_back(surfel);
        }
    }
    
    return surfels;
}
```

## API Reference

### SlamManager Methods

#### Lifecycle
- `start()` - Start SLAM processing threads
- `stop()` - Stop and cleanup SLAM processing
- `setRenderingSystem(renderer)` - Connect to rendering system

#### Data Processing
- `onPointCloudReady(points, colorFrame)` - Process new sensor data
- `onPoseUpdatedForRender(pose, points, colorFrame)` - Handle pose updates

### Frontend Methods

- `processNewFrame(timestamp, points, colorFrame)` - Process new frame data

### Backend Methods

- `processNewKeyframe(keyframe)` - Process keyframe for mapping

### VoxelMap Methods

- `VoxelMap(voxel_size)` - Constructor with configurable resolution
- `fuse(keyframe)` - Integrate keyframe into map
- `getSurfels()` - Extract all surfels for rendering/analysis

### Performance Considerations

1. **Thread Safety**: All components are designed for multi-threaded operation
2. **Memory Efficiency**: Sparse voxel grid minimizes memory usage
3. **Real-time Performance**: Frontend/backend separation maintains frame rates
4. **Scalability**: Map size scales efficiently with scene complexity
5. **GPU Integration**: Seamless integration with OpenGL rendering

### Configuration Parameters

```cpp
// Voxel map configuration
const float VOXEL_SIZE = 0.01f;          // 1cm voxels
const float MAX_INTEGRATION_DISTANCE = 3.0f; // 3m max range
const int MIN_CONFIDENCE_THRESHOLD = 5;   // Minimum surfel confidence

// Frontend configuration  
const float KEYFRAME_DISTANCE_THRESHOLD = 0.1f; // 10cm movement
const float KEYFRAME_ANGLE_THRESHOLD = 0.17f;   // ~10 degrees rotation
const int MIN_FEATURES_PER_FRAME = 100;         // Minimum features for tracking

// Backend configuration
const int MAX_KEYFRAMES_IN_MEMORY = 100;  // Memory management
const float SURFEL_MERGE_THRESHOLD = 0.005f; // 5mm merge distance
```

---

*For complete examples and integration tutorials, see the [SLAM examples](examples/slam_example.md) and [getting started guide](getting_started.md).*