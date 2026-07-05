// SnapOverlayPass.cpp -- see SnapOverlayPass.hpp. Post-tonemap (LDR) overlay.
#include "SnapOverlayPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "SnapSession.hpp"     // krs::snapui::SnapSessionState (ctx)
#include "Snap.hpp"            // krs::snap corrections (flipZ / rotateX90)

#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>

namespace {

constexpr glm::vec3 kDotColor{ 0.95f, 0.95f, 0.95f };     // wake-up dots: quiet white
constexpr glm::vec3 kActiveDot{ 1.00f, 0.85f, 0.10f };    // active candidate glyph: gold
constexpr glm::vec3 kAxisX{ 0.90f, 0.15f, 0.15f };
constexpr glm::vec3 kAxisY{ 0.15f, 0.80f, 0.20f };
constexpr glm::vec3 kAxisZ{ 0.20f, 0.45f, 1.00f };
constexpr glm::vec3 kDiskFront{ 0.96f, 0.96f, 0.96f };    // +Z faces the camera: WHITE
constexpr glm::vec3 kDiskBack{ 1.00f, 0.55f, 0.10f };     // -Z faces the camera: ORANGE

// screen-constant world size: ~`px` logical pixels at this depth (vertical FOV path)
float worldPerPixel(const Camera& cam, const glm::vec3& p, int viewportH)
{
    const float dist = glm::length(cam.getPosition() - p);
    const float halfH = dist * std::tan(glm::radians(Camera::fovDeg() * 0.5f));
    return (2.0f * halfH) / float(std::max(1, viewportH));
}

// Fusion glyph legend as line segments in the camera plane at `pos`, radius r.
void appendGlyph(std::vector<glm::vec3>& out, int glyph, const glm::vec3& pos,
                 const glm::vec3& right, const glm::vec3& up, float r)
{
    auto seg = [&](const glm::vec2& a, const glm::vec2& b) {
        out.push_back(pos + a.x * right + a.y * up);
        out.push_back(pos + b.x * right + b.y * up);
    };
    switch (glyph) {
        case 0: {                                             // SQUARE: face centroid
            const float h = r * 0.85f;
            seg({ -h, -h }, { h, -h }); seg({ h, -h }, { h, h });
            seg({ h, h }, { -h, h });   seg({ -h, h }, { -h, -h });
            break;
        }
        case 1: {                                             // PLUS: centre point
            seg({ -r, 0 }, { r, 0 }); seg({ 0, -r }, { 0, r });
            break;
        }
        case 2: {                                             // TRIANGLE: midpoint
            const glm::vec2 a{ 0, r }, b{ -0.87f * r, -0.5f * r }, c{ 0.87f * r, -0.5f * r };
            seg(a, b); seg(b, c); seg(c, a);
            break;
        }
        case 3: {                                             // CIRCLE: vertex
            const int n = 10;
            for (int i = 0; i < n; ++i) {
                const float a0 = 6.2831853f * float(i) / n, a1 = 6.2831853f * float(i + 1) / n;
                seg({ r * std::cos(a0), r * std::sin(a0) }, { r * std::cos(a1), r * std::sin(a1) });
            }
            break;
        }
        default: {                                            // DIAMOND: axis / apex point
            seg({ 0, r }, { r, 0 }); seg({ r, 0 }, { 0, -r });
            seg({ 0, -r }, { -r, 0 }); seg({ -r, 0 }, { 0, r });
            break;
        }
    }
}

} // namespace

void SnapOverlayPass::initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core* gl)
{
    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl->glBindVertexArray(0);
}

void SnapOverlayPass::onContextDestroyed(QOpenGLContext*, QOpenGLFunctions_4_3_Core* gl)
{
    if (m_vbo) { gl->glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { gl->glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_capacity = 0;
}

void SnapOverlayPass::drawLines(const RenderFrameContext& ctx,
                                const std::vector<glm::vec3>& lines, const glm::vec3& color)
{
    if (lines.empty() || !m_vao) return;
    Shader* shader = ctx.renderer.getShader("collision_debug");
    if (!shader) return;
    auto* gl = ctx.gl;
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (lines.size() > m_capacity) {
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lines.size() * sizeof(glm::vec3)),
                         lines.data(), GL_DYNAMIC_DRAW);
        m_capacity = lines.size();
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(lines.size() * sizeof(glm::vec3)), lines.data());
    }
    shader->use(gl);
    shader->setMat4(gl, "u_mvp", ctx.projection * ctx.view);
    shader->setVec3(gl, "u_color", color);
    shader->setFloat(gl, "u_invExposure", 1.0f);   // post-tonemap: LDR as-is
    gl->glDrawArrays(GL_LINES, 0, GLsizei(lines.size()));
    gl->glBindVertexArray(0);
}

