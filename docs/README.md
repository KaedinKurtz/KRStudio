# Robotics Software Documentation

Welcome to the comprehensive documentation for the Robotics Software development platform. This documentation provides everything you need to understand, build with, and extend this advanced robotics workstation.

## Documentation Overview

This documentation is organized to help you quickly find what you need, whether you're just getting started or diving deep into advanced features.

### 🚀 [Getting Started Guide](getting_started.md)
**Perfect for beginners** - Complete setup instructions, build guide, and your first robotics application.

**What you'll learn:**
- System requirements and dependency installation
- Building and running the software
- Basic navigation and UI overview
- Creating your first robotics application
- Troubleshooting common issues

**Time to complete:** ~30 minutes

### 📚 [Complete API Documentation](API_DOCUMENTATION.md)
**Comprehensive reference** - Full API documentation with architecture overview and component index.

**What's covered:**
- Software architecture and design principles
- Quick start examples and code snippets
- Complete API reference index
- Build and development instructions
- Links to detailed component documentation

### 🎯 Core API Documentation

#### [Rendering System API](rendering_api.md)
**Graphics and Visualization** - Complete OpenGL 4.3 rendering pipeline for 3D robotics visualization.

**Key features:**
- Multi-viewport rendering with independent cameras
- Extensible render pass pipeline
- Shader management and resource optimization
- Post-processing effects (glow, selection)
- Point cloud rendering with RealSense integration

#### [SLAM API](slam_api.md)
**Localization and Mapping** - Real-time SLAM system with RealSense camera integration.

**Key features:**
- Multi-threaded frontend/backend architecture
- Dense voxel-based mapping with surfel representation
- Real-time pose estimation and tracking
- Thread-safe map access and updates
- Memory-efficient sparse voxel grid

#### [Scene & Object Management API](scene_api.md)
**3D Scene Management** - Entity-Component-System architecture for managing robots, cameras, and objects.

**Key features:**
- ECS architecture with EnTT
- URDF robot loading and kinematic simulation
- Advanced camera controls (orbit, fly, pan, dolly)
- Efficient mesh management with GPU optimization
- Hierarchical object relationships

## 📖 Examples and Tutorials

### [SLAM Integration Example](examples/slam_example.md)
**Complete working example** - Comprehensive tutorial showing real-time SLAM integration with RealSense camera.

**What you'll build:**
- RealSense camera manager for RGB-D data capture
- Complete SLAM application with professional UI
- Real-time map visualization and robot tracking
- Error handling and map management features

**Prerequisites:** Intel RealSense camera, basic C++ knowledge
**Time to complete:** ~2 hours

### Future Examples (Coming Soon)
- **Custom Node Development** - Creating nodes for the visual programming system
- **Multi-Viewport Setup** - Setting up multiple synchronized 3D viewports
- **Robot Simulation** - Complete robot simulation with physics and control
- **Database Integration** - Data persistence and management examples

## 🧩 Advanced Documentation (In Development)

The following documentation is currently being developed:

### UI Components API
- MainWindow and application framework
- ViewportWidget and 3D interaction
- Advanced docking system integration
- Property panels and node catalogs

### Node Programming API
- Visual programming framework
- Node types (arithmetic, control systems, sensors)
- Node factory and execution system
- Custom node development

### Database API
- Core database operations and optimization
- Backup, migration, and replication
- Query optimization and indexing
- Data persistence patterns

### Utility APIs
- URDF parsing and robot description
- Data conversion and mesh utilities
- Application path management
- Cross-cutting utility functions

### Component System API
- Complete ECS component reference
- Transform, mesh, material, and lighting components
- Robot-specific components (joints, links)
- Physics and rendering components

## 📋 Documentation Quick Reference

| Topic | Document | Best For | Time Investment |
|-------|----------|----------|-----------------|
| **Getting Started** | [getting_started.md](getting_started.md) | New users, setup | 30 min |
| **API Overview** | [API_DOCUMENTATION.md](API_DOCUMENTATION.md) | Architecture understanding | 15 min |
| **Rendering** | [rendering_api.md](rendering_api.md) | Graphics developers | 45 min |
| **SLAM** | [slam_api.md](slam_api.md) | Robotics engineers | 45 min |
| **Scene Management** | [scene_api.md](scene_api.md) | 3D application developers | 45 min |
| **SLAM Example** | [examples/slam_example.md](examples/slam_example.md) | Hands-on learning | 2 hours |

## 🎯 Learning Paths

### For Robotics Engineers
1. [Getting Started Guide](getting_started.md) - Setup and basics
2. [SLAM API](slam_api.md) - Understanding localization and mapping
3. [SLAM Integration Example](examples/slam_example.md) - Practical implementation
4. [Scene API](scene_api.md) - Robot modeling and kinematics

### For Graphics Developers
1. [Getting Started Guide](getting_started.md) - Setup and basics
2. [Rendering System API](rendering_api.md) - Graphics pipeline
3. [Scene API](scene_api.md) - 3D object management
4. [API Documentation](API_DOCUMENTATION.md) - Full architecture

### For Application Developers
1. [Getting Started Guide](getting_started.md) - Setup and basics
2. [API Documentation](API_DOCUMENTATION.md) - Architecture overview
3. [Scene API](scene_api.md) - Object management
4. [SLAM Integration Example](examples/slam_example.md) - Complete application

## 🛠 Development Resources

### Build Instructions
Complete build instructions are available in the [Getting Started Guide](getting_started.md#building-the-software).

### Dependencies
- **Qt6** (Core, Widgets, OpenGL, SQL, Concurrent)
- **OpenGL 4.3+** 
- **GLM** (OpenGL Mathematics)
- **EnTT** (Entity-Component-System)
- **Assimp** (3D model loading)
- **URDF** (Robot description parsing)
- **RealSense SDK2** (camera support)
- **OpenCV** (computer vision)
- **Eigen3** (linear algebra)

### Platform Support
- **Windows** 10/11 with Visual Studio 2019+
- **Linux** Ubuntu 20.04+ with GCC 9+
- **macOS** 10.15+ with Clang 10+

## 🤝 Contributing

### Documentation Guidelines
- Follow the existing structure and formatting
- Include working code examples
- Test all code snippets before submission
- Update the index when adding new documents

### API Documentation Standards
- Document all public APIs and functions
- Include parameter descriptions and return values
- Provide usage examples for complex functionality
- Explain threading and performance considerations

## 📞 Support and Community

### Getting Help
1. Check the relevant API documentation
2. Look through the [troubleshooting section](getting_started.md#troubleshooting)
3. Search existing GitHub issues
4. Create a detailed issue with system information

### Reporting Issues
When reporting bugs, please include:
- Operating system and version
- Compiler and version
- Complete error messages
- Steps to reproduce
- Minimal code example (if applicable)

## 🔄 Documentation Status

### ✅ Complete
- Getting Started Guide
- Main API Documentation 
- Rendering System API
- SLAM API
- Scene & Object Management API
- SLAM Integration Example

### 🚧 In Progress
- UI Components API
- Node Programming API
- Database API
- Utility APIs
- Component System API
- Additional examples and tutorials

### 📅 Planned
- Performance optimization guide
- Advanced rendering techniques
- Custom shader development
- Multi-robot coordination
- Real-time communication protocols

---

*This documentation is continuously updated as the software evolves. For the most current information, always refer to the latest version of these documents.*

## Document Navigation

- **[⬆️ Back to Top](#robotics-software-documentation)**
- **[🚀 Quick Start](getting_started.md)**
- **[📚 Full API Reference](API_DOCUMENTATION.md)**
- **[🎯 Example Projects](examples/)**