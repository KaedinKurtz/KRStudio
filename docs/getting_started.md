# Getting Started Guide

Welcome to the Robotics Software development environment! This guide will walk you through setting up your development environment, building the software, and creating your first robotics application.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Installation](#installation)
3. [Building the Software](#building-the-software)
4. [First Steps](#first-steps)
5. [Your First Application](#your-first-application)
6. [Next Steps](#next-steps)
7. [Troubleshooting](#troubleshooting)

## Prerequisites

### System Requirements

- **Operating System**: Windows 10/11, Ubuntu 20.04+, macOS 10.15+
- **Compiler**: C++17 compatible compiler (GCC 9+, Clang 10+, MSVC 2019+)
- **CMake**: Version 3.24 or higher
- **Git**: For cloning repositories and submodules
- **GPU**: OpenGL 4.3+ compatible graphics card

### Required Dependencies

The software uses vcpkg for dependency management. The following packages are required:

#### Core Dependencies
- **Qt6**: GUI framework (Core, Widgets, OpenGL, OpenGLWidgets, SQL, Concurrent)
- **OpenGL**: Graphics rendering (4.3+)
- **GLM**: OpenGL Mathematics library
- **EnTT**: Entity-Component-System framework

#### 3D and Robotics
- **Assimp**: 3D model loading
- **URDF**: Robot description parsing
- **Eigen3**: Linear algebra library

#### Computer Vision and SLAM
- **OpenCV**: Computer vision (core, imgproc, features2d, calib3d)
- **RealSense SDK2**: Intel RealSense camera support

#### UI and Visualization
- **Qt Advanced Docking System**: Advanced docking widgets
- **QtNodes**: Node editor framework

## Installation

### 1. Clone the Repository

```bash
# Clone with submodules
git clone --recursive https://github.com/your-repo/RoboticsSoftware.git
cd RoboticsSoftware
```

If you forgot `--recursive`, initialize submodules:

```bash
git submodule update --init --recursive
```

### 2. Install vcpkg (if not already installed)

#### Windows
```powershell
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg

# Bootstrap vcpkg
.\bootstrap-vcpkg.bat

# Integrate with Visual Studio (optional)
.\vcpkg integrate install
```

#### Linux/macOS
```bash
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg

# Bootstrap vcpkg
./bootstrap-vcpkg.sh

# Add to PATH (add to your shell profile)
export PATH="$HOME/vcpkg:$PATH"
```

### 3. Install Dependencies

The project includes `vcpkg.json` for automatic dependency management:

```bash
# From the project root directory
vcpkg install
```

Alternatively, install manually:

```bash
# Core dependencies
vcpkg install qt6[core,widgets,opengl,sql,concurrent]
vcpkg install opengl
vcpkg install glm
vcpkg install entt

# 3D and robotics
vcpkg install assimp
vcpkg install urdfdom
vcpkg install eigen3

# Computer vision
vcpkg install opencv[core,imgproc,features2d,calib3d]
vcpkg install realsense2

# The project uses FetchContent for Qt Advanced Docking System and QtNodes
```

## Building the Software

### Configure with CMake

#### Using CMake GUI (Recommended for Windows)

1. Open CMake GUI
2. Set source directory to the cloned repository
3. Set build directory to `build/` inside the repository
4. Click "Configure"
5. Select your compiler/generator
6. Set `CMAKE_TOOLCHAIN_FILE` to your vcpkg toolchain:
   - Windows: `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
   - Linux/macOS: `~/vcpkg/scripts/buildsystems/vcpkg.cmake`
7. Click "Configure" again, then "Generate"

#### Using Command Line

```bash
# Create build directory
mkdir build && cd build

# Configure (adjust vcpkg path as needed)
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake \
         -DCMAKE_BUILD_TYPE=Release

# On Windows with vcpkg installed in C:\vcpkg:
# cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build the Project

```bash
# Build (from build directory)
cmake --build . --parallel

# Or for specific configuration on Windows
cmake --build . --config Release --parallel
```

### Run the Application

```bash
# From build directory
./RoboticsSoftware

# On Windows
.\Release\RoboticsSoftware.exe
```

## First Steps

When you first launch the application, you'll see:

1. **Main Window**: The central application window with toolbars and menus
2. **Viewport**: 3D rendering area for visualizing robots and scenes
3. **Properties Panel**: Object property editor (docked on the right)
4. **Node Catalog**: Available node types for visual programming (docked on the left)
5. **Database Panel**: Data management interface

### Basic Navigation

#### 3D Viewport Controls
- **Mouse Orbit**: Left-click and drag to orbit around objects
- **Pan**: Middle-click and drag (or Shift + left-click drag)
- **Zoom**: Mouse wheel or right-click and drag
- **Focus**: Double-click on an object to focus the camera on it

#### Camera Modes
- **Orbit Mode**: Default mode for inspecting objects
- **Fly Mode**: Free-flight navigation (press 'F' to toggle)
- **Projection**: Toggle between perspective and orthographic (press 'P')

### Loading Your First Robot

1. Click **File** → **Load Robot** (or use Ctrl+O)
2. Navigate to the `simple_arm.urdf` file in the project root
3. Click **Open**

You should now see a simple robot arm in the viewport!

## Your First Application

Let's create a simple application that loads a robot and allows basic interaction.

### 1. Create a New Project

Create a new directory for your project:

```bash
mkdir MyRobotApp
cd MyRobotApp
```

### 2. Create CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyRobotApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the robotics software installation
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL)
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)
find_package(entt REQUIRED)

# Include the robotics software headers
# Adjust this path to your RoboticsSoftware installation
include_directories(${CMAKE_SOURCE_DIR}/../RoboticsSoftware/include)

# Set Qt MOC
set(CMAKE_AUTOMOC ON)

# Create executable
add_executable(MyRobotApp
    main.cpp
    MyMainWindow.cpp
    MyMainWindow.hpp
)

# Link libraries
target_link_libraries(MyRobotApp
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    OpenGL::GL
    glm::glm
    EnTT::EnTT
)
```

### 3. Create MyMainWindow.hpp

```cpp
#pragma once

#include <QMainWindow>
#include <memory>

// Forward declarations
class Scene;
class RenderingSystem;
class ViewportWidget;
class QPushButton;

class MyMainWindow : public QMainWindow {
    Q_OBJECT

public:
    MyMainWindow(QWidget* parent = nullptr);
    ~MyMainWindow();

private slots:
    void loadRobot();
    void toggleAnimation();

private:
    void setupUI();
    void setupScene();

    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<RenderingSystem> m_renderingSystem;
    ViewportWidget* m_viewport;
    QPushButton* m_loadButton;
    QPushButton* m_animateButton;
    
    bool m_animationEnabled = false;
};
```

### 4. Create MyMainWindow.cpp

```cpp
#include "MyMainWindow.hpp"
#include "Scene.hpp"
#include "RenderingSystem.hpp"
#include "ViewportWidget.hpp"
#include "SceneBuilder.hpp"
#include "URDFParser.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QTimer>
#include <QStatusBar>
#include <QMessageBox>

MyMainWindow::MyMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupScene();
    setupUI();
    
    // Setup a timer for animation updates
    QTimer* updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, [this]() {
        if (m_animationEnabled && m_renderingSystem) {
            m_renderingSystem->updateSceneLogic(0.016f); // ~60 FPS
        }
    });
    updateTimer->start(16); // 60 FPS
}

MyMainWindow::~MyMainWindow() = default;

void MyMainWindow::setupScene() {
    // Create core objects
    m_scene = std::make_unique<Scene>();
    m_renderingSystem = std::make_unique<RenderingSystem>(this);
    
    // Create a default camera
    entt::registry& registry = m_scene->getRegistry();
    entt::entity camera = SceneBuilder::createCamera(registry, 
        glm::vec3(3, 3, 3), glm::vec3(1, 1, 0));
    
    statusBar()->showMessage("Scene initialized", 2000);
}

void MyMainWindow::setupUI() {
    // Create central widget with layout
    QWidget* centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    
    // Create toolbar
    QHBoxLayout* toolbarLayout = new QHBoxLayout;
    
    m_loadButton = new QPushButton("Load Robot");
    m_animateButton = new QPushButton("Start Animation");
    
    toolbarLayout->addWidget(m_loadButton);
    toolbarLayout->addWidget(m_animateButton);
    toolbarLayout->addStretch();
    
    // Create viewport
    m_viewport = new ViewportWidget(m_scene.get(), this);
    
    // Add to main layout
    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(m_viewport, 1); // Give viewport most space
    
    // Connect signals
    connect(m_loadButton, &QPushButton::clicked, this, &MyMainWindow::loadRobot);
    connect(m_animateButton, &QPushButton::clicked, this, &MyMainWindow::toggleAnimation);
    
    // Set window properties
    setWindowTitle("My Robot Application");
    resize(1200, 800);
}

void MyMainWindow::loadRobot() {
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Load Robot URDF",
        QString(),
        "URDF Files (*.urdf);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    try {
        // Parse URDF file
        RobotDescription robotDesc = URDFParser::parseFile(filename.toStdString());
        
        // Spawn robot in scene
        SceneBuilder::spawnRobot(*m_scene, robotDesc);
        
        // Focus camera on robot
        entt::registry& registry = m_scene->getRegistry();
        auto robotView = registry.view<RobotComponent>();
        if (!robotView.empty()) {
            entt::entity robotEntity = *robotView.begin();
            auto& transform = registry.get<TransformComponent>(robotEntity);
            
            // Get camera and focus on robot
            auto cameraView = registry.view<CameraComponent>();
            if (!cameraView.empty()) {
                entt::entity cameraEntity = *cameraView.begin();
                auto& cameraComp = registry.get<CameraComponent>(cameraEntity);
                cameraComp.camera.focusOn(transform.position, 5.0f);
            }
        }
        
        statusBar()->showMessage("Robot loaded: " + filename, 3000);
        
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error Loading Robot", 
                           QString("Failed to load robot: %1").arg(e.what()));
    }
}

