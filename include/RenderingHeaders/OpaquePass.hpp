#pragma once
#include "IRenderPass.hpp"

class Shader; // Forward declaration

class OpaquePass : public IRenderPass {
public:
    // Gets a pointer to the shared Phong shader from the RenderingSystem.
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    // Renders all RenderableMeshComponent entities.
    void execute(const RenderFrameContext& context) override;

private:
};