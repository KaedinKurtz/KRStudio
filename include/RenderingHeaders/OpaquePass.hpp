#pragma once
#include "IRenderPass.hpp"
#include "components.hpp"
#include "GizmoSystem.hpp"

class Shader; // Forward declaration

class OpaquePass : public IRenderPass {
public:
    // Gets a pointer to the shared Phong shader from the RenderingSystem.
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    // Renders all RenderableMeshComponent entities.
    void execute(const RenderFrameContext& context) override;

    using DeferredExclusionTags = entt::exclude_t<
        GizmoHandleComponent          // add more tags here later
        // , NoDeferredDrawTag
        // , AnotherSkipTag
    >;

private:
};