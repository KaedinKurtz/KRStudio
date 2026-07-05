#include "SelectionHighlightPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "SelectionService.hpp"   // krs::sel::SelectionState / indicator / buildIndicatorLines
#include "EdgeSelect.hpp"         // krs::sel::resolveEdge -- TRUE-edge picks travel with their body
#include "Camera.hpp"             // Camera::fovDeg -- screen-constant marker sizing

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
// (the concentric-ring helpers + widths retired with the ring UI -- the committed-selection
// visual is the quadrant glyph now; krs::sel::indicator/buildIndicatorLines remain gated
// backend geometry used by INDICATOR-GEOMETRY/HIGHLIGHT-MATCHES, not by this pass.)

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

    // P2 composite quad (pos3 + uv2, the post_process_vert layout)
    const float quad[] = {
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    };
    gl->glGenVertexArrays(1, &m_quadVao);
    gl->glGenBuffers(1, &m_quadVbo);
    gl->glBindVertexArray(m_quadVao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    gl->glBindVertexArray(0);
}

void SelectionHighlightPass::onContextDestroyed(QOpenGLContext*, QOpenGLFunctions_4_3_Core* gl)
{
    if (m_vbo) { gl->glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { gl->glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_quadVbo) { gl->glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_quadVao) { gl->glDeleteVertexArrays(1, &m_quadVao); m_quadVao = 0; }
    m_vboCapacity = 0;
}

void SelectionHighlightPass::drawIdHighlightComposite(const RenderFrameContext& ctx)
{
    // Requires the pick-ID target (renderAllViewports keeps it fresh whenever a hover or
    // selection is live) and the composite shader; silently skips otherwise.
    if (ctx.targetFBOs.pickIdTexture == 0 || m_quadVao == 0) return;
    auto* st = ctx.registry.ctx().find<krs::sel::SelectionState>();
    if (!st) return;
    Shader* hs = ctx.renderer.getShader("highlight_id");
    if (!hs) return;
    auto* gl = ctx.gl;

    // (entityId+1, typeBits|featureId+1) -- the EXACT encoding the pick pass writes.
    auto pair = [](const krs::sel::Selection& s) -> glm::uvec2 {
        if (!s.valid) return { 0u, 0u };
        std::uint32_t type = 0u, feat = 0u;
        if (s.type == krs::sel::FeatureType::EdgeCircle || s.type == krs::sel::FeatureType::EdgeLine) {
            if (s.edgeId < 0) return { 0u, 0u };
            type = 1u; feat = std::uint32_t(s.edgeId) + 1u;
        } else if (s.type == krs::sel::FeatureType::Vertex) {
            if (s.vertexId < 0) return { 0u, 0u };            // tessellation-vertex fallback: rings only
            type = 2u; feat = std::uint32_t(s.vertexId) + 1u;
        } else {
            if (s.faceId < 0) return { 0u, 0u };
            feat = std::uint32_t(s.faceId) + 1u;
        }
        return { std::uint32_t(s.entity) + 1u, (type << 30) | feat };
    };

    hs->use(gl);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, ctx.targetFBOs.pickIdTexture);
    hs->setInt(gl, "uIdTex", 0);
    hs->setUVec2(gl, "uHover", pair(st->hover));
    int n = 0;
    for (const auto& s : st->selected) {
        if (n >= 8) break;
        const glm::uvec2 p = pair(s);
        if (p.x == 0u) continue;
        hs->setUVec2(gl, "uSel[" + std::to_string(n) + "]", p);
        ++n;
    }
    hs->setInt(gl, "uSelCount", n);
    hs->setVec4(gl, "uHoverColor", glm::vec4(kHoverColor, 0.22f));
    hs->setVec4(gl, "uSelColor",  glm::vec4(kSelectColor, 0.26f));
    hs->setVec2(gl, "uTexSize", glm::vec2(float(ctx.targetFBOs.pickW), float(ctx.targetFBOs.pickH)));
    hs->setFloat(gl, "uInvExposure", 1.0f / ctx.renderer.exposureMultiplier());

    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glBindVertexArray(m_quadVao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    gl->glDisable(GL_BLEND);
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
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    if (!st) return;
    // `enabled` gates INPUT, not display: the choose-bores mode DISARMS ITSELF once the pair is
    // committed, but the operator still needs to SEE the two picked bores for Define. Only the
    // hover preview dies with the mode.
    if (!st->enabled) st->hover = krs::sel::Selection{};
    if (!st->hover.valid && st->selected.empty()) return;
    // Selections TRAVEL WITH their body: re-derive each committed pick's world frame from its
    // (entity, faceId) at the CURRENT transform every frame -- a bore selected on a link that then
    // moves (FK drive, IK drag) used to leave its ring orphaned in space where it was clicked.
    krs::sel::refreshSelections(*st, reg);
    // TRUE-EDGE picks re-derive too (resolveEdge lives above SelectionService in the include graph).
    for (auto& s : st->selected)
        if (s.valid && s.edgeId >= 0 && reg.valid(s.entity)) {
            krs::sel::Selection f = krs::sel::resolveEdge(reg, s.entity, s.edgeId);
            if (f.valid) { f.groupId = s.groupId; s = f; }
        }
    if (!st->hover.valid && st->selected.empty()) return;   // refresh may have dropped dead entries

    auto* gl = context.gl;
    // ALWAYS-ON-TOP: a selected bore's ring sits on the axis (often inside the part), so depth-testing
    // it hides it behind the body. Disable the depth test for the highlight so the ring/arrow is always
    // visible -- the operator must see what they picked regardless of viewing angle. Depth write stays
    // off so the overlay never occludes later passes.
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);

    // P2: the CAD-grade highlight -- pixel-exact face FILL + ID-discontinuity CONTOUR from the
    // pick buffer (edges/vertices glow through their fat pick footprints). The analytic rings
    // below stay for the axis arrows and the ordered pick colors.
    drawIdHighlightComposite(context);

    // COMMITTED selections: the compact JOINT-ORIGIN QUADRANT GLYPH at each pick's true anchor
    // (nearest rim for bores, the click point for planes) -- first pick GREEN-accented, second
    // BLUE (the mate-pair cue survives), 3rd+ orange. The giant concentric analytic rings are
    // RETIRED: the P2 fill/contour above carries the surface read; the marker pins the frame
    // origin the pick actually means.
    std::size_t shown = 0;
    for (const auto& sel : st->selected) {
        if (!sel.valid) continue;
        drawQuadrantMarker(context, sel, (shown < 2) ? kFeatColor[shown] : kSelectColor);
        ++shown;
    }
    // HOVER is carried by the P2 fill/contour + the SnapOverlay dots; the yellow ring is retired.

    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);   // restore for later passes
}

