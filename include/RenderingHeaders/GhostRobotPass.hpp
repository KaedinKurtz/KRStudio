#pragma once

#include "IRenderPass.hpp"

class Shader;

/**
 * @class GhostRobotPass
 * @brief Phase 7 capstone -- the translucent "ghost validity robot".
 *
 * Draws each live robot's COMMANDED (pre-clamp) pose, FK(qCommandRaw), as a translucent tinted
 * overlay of the robot's own meshes: GREEN where the command is reachable, RED on the links beyond a
 * joint that hit its limit. The real (clamped) robot keeps rendering normally; the ghost shows where
 * it WOULD be and which joint is at its stop.
 *
 * Runs in the overlay stage AFTER tonemap (display space -> colour output directly, no exposure
 * pre-divide), with the scene depth blitted into the final FBO already, so it depth-tests against the
 * real robot (write off, test on) -- the divergent ghost reads through empty space but is occluded by
 * nearer geometry. It only draws a robot when a joint is actually clamped, so it is invisible in
 * normal operation (zero impact on the established look). Main-scene only: it keys off the ctx
 * RobotRegistry, which the isolated Robot-View scene does not have.
 */
class GhostRobotPass : public IRenderPass {
public:
    ~GhostRobotPass() override = default;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
};
