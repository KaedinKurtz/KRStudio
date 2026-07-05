#pragma once

#include "IRenderPass.hpp"
#include "SelectionService.hpp"   // krs::sel::Selection (quadrant-marker anchor derivation)

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
    // P2: the pixel-exact fill + ID-discontinuity contour composite from the pick-ID buffer
    // (re-fetches the ctx SelectionState internally; no-op when no pick target exists yet).
    void drawIdHighlightComposite(const RenderFrameContext& ctx);
    // Solid triangle fan through the same shader/exposure path as drawLines.
    void drawFan(const RenderFrameContext& ctx, const std::vector<glm::vec3>& fan,
                 const glm::vec3& color);
    // The committed-selection marker: the compact JOINT-ORIGIN QUADRANT GLYPH (two accent +
    // two white quadrants, outline circle, short axis stub) at the pick's true anchor point --
    // replaces the retired concentric analytic rings.
    void drawQuadrantMarker(const RenderFrameContext& ctx, const krs::sel::Selection& sel,
                            const glm::vec3& accent);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::size_t  m_vboCapacity = 0;   // in vertices
    unsigned int m_quadVao = 0;       // fullscreen composite quad (pos3 + uv2)
    unsigned int m_quadVbo = 0;
};
