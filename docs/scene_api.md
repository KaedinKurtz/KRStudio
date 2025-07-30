# Scene & Object Management API

The Scene and Object Management system provides a comprehensive Entity-Component-System (ECS) architecture for managing 3D scenes, robots, cameras, meshes, and other objects in the robotics workstation. Built on EnTT, it offers efficient and flexible object management with support for complex hierarchical relationships.

## Table of Contents

1. [Overview](#overview)
2. [Core Classes](#core-classes)
3. [Entity-Component-System](#entity-component-system)
4. [Robot Management](#robot-management)
5. [Camera System](#camera-system)
6. [Mesh Management](#mesh-management)
7. [Scene Building](#scene-building)
8. [Usage Examples](#usage-examples)
9. [API Reference](#api-reference)

## Overview

The scene management system is built around these key concepts:

- **Scene**: ECS registry container managing all entities and components
- **Robot**: Complete robot model with URDF support and kinematic chains  
- **Camera**: Advanced camera system with orbit/fly modes and multiple projections
- **Mesh**: 3D geometry management with efficient GPU resource handling
- **SceneBuilder**: Factory utilities for creating and populating scenes

### Key Features

- Entity-Component-System architecture for maximum flexibility
- URDF robot loading and kinematic simulation
- Advanced camera controls (orbit, fly, pan, dolly)
- Efficient mesh management with GPU resource optimization
- Hierarchical object relationships and transformations
- Material and lighting support
- Automatic resource management and cleanup

## Core Classes

### Scene

The central ECS registry container that manages all entities and their components.

```cpp
class Scene {
public:
    Scene();
    ~Scene();

    // Registry access
    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

private:
    entt::registry m_registry;
};
```

#### Usage Example

```cpp
// Create a scene
std::unique_ptr<Scene> scene = std::make_unique<Scene>();
entt::registry& registry = scene->getRegistry();

// Create entities
entt::entity robotEntity = registry.create();
entt::entity cameraEntity = registry.create();

// Add components
registry.emplace<TransformComponent>(robotEntity, 
    glm::vec3(0, 0, 0),       // position
    glm::quat(1, 0, 0, 0),    // rotation
    glm::vec3(1, 1, 1)        // scale
);
```

### Robot

Complete robot representation with URDF support, kinematic chains, and animation capabilities.

```cpp
class Robot {
public:
    Robot(const std::string& urdf_path);
    
    // Update and rendering
    void update(double deltaTime);
    void draw(Shader& shader, Mesh& link_mesh) const;
    
    // Joint control
    std::map<std::string, double> getJointStates() const;

private:
    struct Link {
        std::string name;
        std::vector<std::shared_ptr<Link>> child_links;
        std::vector<std::shared_ptr<Joint>> child_joints;
    };

    struct Joint {
        std::string name;
        glm::vec3 origin_xyz;
        glm::vec3 axis;
        double current_angle_rad = 0.0;
    };

    std::shared_ptr<Link> rootLink;
    std::map<std::string, std::shared_ptr<Link>> linkMap;
    std::map<std::string, std::shared_ptr<Joint>> jointMap;
    std::string modelName;
    double m_totalTime = 0.0;
};
```

#### Robot Features

- **URDF Loading**: Complete URDF parser supporting links, joints, and materials
- **Kinematic Chains**: Hierarchical link/joint structure with forward kinematics
- **Joint Animation**: Time-based joint movement and state tracking
- **Efficient Rendering**: Optimized drawing with shared mesh resources
- **State Management**: Complete joint state access and modification

#### Usage Example

```cpp
// Load robot from URDF
Robot robot("path/to/robot.urdf");

// Update robot animation
robot.update(deltaTime);

// Get joint states
std::map<std::string, double> jointStates = robot.getJointStates();
for (const auto& [jointName, angle] : jointStates) {
    std::cout << jointName << ": " << angle << " rad" << std::endl;
}

// Render robot
Shader* robotShader = renderingSystem->getShader("robot_shader");
Mesh cubeMesh = Mesh::getLitCubeVertices();
robot.draw(*robotShader, cubeMesh);
```

### Camera

Advanced camera system with multiple navigation modes and projection types.

```cpp
class Camera {
public:
    enum Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };
    enum class NavMode { ORBIT, FLY };

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f));

    // Core functions
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Navigation modes
    void setNavMode(NavMode mode) { m_Mode = mode; }
    NavMode navMode() const { return m_Mode; }

    // Camera controls
    void orbit(float xoffset, float yoffset);
    void pan(float xoffset, float yoffset);
    void dolly(float yoffset);
    void move(Camera_Movement direction, float deltaTime);
    void focusOn(const glm::vec3& target, float distance = 5.0f);

    // Utility functions
    void toggleProjection();
    void resetView(float aspectRatio, const glm::vec3& target = glm::vec3(0.0f), 
                   float objectSize = 1.0f);
    void defaultInitialView();

    // Getters
    glm::vec3 getPosition() const { return m_Position; }
    glm::vec3 getFocalPoint() const { return m_FocalPoint; }
    float getDistance() const { return m_Distance; }
    bool isPerspective() const { return m_IsPerspective; }
};
```

#### Camera Features

- **Dual Navigation Modes**: Orbit mode for object inspection, fly mode for free navigation
- **Projection Switching**: Dynamic switching between perspective and orthographic
- **Advanced Controls**: Orbit, pan, dolly, and focus operations
- **Smooth Movement**: Built-in smoothing for fluid camera transitions
- **Flexible Targeting**: Focus on specific objects or points in space

#### Usage Example

```cpp
// Create camera entity with component
entt::entity cameraEntity = registry.create();
registry.emplace<CameraComponent>(cameraEntity,
    glm::vec3(5, 5, 5),      // position
    glm::vec3(0, 0, 0),      // target
    45.0f,                   // fov
    CameraComponent::Type::Perspective
);

// Get camera for direct manipulation
Camera& camera = registry.get<CameraComponent>(cameraEntity).camera;

// Set navigation mode
camera.setNavMode(Camera::NavMode::ORBIT);

// Focus on robot
glm::vec3 robotPosition = getRobotPosition();
camera.focusOn(robotPosition, 3.0f);

// Handle mouse input for camera control
void handleMouseMove(float deltaX, float deltaY) {
    if (camera.navMode() == Camera::NavMode::ORBIT) {
        camera.orbit(deltaX, deltaY);
    }
}
```

### Mesh

Efficient 3D geometry management with GPU resource optimization.

```cpp
class Mesh {
public:
    Mesh(const std::vector<float>& vertices);
    Mesh(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);

    // Data access
    const std::vector<float>& vertices() const { return m_vertices; }
    const std::vector<unsigned int>& indices() const { return m_indices; }
    bool hasIndices() const { return !m_indices.empty(); }

    // Standard meshes
    static const std::vector<float>& getLitCubeVertices();
    static const std::vector<unsigned int>& getLitCubeIndices();

private:
    std::vector<float> m_vertices;
    std::vector<unsigned int> m_indices;
};
```

#### Mesh Features

- **Flexible Construction**: Support for indexed and non-indexed geometry
- **Standard Primitives**: Built-in cube, sphere, and other common shapes
- **GPU Integration**: Automatic GPU buffer management through ECS components
- **Memory Efficiency**: Shared vertex data and intelligent resource reuse

#### Usage Example

```cpp
// Create custom mesh
std::vector<float> vertices = {
    // positions       // normals        // texcoords
    -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
     0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
     0.0f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f
};
Mesh triangleMesh(vertices);

// Create entity with mesh
entt::entity meshEntity = registry.create();
registry.emplace<MeshComponent>(meshEntity, triangleMesh);
registry.emplace<TransformComponent>(meshEntity, 
    glm::vec3(0, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1, 1, 1));

// Use standard cube mesh
Mesh cubeMesh(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());
```

## Entity-Component-System

The scene uses an ECS architecture where entities are IDs, components store data, and systems process entities with specific component combinations.

### Core Components

#### TransformComponent
```cpp
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    
    glm::mat4 getMatrix() const;
    void setFromMatrix(const glm::mat4& matrix);
};
```

#### MeshComponent
```cpp
struct MeshComponent {
    Mesh mesh;
    bool visible = true;
    
    MeshComponent(const Mesh& m) : mesh(m) {}
};
```

#### CameraComponent
```cpp
struct CameraComponent {
    enum class Type { Perspective, Orthographic };
    
    Camera camera;
    Type type = Type::Perspective;
    float fov = 45.0f;        // For perspective
    float orthoSize = 10.0f;  // For orthographic
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
};
```

#### MaterialComponent
```cpp
struct MaterialComponent {
    glm::vec4 albedoColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metalness = 0.0f;
    float roughness = 0.5f;
    glm::vec3 emissiveColor{0.0f};
    float emissiveIntensity = 1.0f;
    
    std::string albedoTexturePath;
    std::string normalTexturePath;
    std::string metallicRoughnessTexturePath;
};
```

### System Examples

```cpp
// Transform update system
void updateTransforms(entt::registry& registry, float deltaTime) {
    auto view = registry.view<TransformComponent, VelocityComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& velocity = view.get<VelocityComponent>(entity);
        
        transform.position += velocity.linear * deltaTime;
        // Apply angular velocity...
    }
}

// Rendering system
void renderMeshes(entt::registry& registry, const glm::mat4& view, 
                  const glm::mat4& projection) {
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& mesh = view.get<MeshComponent>(entity);
        auto& material = view.get<MaterialComponent>(entity);
        
        if (mesh.visible) {
            renderMesh(mesh, transform.getMatrix(), view, projection, material);
        }
    }
}
```

## Robot Management

### RobotDescription Structure

```cpp
struct RobotDescription {
    std::string name;
    std::vector<LinkDescription> links;
    std::vector<JointDescription> joints;
    MaterialDescription defaultMaterial;
};

struct LinkDescription {
    std::string name;
    uint64_t persistent_id;
    
    // Visual properties
    glm::vec3 visual_origin_xyz;
    glm::vec3 visual_origin_rpy;
    glm::vec3 visual_size;
    std::string visual_mesh_path;
    MaterialDescription material;
    
    // Collision properties
    glm::vec3 collision_origin_xyz;
    glm::vec3 collision_origin_rpy;
    glm::vec3 collision_size;
    
    // Inertial properties
    float mass;
    glm::vec3 inertia_origin_xyz;
    glm::mat3 inertia_tensor;
};

struct JointDescription {
    std::string name;
    std::string type;
    std::string parent_link;
    std::string child_link;
    
    glm::vec3 origin_xyz;
    glm::vec3 origin_rpy;
    glm::vec3 axis;
    
    // Joint limits
    float lower_limit;
    float upper_limit;
    float velocity_limit;
    float effort_limit;
};
```

### Robot Components

```cpp
struct RobotComponent {
    std::string name;
    std::string urdfPath;
    std::map<std::string, float> jointStates;
    bool isActive = true;
};

struct JointComponent {
    std::string name;
    std::string type;
    glm::vec3 axis;
    float currentValue = 0.0f;
    float minValue = -M_PI;
    float maxValue = M_PI;
    float velocity = 0.0f;
    float effort = 0.0f;
};

struct LinkComponent {
    std::string name;
    entt::entity parentJoint = entt::null;
    std::vector<entt::entity> childJoints;
    MaterialDescription material;
};
```

## Scene Building

### SceneBuilder Utility

Factory class for creating and populating scenes with common objects.

```cpp
class SceneBuilder {
public:
    // Robot creation
    static void spawnRobot(Scene& scene, const RobotDescription& description);
    
    // Camera creation
    static entt::entity createCamera(entt::registry& registry,
                                   const glm::vec3& position,
                                   const glm::vec3& color = {1,1,0});
    
    // Spline creation
    static entt::entity makeCR(entt::registry& registry,
                             const std::vector<glm::vec3>& controlPoints,
                             const glm::vec4& coreColor,
                             const glm::vec4& glowColor,
                             float glowThickness);
    
    static entt::entity makeLinear(entt::registry& registry,
                                 const std::vector<glm::vec3>& controlPoints,
                                 const glm::vec4& coreColor,
                                 const glm::vec4& glowColor,
                                 float glowThickness);
    
    static entt::entity makeBezier(entt::registry& registry,
                                 const std::vector<glm::vec3>& controlPoints,
                                 const glm::vec4& coreColor,
                                 const glm::vec4& glowColor,
                                 float glowThickness);
    
    // Parametric curves
    static entt::entity makeParam(entt::registry& registry,
                                std::function<glm::vec3(float)> function,
                                const glm::vec4& coreColor,
                                const glm::vec4& glowColor,
                                float glowThickness);
};
```

#### Usage Examples

```cpp
// Create a complete scene
Scene scene;
entt::registry& registry = scene.getRegistry();

// Load and spawn robot
RobotDescription robotDesc = URDFParser::parseFile("robot.urdf");
SceneBuilder::spawnRobot(scene, robotDesc);

// Create cameras
entt::entity mainCamera = SceneBuilder::createCamera(registry, 
    glm::vec3(5, 5, 5), glm::vec3(1, 1, 0));
entt::entity topCamera = SceneBuilder::createCamera(registry, 
    glm::vec3(0, 10, 0), glm::vec3(0, 1, 1));

// Create trajectory splines
std::vector<glm::vec3> waypoints = {
    {0, 0, 0}, {1, 2, 1}, {3, 1, 2}, {4, 0, 0}
};
entt::entity trajectory = SceneBuilder::makeBezier(registry, waypoints,
    glm::vec4(1, 0, 0, 1),    // red core
    glm::vec4(1, 0.5, 0, 0.5), // orange glow
    2.0f                       // glow thickness
);
```

## Usage Examples

### Complete Scene Setup

```cpp
#include "Scene.hpp"
#include "SceneBuilder.hpp"
#include "URDFParser.hpp"

class RoboticsScene {
private:
    std::unique_ptr<Scene> m_scene;
    entt::entity m_robotEntity;
    entt::entity m_cameraEntity;

public:
    void setupScene() {
        // Create scene
        m_scene = std::make_unique<Scene>();
        entt::registry& registry = m_scene->getRegistry();
        
        // Load robot
        RobotDescription robotDesc = URDFParser::parseFile("assets/robot.urdf");
        SceneBuilder::spawnRobot(*m_scene, robotDesc);
        
        // Find the robot entity (could be tracked during spawn)
        auto robotView = registry.view<RobotComponent>();
        m_robotEntity = *robotView.begin();
        
        // Create main camera
        m_cameraEntity = SceneBuilder::createCamera(registry, 
            glm::vec3(3, 3, 3), glm::vec3(1, 1, 0));
        
        // Focus camera on robot
        auto& cameraComp = registry.get<CameraComponent>(m_cameraEntity);
        auto& robotTransform = registry.get<TransformComponent>(m_robotEntity);
        cameraComp.camera.focusOn(robotTransform.position, 5.0f);
        
        // Add lighting
        createLighting(registry);
        
        // Add ground plane
        createGroundPlane(registry);
    }
    
private:
    void createLighting(entt::registry& registry) {
        // Create directional light
        entt::entity light = registry.create();
        registry.emplace<TransformComponent>(light,
            glm::vec3(5, 10, 5), glm::quat(1, 0, 0, 0), glm::vec3(1));
        registry.emplace<LightComponent>(light,
            LightComponent::Type::Directional,
            glm::vec3(1.0f, 1.0f, 0.9f),  // warm white
            3.0f                           // intensity
        );
    }
    
    void createGroundPlane(entt::registry& registry) {
        // Create large plane for ground
        std::vector<float> planeVertices = createPlaneVertices(20.0f);
        Mesh planeMesh(planeVertices);
        
        entt::entity ground = registry.create();
        registry.emplace<TransformComponent>(ground,
            glm::vec3(0, -1, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
        registry.emplace<MeshComponent>(ground, planeMesh);
        registry.emplace<MaterialComponent>(ground,
            glm::vec4(0.6f, 0.6f, 0.6f, 1.0f), // gray
            0.0f,  // non-metallic
            0.8f   // rough
        );
    }
};
```

### Robot Joint Control

```cpp
// Control robot joints
void controlRobotJoints(entt::registry& registry, entt::entity robotEntity) {
    auto& robotComp = registry.get<RobotComponent>(robotEntity);
    
    // Set specific joint angles
    robotComp.jointStates["shoulder_pan_joint"] = M_PI / 4;  // 45 degrees
    robotComp.jointStates["shoulder_lift_joint"] = -M_PI / 6; // -30 degrees
    robotComp.jointStates["elbow_joint"] = M_PI / 3;         // 60 degrees
    
    // Update robot with new joint states
    updateRobotKinematics(registry, robotEntity);
}

void updateRobotKinematics(entt::registry& registry, entt::entity robotEntity) {
    // Find all joint entities for this robot
    auto jointView = registry.view<JointComponent, TransformComponent>();
    
    for (auto jointEntity : jointView) {
        auto& joint = jointView.get<JointComponent>(jointEntity);
        auto& transform = jointView.get<TransformComponent>(jointEntity);
        
        // Update transform based on joint value
        float angle = joint.currentValue;
        glm::quat rotation = glm::angleAxis(angle, joint.axis);
        transform.rotation = rotation;
    }
}
```

### Dynamic Scene Modification

```cpp
// Add objects dynamically to the scene
void addDynamicObjects(Scene& scene) {
    entt::registry& registry = scene.getRegistry();
    
    // Add moving obstacles
    for (int i = 0; i < 5; ++i) {
        entt::entity obstacle = registry.create();
        
        // Random position
        glm::vec3 position(
            (rand() % 20) - 10,  // -10 to 10
            1.0f,
            (rand() % 20) - 10
        );
        
        registry.emplace<TransformComponent>(obstacle,
            position, glm::quat(1, 0, 0, 0), glm::vec3(0.5f));
        
        // Cube mesh
        Mesh cubeMesh(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());
        registry.emplace<MeshComponent>(obstacle, cubeMesh);
        
        // Random material
        glm::vec4 color(
            float(rand()) / RAND_MAX,
            float(rand()) / RAND_MAX,
            float(rand()) / RAND_MAX,
            1.0f
        );
        registry.emplace<MaterialComponent>(obstacle, color, 0.1f, 0.3f);
        
        // Add movement
        glm::vec3 velocity(
            (float(rand()) / RAND_MAX - 0.5f) * 2.0f,
            0.0f,
            (float(rand()) / RAND_MAX - 0.5f) * 2.0f
        );
        registry.emplace<VelocityComponent>(obstacle, velocity, glm::vec3(0));
    }
}
```

## API Reference

### Scene Methods

- `getRegistry()` - Access to the ECS registry
- Entity and component management through EnTT registry

### Robot Methods

- `Robot(urdf_path)` - Load robot from URDF file
- `update(deltaTime)` - Update robot animation and kinematics
- `draw(shader, mesh)` - Render robot with specified shader and mesh
- `getJointStates()` - Get current joint positions

### Camera Methods

#### Navigation
- `orbit(xoffset, yoffset)` - Orbit around focal point
- `pan(xoffset, yoffset)` - Pan camera view
- `dolly(yoffset)` - Move closer/farther from focal point
- `move(direction, deltaTime)` - Free-flight movement

#### Configuration
- `setNavMode(mode)` - Set ORBIT or FLY navigation mode
- `toggleProjection()` - Switch between perspective/orthographic
- `focusOn(target, distance)` - Focus camera on specific point
- `resetView(aspectRatio, target, objectSize)` - Reset to default view

#### State Access
- `getPosition()` - Get camera world position
- `getFocalPoint()` - Get camera focal point
- `getDistance()` - Get distance from focal point
- `getViewMatrix()` - Get view transformation matrix
- `getProjectionMatrix(aspectRatio)` - Get projection matrix

### SceneBuilder Methods

- `spawnRobot(scene, description)` - Create robot entities from description
- `createCamera(registry, position, color)` - Create camera entity
- `makeBezier/makeCR/makeLinear(...)` - Create spline entities
- `makeParam(registry, function, ...)` - Create parametric curve entity

### Performance Considerations

1. **ECS Efficiency**: Entity-component systems provide cache-friendly data access
2. **Memory Management**: Automatic component lifecycle management
3. **Batch Operations**: Process entities in groups for optimal performance
4. **GPU Resources**: Efficient sharing and management of GPU buffers
5. **Hierarchical Updates**: Optimized parent-child relationship updates

### Best Practices

1. **Component Design**: Keep components data-only, logic in systems
2. **Entity Relationships**: Use entity references for parent-child relationships
3. **Resource Sharing**: Share meshes and materials between entities
4. **Update Ordering**: Organize system updates in logical dependency order
5. **Memory Usage**: Clean up unused entities and components regularly

---

*For complete examples and tutorials, see the [examples directory](examples/) and [getting started guide](getting_started.md).*