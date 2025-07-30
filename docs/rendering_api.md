# Rendering System API

The Rendering System provides a complete OpenGL 4.3-based rendering pipeline for 3D robotics visualization with multi-viewport support, extensible render passes, and efficient resource management.

## Table of Contents

1. [Overview](#overview)
2. [Core Classes](#core-classes)
3. [Render Passes](#render-passes)
4. [Shader Management](#shader-management)
5. [Multi-Viewport Support](#multi-viewport-support)
6. [Usage Examples](#usage-examples)
7. [API Reference](#api-reference)

## Overview

The rendering system is built around these key concepts:

- **RenderingSystem**: Central coordinator for all rendering operations
- **IRenderPass**: Extensible render pass interface for pipeline customization
- **Shader**: OpenGL shader program management and resource handling
- **ViewportWidget**: Individual 3D viewport with independent camera and rendering
- **TargetFBOs**: Framebuffer objects for post-processing and effects

### Key Features

- Multi-viewport rendering with independent cameras
- Extensible render pass pipeline
- Automatic resource management across OpenGL contexts
- Post-processing effects (glow, selection highlighting)
- Point cloud rendering with RealSense integration
- Performance monitoring and frame timing

## Core Classes

### RenderingSystem

The main rendering coordinator that manages the entire rendering pipeline.

```cpp
class RenderingSystem : public QObject {
    Q_OBJECT

public:
    explicit RenderingSystem(QObject* parent = nullptr);
    ~RenderingSystem();

    // Lifecycle & Core API
    void initializeResourcesForContext(QOpenGLFunctions_4_3_Core* gl, Scene* scene);
    void renderView(ViewportWidget* viewport, QOpenGLFunctions_4_3_Core* gl, 
                   int vpW, int vpH, float frameDeltaTime);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);
    void onViewportResized(ViewportWidget* vp, QOpenGLFunctions_4_3_Core* gl, 
                          int fbW, int fbH);

    // Scene Logic Updates
    void updateSceneLogic(float deltaTime);
    void updateCameraTransforms();
    void updatePointCloud(const rs2::points& points, 
                         const rs2::video_frame& colorFrame, 
                         const glm::mat4& pose);

    // Resource Access
    Scene& getScene() const { return *m_scene; }
    entt::entity getCurrentCameraEntity() const { return m_currentCamera; }
    Shader* getShader(const std::string& name);
    bool isContextInitialized(QOpenGLContext* ctx) const;

public slots:
    void onContextAboutToBeDestroyed();
};
```

#### Usage Example

```cpp
// Initialize the rendering system
RenderingSystem* renderer = new RenderingSystem(this);
Scene* scene = new Scene();
QOpenGLFunctions_4_3_Core* gl = context->versionFunctions<QOpenGLFunctions_4_3_Core>();

// Initialize resources for the current context
renderer->initializeResourcesForContext(gl, scene);

// Render a frame
ViewportWidget* viewport = getActiveViewport();
renderer->renderView(viewport, gl, width, height, deltaTime);
```

### TargetFBOs

Framebuffer objects for rendering targets and post-processing effects.

```cpp
struct TargetFBOs {
    int w = 0, h = 0;                           // Dimensions
    GLuint mainFBO = 0;                         // Main rendering target
    GLuint mainColorTexture = 0;                // Color buffer
    GLuint mainDepthTexture = 0;                // Depth buffer
    GLuint glowFBO = 0;                         // Glow effect buffer
    GLuint glowTexture = 0;                     // Glow texture
    GLuint pingpongFBO[2] = { 0, 0 };          // Blur ping-pong buffers
    GLuint pingpongTexture[2] = { 0, 0 };      // Blur textures
};
```

## Render Passes

The rendering pipeline is composed of extensible render passes that implement the `IRenderPass` interface.

### IRenderPass Interface

```cpp
class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    
    virtual void initialize(QOpenGLFunctions_4_3_Core* gl, 
                           RenderingSystem* renderingSystem) = 0;
    virtual void render(QOpenGLFunctions_4_3_Core* gl,
                       RenderingSystem* renderingSystem,
                       const glm::mat4& view,
                       const glm::mat4& projection,
                       int viewportWidth, int viewportHeight) = 0;
    virtual void shutdown(QOpenGLFunctions_4_3_Core* gl) = 0;
};
```

### Available Render Passes

#### OpaquePass
Renders solid, opaque geometry including robots, meshes, and static objects.

```cpp
class OpaquePass : public IRenderPass {
public:
    void initialize(QOpenGLFunctions_4_3_Core* gl, RenderingSystem* renderingSystem) override;
    void render(QOpenGLFunctions_4_3_Core* gl, RenderingSystem* renderingSystem,
               const glm::mat4& view, const glm::mat4& projection,
               int viewportWidth, int viewportHeight) override;
    void shutdown(QOpenGLFunctions_4_3_Core* gl) override;
};
```

#### PointCloudPass
Specialized pass for rendering point cloud data from sensors like RealSense cameras.

```cpp
class PointCloudPass : public IRenderPass {
public:
    void updatePointCloud(QOpenGLFunctions_4_3_Core* gl,
                         const rs2::points& points,
                         const rs2::video_frame& colorFrame);
    
    void setPose(const glm::mat4& pose) { m_pose = pose; }
    bool hasData() const { return m_hasValidData; }
};
```

#### GridPass
Renders customizable 3D grid with level-of-detail and fade effects.

```cpp
class GridPass : public IRenderPass {
public:
    void setGridProperties(float spacing, glm::vec3 color, float fadeDistance);
    void setViewDependentFading(bool enabled);
};
```

#### SelectionGlowPass
Implements object selection highlighting with glow effects.

```cpp
class SelectionGlowPass : public IRenderPass {
public:
    void setSelectedEntities(const std::vector<entt::entity>& entities);
    void setGlowColor(const glm::vec3& color);
    void setGlowIntensity(float intensity);
};
```

#### SplinePass
Renders spline curves and paths for trajectory visualization.

```cpp
class SplinePass : public IRenderPass {
public:
    void addSpline(const std::vector<glm::vec3>& controlPoints,
                   const glm::vec3& color,
                   float thickness = 2.0f);
    void clearSplines();
};
```

### Creating Custom Render Passes

```cpp
class CustomRenderPass : public IRenderPass {
private:
    Shader* m_shader = nullptr;
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;

public:
    void initialize(QOpenGLFunctions_4_3_Core* gl, RenderingSystem* renderingSystem) override {
        // Load custom shader
        m_shader = renderingSystem->getShader("custom_shader");
        
        // Create vertex array object
        gl->glGenVertexArrays(1, &m_VAO);
        gl->glGenBuffers(1, &m_VBO);
        
        // Setup vertex attributes...
    }
    
    void render(QOpenGLFunctions_4_3_Core* gl, RenderingSystem* renderingSystem,
               const glm::mat4& view, const glm::mat4& projection,
               int viewportWidth, int viewportHeight) override {
        // Custom rendering logic
        m_shader->use();
        m_shader->setMatrix4("u_view", view);
        m_shader->setMatrix4("u_projection", projection);
        
        gl->glBindVertexArray(m_VAO);
        // ... render custom geometry
    }
    
    void shutdown(QOpenGLFunctions_4_3_Core* gl) override {
        gl->glDeleteVertexArrays(1, &m_VAO);
        gl->glDeleteBuffers(1, &m_VBO);
    }
};
```

## Shader Management

The rendering system provides automatic shader loading, compilation, and management across multiple OpenGL contexts.

### Shader Class

```cpp
class Shader {
public:
    Shader();
    ~Shader();
    
    bool loadFromFiles(const QString& vertexPath, const QString& fragmentPath);
    bool loadFromFiles(const QString& vertexPath, const QString& geometryPath, 
                      const QString& fragmentPath);
    
    void use();
    bool isValid() const { return m_isValid; }
    GLuint getProgramId() const { return m_program; }
    
    // Uniform setters
    void setInt(const std::string& name, int value);
    void setFloat(const std::string& name, float value);
    void setVec3(const std::string& name, const glm::vec3& value);
    void setVec4(const std::string& name, const glm::vec4& value);
    void setMatrix4(const std::string& name, const glm::mat4& value);
    void setBool(const std::string& name, bool value);
};
```

### Shader Usage

```cpp
// Get shader from rendering system
Shader* shader = renderingSystem->getShader("phong_lighting");
if (shader && shader->isValid()) {
    shader->use();
    
    // Set uniforms
    shader->setMatrix4("u_model", modelMatrix);
    shader->setMatrix4("u_view", viewMatrix);
    shader->setMatrix4("u_projection", projectionMatrix);
    shader->setVec3("u_lightPos", lightPosition);
    shader->setVec3("u_viewPos", cameraPosition);
    
    // Render geometry...
}
```

### Standard Shader Locations

Shaders are automatically loaded from the `shaders/` directory:

```
shaders/
├── basic.vert              # Basic vertex shader
├── basic.frag              # Basic fragment shader
├── phong.vert             # Phong lighting vertex
├── phong.frag             # Phong lighting fragment
├── pointcloud.vert        # Point cloud vertex
├── pointcloud.frag        # Point cloud fragment
├── grid.vert              # Grid vertex shader
├── grid.frag              # Grid fragment shader
└── postprocess.vert       # Post-processing vertex
└── postprocess.frag       # Post-processing fragment
```

## Multi-Viewport Support

The rendering system supports multiple independent viewports, each with their own camera and rendering settings.

### ViewportWidget

```cpp
class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(Scene* scene, QWidget* parent = nullptr);
    
    // Camera control
    void setCameraEntity(entt::entity camera);
    entt::entity getCameraEntity() const { return m_cameraEntity; }
    
    // Viewport properties
    void setViewportName(const QString& name);
    QString getViewportName() const { return m_viewportName; }
    
    // Rendering control
    void setRenderingEnabled(bool enabled);
    bool isRenderingEnabled() const { return m_renderingEnabled; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    
    // Mouse interaction
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

signals:
    void cameraChanged();
    void viewportClicked(entt::entity entity);
};
```

### Multi-Viewport Setup

```cpp
// Create multiple viewports
ViewportWidget* mainViewport = new ViewportWidget(scene);
ViewportWidget* topViewport = new ViewportWidget(scene);
ViewportWidget* sideViewport = new ViewportWidget(scene);

// Create cameras for each viewport
entt::registry& registry = scene->getRegistry();

// Main perspective camera
entt::entity mainCamera = registry.create();
registry.emplace<CameraComponent>(mainCamera,
    glm::vec3(5, 5, 5),     // position
    glm::vec3(0, 0, 0),     // target
    45.0f,                  // fov
    CameraComponent::Type::Perspective
);

// Top orthographic camera
entt::entity topCamera = registry.create();
registry.emplace<CameraComponent>(topCamera,
    glm::vec3(0, 10, 0),    // position
    glm::vec3(0, 0, 0),     // target
    10.0f,                  // ortho size
    CameraComponent::Type::Orthographic
);

// Assign cameras to viewports
mainViewport->setCameraEntity(mainCamera);
topViewport->setCameraEntity(topCamera);
```

## Usage Examples

### Basic Rendering Setup

```cpp
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"

class MyRoboticsApp : public QMainWindow {
private:
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<RenderingSystem> m_renderingSystem;
    ViewportWidget* m_viewport;

public:
    MyRoboticsApp() {
        // Create core objects
        m_scene = std::make_unique<Scene>();
        m_renderingSystem = std::make_unique<RenderingSystem>(this);
        
        // Create viewport
        m_viewport = new ViewportWidget(m_scene.get(), this);
        setCentralWidget(m_viewport);
        
        // Setup camera
        entt::registry& registry = m_scene->getRegistry();
        entt::entity camera = registry.create();
        registry.emplace<CameraComponent>(camera,
            glm::vec3(0, 0, 5),  // position
            glm::vec3(0, 0, 0),  // target
            45.0f                // fov
        );
        m_viewport->setCameraEntity(camera);
    }
};
```

### Adding Render Passes

```cpp
// Add custom render pass to the pipeline
class MyRenderPass : public IRenderPass {
    // ... implementation
};

// Register the render pass (this is typically done internally)
m_renderingSystem->addRenderPass(std::make_unique<MyRenderPass>());
```

### Point Cloud Rendering

```cpp
// Setup point cloud rendering with RealSense
void setupPointCloudRendering() {
    // Point cloud data comes from SLAM system
    connect(m_slamManager, &SlamManager::newFrameData,
            this, [this](double timestamp, 
                         std::shared_ptr<rs2::points> points,
                         std::shared_ptr<rs2::video_frame> colorFrame) {
        // Update rendering system with new point cloud data
        glm::mat4 currentPose = getCurrentRobotPose();
        m_renderingSystem->updatePointCloud(*points, *colorFrame, currentPose);
    });
}
```

### Custom Shader Integration

```cpp
// Load and use custom shader
void useCustomShader() {
    // Shaders are automatically loaded from shaders/ directory
    Shader* customShader = m_renderingSystem->getShader("my_custom_shader");
    
    if (customShader && customShader->isValid()) {
        customShader->use();
        
        // Set custom uniforms
        customShader->setFloat("u_time", getCurrentTime());
        customShader->setVec3("u_robotColor", getRobotColor());
        
        // Render with custom shader...
    }
}
```

## API Reference

### RenderingSystem Methods

#### Core Lifecycle
- `initializeResourcesForContext(gl, scene)` - Initialize rendering resources
- `renderView(viewport, gl, width, height, deltaTime)` - Render single viewport
- `shutdown(gl)` - Cleanup resources
- `onViewportResized(viewport, gl, width, height)` - Handle viewport resize

#### Scene Updates
- `updateSceneLogic(deltaTime)` - Update scene animations and logic
- `updateCameraTransforms()` - Update camera matrices
- `updatePointCloud(points, colorFrame, pose)` - Update point cloud data

#### Resource Access
- `getScene()` - Get the managed scene
- `getCurrentCameraEntity()` - Get active camera entity
- `getShader(name)` - Get shader by name
- `isContextInitialized(context)` - Check if context is initialized

### Performance Considerations

1. **Multi-Context Support**: The rendering system efficiently manages resources across multiple OpenGL contexts
2. **Frame Timing**: Built-in performance monitoring tracks frame times and rendering performance
3. **Resource Caching**: Shaders and GPU resources are cached and reused
4. **Viewport Culling**: Only visible viewports are rendered
5. **Level-of-Detail**: Grid and other elements automatically adjust detail based on view distance

### Thread Safety

The rendering system is designed to work with Qt's OpenGL threading model:

- All OpenGL operations must be performed on the main thread
- Scene updates can be performed from background threads
- Resource creation/destruction is synchronized across contexts

---

*For more examples and advanced usage, see the [examples directory](examples/) and [tutorials](../examples/).*