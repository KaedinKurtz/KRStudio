#pragma once

#include "IRenderPass.hpp"

/**
 * @brief Overlay pass that draws the fluid particle pool as lit spherical
 * point sprites, pulling positions directly from the solver's SSBO.
 */
class FluidPass : public IRenderPass
{
public:
    void execute(const RenderFrameContext& context) override;
};
