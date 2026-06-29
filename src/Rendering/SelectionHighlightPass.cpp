#include "SelectionHighlightPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "SelectionService.hpp"   // krs::sel::SelectionState / indicator / buildIndicatorLines

#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>

namespace {

constexpr glm::vec3 kHoverColor{ 1.00f, 0.95f, 0.20f };   // bright yellow = "about to select"
constexpr glm::vec3 kSelectColor{ 1.00f, 0.55f, 0.10f };  // orange = 3rd+ committed selection
// The robot-builder mate workflow picks TWO bores: the FIRST is the parent/anchor (GREEN), the
// SECOND is the child that snaps onto it (BLUE). Distinct colors so the operator sees which is which.
constexpr glm::vec3 kFeatColor[2] = {
    { 0.10f, 1.00f, 0.25f },   // [0] first pick  = GREEN
    { 0.18f, 0.50f, 1.00f },   // [1] second pick = BLUE
};
constexpr float kGlow = 1.5f;                            // slight HDR lift for a glow WITHOUT clipping the hue to white
constexpr float kHoverWidth = 2.5f;                       // px (driver-clamped on some GL profiles)
constexpr float kSelectWidth = 6.0f;                      // thick "locked" ring (driver-clamped on core profiles)
constexpr float kPlaneHalf = 0.03f;                       // planar-face outline half-size (was 0.01 -- too small to read)

// Append an IndicatorLines (rim + arrow) into a flat GL_LINES vertex list.
void appendIndicator(std::vector<glm::vec3>& out, const krs::sel::IndicatorLines& L)
{
    out.insert(out.end(), L.ring.begin(), L.ring.end());
    out.insert(out.end(), L.arrow.begin(), L.arrow.end());
}

// A second concentric rim (scaled about the disk centre) so a COMMITTED selection
// reads distinctly from a hover preview. Pure cosmetic; derived from the same rim.
void appendOuterRing(std::vector<glm::vec3>& out, const krs::sel::IndicatorLines& L, float scale)
{
    for (const glm::vec3& p : L.ring)
        out.push_back(L.diskCenter + scale * (p - L.diskCenter));
}

} // namespace

void SelectionHighlightPass::initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core* gl)
{
    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl->glBindVertexArray(0);
}

void SelectionHighlightPass::onContextDestroyed(QOpenGLContext*, QOpenGLFunctions_4_3_Core* gl)
{
    if (m_vbo) { gl->glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { gl->glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_vboCapacity = 0;
}

void SelectionHighlightPass::drawLines(const RenderFrameContext& ctx,
                                       const std::vector<glm::vec3>& lines, const glm::vec3& color)
{
    if (lines.empty() || !m_vao) return;
    Shader* shader = ctx.renderer.getShader("collision_debug");
    if (!shader) return;

    auto* gl = ctx.gl;
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // grow-only reallocation; orphan-and-fill otherwise.
    if (lines.size() > m_vboCapacity) {
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lines.size() * sizeof(glm::vec3)),
                         lines.data(), GL_DYNAMIC_DRAW);
        m_vboCapacity = lines.size();
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
                            GLsizeiptr(lines.size() * sizeof(glm::vec3)), lines.data());
    }

    shader->use(gl);
    // indicator vertices are already in WORLD space -> model is identity.
    shader->setMat4(gl, "u_mvp", ctx.projection * ctx.view);
    shader->setVec3(gl, "u_color", color);
    // Overlay drawn pre-tonemap: compensate the photometric exposure so the
    // highlight is not crushed to black (matches GridPass / SelectionGlowPass).
    shader->setFloat(gl, "u_invExposure", 1.0f / ctx.renderer.exposureMultiplier());
    gl->glDrawArrays(GL_LINES, 0, GLsizei(lines.size()));
    gl->glBindVertexArray(0);
}

void SelectionHighlightPass::execute(const RenderFrameContext& context)
{
    auto& reg = context.registry;
    const auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st || !st->enabled) return;
    if (!st->hover.valid && st->selected.empty()) return;

    auto* gl = context.gl;
    // ALWAYS-ON-TOP: a selected bore's ring sits on the axis (often inside the part), so depth-testing
    // it hides it behind the body. Disable the depth test for the highlight so the ring/arrow is always
    // visible -- the operator must see what they picked regardless of viewing angle. Depth write stays
    // off so the overlay never occludes later passes.
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);

    // SELECTED features (committed): PER-FEATURE color so the operator sees the mate pair --
    // first pick GREEN (parent/anchor), second pick BLUE (child that snaps), 3rd+ orange. Each is a
    // double concentric ring at high HDR intensity so it reads as a glowing rim, not a hairline.
    gl->glLineWidth(kSelectWidth);
    std::size_t shown = 0;
    for (const auto& sel : st->selected) {
        if (!sel.valid) continue;
        const krs::sel::IndicatorLines L =
            krs::sel::buildIndicatorLines(krs::sel::indicator(sel, 64, kPlaneHalf));
        std::vector<glm::vec3> one;
        one.insert(one.end(), L.arrow.begin(), L.arrow.end());   // axis arrow (which way the joint turns)
        // Stack several concentric rings into a THICK band -- desktop GL clamps glLineWidth to 1px on
        // core profiles, so a band of rings is the only reliable way to read as a fat glowing rim.
        for (float sc : { 0.92f, 0.97f, 1.00f, 1.05f, 1.10f, 1.15f }) appendOuterRing(one, L, sc);
        const glm::vec3 base = (shown < 2) ? kFeatColor[shown] : kSelectColor;
        drawLines(context, one, base * kGlow);       // >1 -> glows through the tonemap
        ++shown;
    }

    // HOVERED feature (preview): yellow, single ring. Drawn last so it sits on top.
    if (st->hover.valid) {
        std::vector<glm::vec3> hovLines;
        appendIndicator(hovLines,
            krs::sel::buildIndicatorLines(krs::sel::indicator(st->hover, 32, kPlaneHalf)));
        gl->glLineWidth(kHoverWidth);
        drawLines(context, hovLines, kHoverColor);
    }

    gl->glLineWidth(1.0f);
    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);   // restore for later passes
}
