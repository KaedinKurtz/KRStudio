#pragma once
// ===========================================================================
// SNAP OVERLAY (mate-selector P4) -- the Onshape/Fusion inference visuals.
// Draws, from the ctx SnapSessionState:
//   - a GLYPH DOT per candidate of the hovered feature (the Fusion legend:
//     square = face centroid, plus = centre, triangle = midpoint,
//     circle = vertex, diamond = axis/apex point), screen-constant size;
//   - on the ACTIVE (nearest-to-cursor) candidate: the mate-frame preview --
//     a two-tone DISK in the frame's XY plane (white toward +Z, orange when
//     the -Z side faces the camera: Fusion's which-way-will-it-mate cue),
//     red X / green Y stubs and a longer blue Z arrow, with the session's
//     F/R corrections applied so the preview IS what a click places.
// Registered post-tonemap (LDR, no exposure compensation), depth test off --
// inference UI must never hide inside geometry.
// ===========================================================================
#include "IRenderPass.hpp"

#include <glm/glm.hpp>
#include <vector>

class SnapOverlayPass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

private:
    void drawLines(const RenderFrameContext& ctx, const std::vector<glm::vec3>& lines,
                   const glm::vec3& color);
    void drawFan(const RenderFrameContext& ctx, const std::vector<glm::vec3>& fan,
                 const glm::vec3& color);

    unsigned int m_vao = 0, m_vbo = 0;         // shared dynamic buffer (lines + fans)
    std::size_t  m_capacity = 0;               // in vertices
};
