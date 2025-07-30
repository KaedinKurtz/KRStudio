# Robotics Software - API Documentation

A comprehensive C++ robotics workstation software built with Qt6 and OpenGL, featuring simulation, SLAM, node-based programming, and advanced visualization capabilities.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Quick Start](#quick-start)
4. [Core APIs](#core-apis)
5. [Component Systems](#component-systems)
6. [Examples & Tutorials](#examples--tutorials)
7. [Build & Development](#build--development)

## Overview

This robotics software provides a fully integrated development environment for robotics applications, featuring:

- **Real-time 3D Rendering**: OpenGL-based rendering pipeline with multiple viewport support
- **SLAM (Simultaneous Localization and Mapping)**: Complete SLAM system with RealSense support
- **Node-based Programming**: Visual programming interface for robotics algorithms
- **Scene Management**: Entity-Component-System (ECS) architecture for scene objects
- **Database Integration**: Comprehensive data management and persistence
- **UI Framework**: Modern Qt6-based interface with advanced docking

## Architecture

The software follows a modular architecture with clear separation of concerns:

```
├── Rendering System          # OpenGL rendering pipeline
├── SLAM System              # Localization and mapping
├── Scene Management         # ECS-based object management  
├── Node Programming         # Visual programming framework
├── UI Framework             # Qt6-based user interface
├── Database Layer           # Data persistence and management
└── Utility Systems          # Cross-cutting concerns
```

## Quick Start

### Basic Scene Setup

```cpp
#include "Scene.hpp"
#include "RenderingSystem.hpp"
#include "MainWindow.hpp"

// Create and initialize the main application
MainWindow window;
Scene* scene = window.getScene();
RenderingSystem* renderer = window.getRenderingSystem();

// Initialize rendering context
QOpenGLFunctions_4_3_Core* gl = // ... get OpenGL context
renderer->initializeResourcesForContext(gl, scene);

// Add a camera to the scene
entt::registry& registry = scene->getRegistry();
entt::entity camera = registry.create();
registry.emplace<CameraComponent>(camera, 
    glm::vec3(0, 0, 5),  // position
    glm::vec3(0, 0, 0),  // target
    45.0f                // fov
);
```

### Loading and Displaying a Robot

```cpp
#include "Robot.hpp"
#include "SceneBuilder.hpp"

// Load URDF robot description
std::string urdfPath = "path/to/robot.urdf";
SceneBuilder builder;
entt::entity robotEntity = builder.loadRobotFromURDF(urdfPath, registry);

// Set robot pose
registry.emplace<TransformComponent>(robotEntity,
    glm::vec3(0, 0, 0),                    // position
    glm::quat(1, 0, 0, 0),                 // rotation
    glm::vec3(1, 1, 1)                     // scale
);
```

## Core APIs

### [Rendering System API](docs/rendering_api.md)
- `RenderingSystem` - Main rendering coordination
- `IRenderPass` - Extensible render pass interface  
- `Shader` - Shader program management
- `ViewportWidget` - Multi-viewport rendering

### [SLAM API](docs/slam_api.md)
- `SlamManager` - SLAM system coordination
- `Frontend` - Visual-inertial frontend processing
- `Backend` - Map optimization and loop closure
- `VoxelMap` - 3D occupancy mapping

### [Scene & Object Management API](docs/scene_api.md)
- `Scene` - ECS registry container
- `Robot` - Robot model representation
- `Mesh` - 3D geometry handling
- `Camera` - Virtual camera management

### [UI Components API](docs/ui_api.md)
- `MainWindow` - Application main window
- `ViewportWidget` - 3D viewport rendering
- `PropertiesPanel` - Object property editing
- `NodeCatalogWidget` - Node library browser

### [Node Programming API](docs/node_api.md)
- `Node` - Base node class for all algorithms
- `NodeFactory` - Dynamic node creation
- `ArithmeticAndMathNodes` - Mathematical operations
- `ControlSystemsNodes` - Control theory algorithms
- `SensorNodes` - Sensor data processing

### [Database API](docs/database_api.md)
- `DatabaseManager` - Core database operations
- `DatabaseQueryOptimizer` - Query performance optimization
- `DatabaseBackupManager` - Backup and restore operations
- `DatabaseMigrationManager` - Schema versioning

### [Utility APIs](docs/utility_api.md)
- `URDFParser` - URDF file parsing
- `DataConversion` - Data type conversions
- `MeshUtils` - 3D mesh processing utilities
- `AppPaths` - Application path management

## Component Systems

### [ECS Components](docs/components_api.md)
The software uses an Entity-Component-System architecture with rich component types:

#### Core Components
- `TransformComponent` - Position, rotation, scale
- `MeshComponent` - 3D geometry data
- `MaterialComponent` - Surface appearance properties
- `CameraComponent` - Virtual camera parameters

#### Rendering Components  
- `RenderResourceComponent` - GPU buffer management
- `LightComponent` - Light source parameters
- `PulsingLightComponent` - Animated lighting effects

#### Physics Components
- `RigidBodyComponent` - Physics simulation
- `ColliderComponent` - Collision detection
- `JointComponent` - Kinematic joints

#### Robot Components
- `RobotComponent` - Robot-specific data
- `JointStateComponent` - Joint positions/velocities
- `LinkComponent` - Robot link properties

## Examples & Tutorials

### [Getting Started Guide](docs/getting_started.md)
Step-by-step tutorial for building your first robotics application.

### [SLAM Integration Example](docs/examples/slam_example.md)
Complete example showing how to integrate RealSense camera data with the SLAM system.

### [Custom Node Development](docs/examples/custom_node_example.md)
Tutorial for creating custom nodes for the visual programming system.

### [Multi-Viewport Setup](docs/examples/multi_viewport_example.md)
Setting up multiple synchronized 3D viewports for complex visualization.

### [Robot Simulation](docs/examples/robot_simulation_example.md)
Complete robot simulation setup with physics and control.

## Build & Development

### Dependencies
- Qt6 (Core, GUI, Widgets, OpenGL, OpenGLWidgets, SQL)
- OpenGL 4.3+
- GLM (OpenGL Mathematics)
- Assimp (3D model loading)
- URDF (Robot description parsing)
- RealSense SDK2 (camera support)
- OpenCV (computer vision)
- EnTT (Entity-Component-System)
- Eigen3 (linear algebra)

### Build Instructions

```bash
# Clone with submodules
git clone --recursive <repository-url>
cd RoboticsSoftware

# Configure with CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run
./build/RoboticsSoftware
```

### Development Setup

```bash
# Install dependencies via vcpkg
vcpkg install qt6 assimp urdfdom realsense2 opencv glm eigen3

# Configure for development
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## API Reference Index

- **Rendering**: [RenderingSystem](docs/api/RenderingSystem.md), [Shader](docs/api/Shader.md), [IRenderPass](docs/api/IRenderPass.md)
- **SLAM**: [SlamManager](docs/api/SlamManager.md), [Frontend](docs/api/Frontend.md), [Backend](docs/api/Backend.md)
- **Scene**: [Scene](docs/api/Scene.md), [Robot](docs/api/Robot.md), [Camera](docs/api/Camera.md)
- **UI**: [MainWindow](docs/api/MainWindow.md), [ViewportWidget](docs/api/ViewportWidget.md)
- **Nodes**: [Node](docs/api/Node.md), [NodeFactory](docs/api/NodeFactory.md)
- **Database**: [DatabaseManager](docs/api/DatabaseManager.md)
- **Components**: [TransformComponent](docs/api/TransformComponent.md), [MeshComponent](docs/api/MeshComponent.md)

---

*For detailed API documentation, examples, and tutorials, see the linked documents above.*