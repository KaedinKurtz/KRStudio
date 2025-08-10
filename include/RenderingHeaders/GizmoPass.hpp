#pragma once

#include "IRenderPass.hpp"

// Forward declarations
class Shader;

/**
 * @class GizmoPass
 * @brief Renders the 3D transformation gizmo handles.
 *
 * This pass is responsible for drawing all entities that are part of the gizmo.
 * It runs late in the overlay stage and disables depth testing to ensure the
 * gizmo is always visible and drawn on top of other scene objects. It also
 * respects the HiddenComponent tag to control visibility of handles.
 */
class GizmoPass : public IRenderPass {
public:
    ~GizmoPass() override = default;

    /**
     * @brief Initializes the pass. This can be empty for now as the shader is retrieved during execution.
     */
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;

    /**
     * @brief Executes the gizmo rendering logic for a single frame.
     */
    void execute(const RenderFrameContext& context) override;
};
