#include "SelectionHighlightPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "SelectionService.hpp"   // krs::sel::SelectionState / indicator / buildIndicatorLines

#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>

namespace {

constexpr glm::vec3 kHoverColor{ 1.00f, 0.95f, 0.20f };   // bright yellow = "about to select"
constexpr glm::vec3 kSelectColor{ 1.00f, 0.55f, 0.10f };  // orange = committed selection
constexpr float kHoverWidth = 2.5f;                       // px (driver-clamped on some GL profiles)
constexpr float kSelectWidth = 4.0f;                      // a couple px thicker than hover
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
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);   // overlay: don't occlude later passes

    // SELECTED features (committed): orange, double ring + thicker line for a "locked" look.
    if (!st->selected.empty()) {
        std::vector<glm::vec3> selLines;
        for (const auto& sel : st->selected) {
            if (!sel.valid) continue;
            const krs::sel::IndicatorLines L =
                krs::sel::buildIndicatorLines(krs::sel::indicator(sel, 32, kPlaneHalf));
            appendIndicator(selLines, L);
            appendOuterRing(selLines, L, 1.12f);     // concentric accent
        }
        gl->glLineWidth(kSelectWidth);
        drawLines(context, selLines, kSelectColor);
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
}
