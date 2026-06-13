#pragma once
#include "IRenderPass.hpp"

/**
 * @brief Phase 5 overlay pass: recolours FEM bodies' SOLID render meshes by the
 * published nodal scalar field (von Mises / temperature / strain), through the
 * SAME cold->hot ramp and (MPM-shared) dynamic range as the particle viz. Drawn
 * forward over the lit opaque result (depth LEQUAL) only when a viz mode is
 * active; in Default mode it is a no-op (the body shows normal PBR).
 */
class FemVizPass : public IRenderPass {
public:
    void execute(const RenderFrameContext& context) override;
};