void SnapOverlayPass::drawFan(const RenderFrameContext& ctx,
                              const std::vector<glm::vec3>& fan, const glm::vec3& color)
{
    if (fan.size() < 3 || !m_vao) return;
    Shader* shader = ctx.renderer.getShader("collision_debug");
    if (!shader) return;
    auto* gl = ctx.gl;
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (fan.size() > m_capacity) {
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(fan.size() * sizeof(glm::vec3)),
                         fan.data(), GL_DYNAMIC_DRAW);
        m_capacity = fan.size();
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(fan.size() * sizeof(glm::vec3)), fan.data());
    }
    shader->use(gl);
    shader->setMat4(gl, "u_mvp", ctx.projection * ctx.view);
    shader->setVec3(gl, "u_color", color);
    shader->setFloat(gl, "u_invExposure", 1.0f);
    gl->glDrawArrays(GL_TRIANGLE_FAN, 0, GLsizei(fan.size()));
    gl->glBindVertexArray(0);
}

void SnapOverlayPass::execute(const RenderFrameContext& context)
{
    auto* ss = context.registry.ctx().find<krs::snapui::SnapSessionState>();
    if (!ss || !ss->armed || ss->candidates.empty()) return;

    auto* gl = context.gl;
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);

    // camera basis for screen-facing glyphs
    const glm::vec3 right(context.view[0][0], context.view[1][0], context.view[2][0]);
    const glm::vec3 up(context.view[0][1], context.view[1][1], context.view[2][1]);

    // ---- wake-up dots (all candidates), active one drawn gold + bigger ----
    std::vector<glm::vec3> dots, activeGlyph;
    for (std::size_t i = 0; i < ss->candidates.size(); ++i) {
        const auto& c = ss->candidates[i];
        const float wpp = worldPerPixel(context.camera, c.pos, context.viewportHeight);
        if (int(i) == ss->activeIdx)
            appendGlyph(activeGlyph, c.glyph, c.pos, right, up, 7.0f * wpp);
        else
            appendGlyph(dots, c.glyph, c.pos, right, up, 4.5f * wpp);
    }
    drawLines(context, dots, kDotColor);
    drawLines(context, activeGlyph, kActiveDot);

    // ---- the mate-frame preview on the active candidate (corrections applied) ----
    if (ss->activeIdx >= 0 && ss->activeIdx < int(ss->candidates.size())) {
        krs::snap::SnapCandidate c = ss->candidates[std::size_t(ss->activeIdx)];
        if (ss->flipped) c = krs::snap::flipZ(c);
        for (int r = 0; r < (ss->rotSteps & 3); ++r) c = krs::snap::rotateX90(c);

        const float wpp = worldPerPixel(context.camera, c.pos, context.viewportHeight);
        const glm::vec3 y = glm::normalize(glm::cross(c.z, c.x));
        const float diskR = 16.0f * wpp;

        // two-tone disk: WHITE when +Z faces the camera, ORANGE when the back side does
        const bool frontFacing = glm::dot(c.z, context.camera.getPosition() - c.pos) >= 0.0f;
        std::vector<glm::vec3> fan;
        fan.push_back(c.pos);
        const int n = 24;
        for (int i = 0; i <= n; ++i) {
            const float a = 6.2831853f * float(i) / n;
            fan.push_back(c.pos + diskR * (std::cos(a) * c.x + std::sin(a) * y));
        }
        drawFan(context, fan, frontFacing ? kDiskFront : kDiskBack);

        // axis stubs: X red, Y green, Z blue (longer, with barbs)
        std::vector<glm::vec3> ax;
        ax.push_back(c.pos); ax.push_back(c.pos + c.x * (22.0f * wpp));
        drawLines(context, ax, kAxisX);
        ax.clear();
        ax.push_back(c.pos); ax.push_back(c.pos + y * (22.0f * wpp));
        drawLines(context, ax, kAxisY);
        ax.clear();
        const glm::vec3 zTip = c.pos + c.z * (34.0f * wpp);
        ax.push_back(c.pos); ax.push_back(zTip);
        ax.push_back(zTip); ax.push_back(zTip - c.z * (7.0f * wpp) + c.x * (4.0f * wpp));
        ax.push_back(zTip); ax.push_back(zTip - c.z * (7.0f * wpp) - c.x * (4.0f * wpp));
        drawLines(context, ax, kAxisZ);
    }

    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);
}
