#pragma once

#include "IRenderPass.hpp"

#include <glm/glm.hpp>
#include <vector>

/**
 * @brief Sub-feature selection HIGHLIGHT overlay (the visual half of feature
 * selection). Reads krs::sel::SelectionState from registry.ctx() and draws,
 * for the HOVERED feature and each SELECTED feature, the analytic indicator
 * (the disk rim on a cylinder's axis / the outline on a plane) plus an axis
 * (or surface-normal) arrow. The geometry is DERIVED from the gated backend
 * (krs::sel::indicator + buildIndicatorLines) -- it never drifts from the true
 * feature. Hover and selected use DISTINCT colours so the user can tell a
 * preview from a committed selection.
 *
 * Inspectable-at-rest identity/geometry is gated (HIGHLIGHT-MATCHES,
 * INDICATOR-GEOMETRY-CORRECT, MULTI-SELECT); that the lines actually render on
 * the user's screen is OPERATOR-VISUAL-CONFIRM.
 */
class SelectionHighlightPass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    // One dynamic line buffer, re-uploaded each frame (selection changes interactively).
    void drawLines(const RenderFrameContext& ctx, const std::vector<glm::vec3>& lines,
                   const glm::vec3& color);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::size_t  m_vboCapacity = 0;   // in vertices
};