void MyMainWindow::toggleAnimation() {
    m_animationEnabled = !m_animationEnabled;
    m_animateButton->setText(m_animationEnabled ? "Stop Animation" : "Start Animation");
    
    statusBar()->showMessage(
        m_animationEnabled ? "Animation started" : "Animation stopped", 
        2000
    );
}
```

### 5. Create main.cpp

```cpp
#include <QApplication>
#include "MyMainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("My Robot Application");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Your Organization");
    
    // Create and show main window
    MyMainWindow window;
    window.show();
    
    return app.exec();
}
```

### 6. Build and Run

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build . --parallel

# Run
./MyRobotApp
```

## Next Steps

Congratulations! You now have a basic robotics application. Here are some suggested next steps:

### 1. Explore the Examples

Check out the detailed examples in the documentation:
- [SLAM Integration Example](examples/slam_example.md)
- [Custom Node Development](examples/custom_node_example.md) 
- [Multi-Viewport Setup](examples/multi_viewport_example.md)
- [Robot Simulation](examples/robot_simulation_example.md)

### 2. Add More Features

Try extending your application with:

#### SLAM Integration
```cpp
// Add SLAM manager to your application
#include "SlamManager.hpp"

// In your constructor:
m_slamManager = std::make_unique<SlamManager>(this);
m_slamManager->setRenderingSystem(m_renderingSystem.get());
m_slamManager->start();
```

