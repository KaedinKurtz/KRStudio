#pragma once

#include "RenderingSystem.hpp"

/// Base interface for all post-processing passes.
/// Each pass receives the current RenderFrameContext and
/// is responsible for binding its shader, input textures,
/// and issuing draw calls on the full-screen quad.
struct IPostProcessingPass {
    virtual ~IPostProcessingPass() = default;

    /// Execute this pass using the provided context.
    /// ctx.gl     : OpenGL functions
    /// ctx.camera : camera for this frame (unused by most passes)
    /// ctx.targetFBOs.finalColorTexture : input texture (lit scene)
    virtual void execute(RenderFrameContext& ctx) = 0;
};