void SelectionHighlightPass::drawFan(const RenderFrameContext& ctx,
                                     const std::vector<glm::vec3>& fan, const glm::vec3& color)
{
    if (fan.size() < 3 || !m_vao) return;
    Shader* shader = ctx.renderer.getShader("collision_debug");
    if (!shader) return;
    auto* gl = ctx.gl;
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (fan.size() > m_vboCapacity) {
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(fan.size() * sizeof(glm::vec3)),
                         fan.data(), GL_DYNAMIC_DRAW);
        m_vboCapacity = fan.size();
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(fan.size() * sizeof(glm::vec3)), fan.data());
    }
    shader->use(gl);
    shader->setMat4(gl, "u_mvp", ctx.projection * ctx.view);
    shader->setVec3(gl, "u_color", color);
    shader->setFloat(gl, "u_invExposure", 1.0f / ctx.renderer.exposureMultiplier());
    gl->glDrawArrays(GL_TRIANGLE_FAN, 0, GLsizei(fan.size()));
    gl->glBindVertexArray(0);
}

void SelectionHighlightPass::drawQuadrantMarker(const RenderFrameContext& ctx,
                                                const krs::sel::Selection& s, const glm::vec3& accent)
{
    using FT = krs::sel::FeatureType;
    glm::vec3 p = s.hitPoint, n(0.0f);
    switch (s.type) {
        case FT::Cylinder:
        case FT::Cone:
            n = s.axisDir;
            if (glm::dot(p, p) < 1e-12f) p = s.axisPos;    // untrimmed bore: axis midpoint
            break;
        case FT::Plane:
            n = s.normal;
            if (glm::dot(p, p) < 1e-12f) p = s.axisPos;
            break;
        case FT::EdgeCircle:
        case FT::EdgeLine:
            n = s.axisDir;
            p = s.axisPos;
            break;
        default:                                           // Vertex / Sphere / Other: face the camera
            n = ctx.camera.getPosition() - p;
            break;
    }
    if (glm::dot(n, n) < 1e-9f) n = glm::vec3(0, 0, 1);
    n = glm::normalize(n);
    glm::vec3 x = glm::cross(n, glm::vec3(0, 0, 1));
    if (glm::dot(x, x) < 1e-8f) x = glm::cross(n, glm::vec3(0, 1, 0));
    x = glm::normalize(x);
    const glm::vec3 y = glm::normalize(glm::cross(n, x));

    // screen-constant radius (~9 px at any zoom)
    const float dist = glm::length(ctx.camera.getPosition() - p);
    const float wpp = (2.0f * dist * std::tan(glm::radians(Camera::fovDeg() * 0.5f)))
                      / float(std::max(1, ctx.viewportHeight));
    const float r = 9.0f * wpp;

    // four quadrant fans: opposite quadrants take the order accent, the others near-white
    const glm::vec3 white(0.94f, 0.94f, 0.94f);
    for (int q = 0; q < 4; ++q) {
        std::vector<glm::vec3> fan;
        fan.push_back(p);
        const int segs = 8;
        for (int i = 0; i <= segs; ++i) {
            const float a = glm::radians(90.0f) * (float(q) + float(i) / float(segs));
            fan.push_back(p + r * (std::cos(a) * x + std::sin(a) * y));
        }
        drawFan(ctx, fan, ((q & 1) == 0) ? accent : white);
    }
    // outline ring + a short axis stub so a bore still reads its direction
    std::vector<glm::vec3> lines;
    const int segs = 24;
    for (int i = 0; i < segs; ++i) {
        const float a0 = 6.2831853f * float(i) / segs, a1 = 6.2831853f * float(i + 1) / segs;
        lines.push_back(p + r * (std::cos(a0) * x + std::sin(a0) * y));
        lines.push_back(p + r * (std::cos(a1) * x + std::sin(a1) * y));
    }
    lines.push_back(p);
    lines.push_back(p + n * (2.4f * r));
    drawLines(ctx, lines, accent * kGlow);
}