#### Multiple Viewports
```cpp
// Create additional viewports for different views
ViewportWidget* topView = new ViewportWidget(m_scene.get(), this);
ViewportWidget* sideView = new ViewportWidget(m_scene.get(), this);

// Create cameras for each view
entt::entity topCamera = SceneBuilder::createCamera(registry, 
    glm::vec3(0, 10, 0), glm::vec3(0, 1, 1));
topView->setCameraEntity(topCamera);
```

#### Node-Based Programming
```cpp
#include "NodeFactory.hpp"
#include "ArithmeticAndMathNodes.hpp"

// Create a node-based computation graph
NodeFactory factory;
auto addNode = factory.createNode("AddNode");
auto multiplyNode = factory.createNode("MultiplyNode");

// Connect nodes and process data
```

### 3. Learn the APIs

Dive deeper into the comprehensive API documentation:
- [Rendering System API](rendering_api.md) - Advanced graphics and visualization
- [SLAM API](slam_api.md) - Localization and mapping
- [Scene & Object Management API](scene_api.md) - 3D scene management
- [Node Programming API](node_api.md) - Visual programming
- [Database API](database_api.md) - Data persistence

### 4. Join the Community

- Read the full [API Documentation](API_DOCUMENTATION.md)
- Check out advanced [Examples and Tutorials](examples/)
- Contribute to the project on GitHub

## Troubleshooting

### Common Build Issues

#### CMake Can't Find vcpkg
```bash
# Make sure to specify the toolchain file
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

#### Qt6 Not Found
```bash
# Install Qt6 via vcpkg
vcpkg install qt6[core,widgets,opengl,sql,concurrent]

# Or set Qt6_DIR manually
cmake .. -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6
```

#### RealSense SDK Issues
```bash
# Install RealSense SDK
vcpkg install realsense2

# Or disable RealSense features
cmake .. -DENABLE_REALSENSE=OFF
```

#### OpenGL Version Issues
Make sure your graphics drivers are up to date and support OpenGL 4.3+.

### Runtime Issues

#### Application Won't Start
- Check that all shared libraries are in your PATH
- Verify OpenGL drivers are installed and updated
- Try running from the build directory

#### Rendering Issues
- Update graphics drivers
- Check OpenGL version with `glxinfo` (Linux) or GPU-Z (Windows)
- Verify viewport size is not zero

#### Robot Loading Fails
- Check URDF file syntax
- Verify mesh file paths in URDF are correct
- Ensure mesh files exist and are accessible

### Getting Help

If you encounter issues:

1. Check the [troubleshooting section](#troubleshooting) above
2. Search existing GitHub issues
3. Create a detailed bug report with:
   - Operating system and version
   - Compiler and version
   - CMake output
   - Error messages
   - Steps to reproduce

### Performance Tips

1. **Build in Release mode** for better performance:
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

2. **Enable parallel builds**:
   ```bash
   cmake --build . --parallel
   ```

3. **Use appropriate GPU drivers** and ensure OpenGL 4.3+ support

4. **Monitor resource usage** - the software can be memory intensive with large point clouds

---

*Welcome to robotics software development! For more advanced topics and examples, continue with the [API documentation](API_DOCUMENTATION.md) and [examples](examples/).*