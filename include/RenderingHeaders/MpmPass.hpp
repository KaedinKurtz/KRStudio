#pragma once

#include "IRenderPass.hpp"

/**
 * @brief Overlay pass that draws the MLS-MPM particle pool as lit spherical
 * point sprites, pulling positions directly from MpmSystem's SSBO. Opaque,
 * depth-tested against the scene.
 */
class MpmPass : public IRenderPass
{
public:
    void execute(const RenderFrameContext& context) override;
};
