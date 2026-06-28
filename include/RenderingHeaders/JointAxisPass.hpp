#pragma once

#include "IRenderPass.hpp"

// Forward declarations
class Shader;

/**
 * @class JointAxisPass
 * @brief Draws joint-axis indicator bars (entities tagged JointAxisComponent) always
 *        on top, so a robot's defined rotation axes are visible even when the bar sits
 *        inside a link body.
 *
 * Mirrors GizmoPass: runs in the overlay stage AFTER tonemap (so it needs no exposure
 * pre-divide), clears the depth buffer so nothing occludes the axes, and draws each bar
 * with the flat "gizmo_highlight" shader using its MaterialComponent.albedoColor.
 */
class JointAxisPass : public IRenderPass {
public:
    ~JointAxisPass() override = default;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
};
