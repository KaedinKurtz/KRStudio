#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "ViewportWidget.hpp"

// Forward declarations for decoupling
class RenderingSystem;
class QOpenGLFunctions_4_3_Core;
class QOpenGLContext;
class Camera;
struct TargetFBOs; // This struct will be fully defined in RenderingSystem.hpp

// This struct bundles all per-frame data into a single, clean package.
struct RenderFrameContext {
    QOpenGLFunctions_4_3_Core*   gl;
    entt::registry&              registry;
    RenderingSystem&             renderer;
    const Camera&                camera;
    const glm::mat4&             view;
    const glm::mat4&             projection;
    TargetFBOs&                  targetFBOs;
    int                          viewportWidth;
    int                          viewportHeight;
    float                        deltaTime;
    float                        elapsedTime;
    ViewportWidget*              viewport;
};

// The interface that all render pass classes must implement.
class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    // Called once when RenderingSystem starts. For loading shaders, etc.
    virtual void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {}

    // Called every frame. This is where the drawing happens.
    virtual void execute(const RenderFrameContext& context) = 0;

    // Called when a context is destroyed. Essential for cleanup.
    virtual void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {}

    // Called when a viewport is resized. For passes that manage FBOs.
    virtual void onResize(const RenderFrameContext& context) {}


};