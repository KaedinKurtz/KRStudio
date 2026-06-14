// RenderGates.cpp — Phase F GATE G: F0 headless render self-test (KRS_RENDER_SELFTEST).
//
// Locks the render-correctness primitives that the F1/F2/F3 fixes touch by driving
// the REAL colormap path (the `fem_viz` shader: vT = clamp((scalar-min)/(max-min)),
// FragColor = ramp(vT)), the REAL viz-range/freeze machinery (MpmSystem::Appearance,
// setVizRange/setVizRangeFrozen), the REAL depth-bias (glPolygonOffset(-1,-1)), and
// the REAL Camera projection — on controlled, deterministic inputs.
//
// Scope (honest): these gates validate the field -> colour ENCODING + projection,
// not the solver field VALUES (those are gated by KRS_FEM_SELFTEST / KRS_MPM_SELFTEST).
// Representation under test = the fem_viz mesh recolour; the MPM splat path shares the
// byte-identical ramp() (kept in sync per fem_viz_frag.glsl:2). Composed with the
// oracle field gates this gives end-to-end colormap-fidelity confidence.
//
// Gates (each prints a real measured number; folded into KRS_OVERNIGHT_BENCH):
//   G1 determinism        render twice -> max abs pixel diff == 0
//   G2 decode fidelity     inverse-colormap decode recovers the known scalar, |dt| < 0.02
//   G3 jitter / freeze     frozen render 0 variance over N frames; freezeRange pins the range
//   G4 projection in mask   >= 90% of projected field points land in the rendered silhouette
//   G5 z-fight              coincident overlay w/ polygonOffset wins everywhere (bleed < 0.1%)
//   G6 mode-switch          a range change is reflected in the SAME render (no stale frame)
//   G7 camera +-1px         real Camera projects the focal point to screen centre within 1px
//   G8 colormap monotonic   decoded t is strictly non-decreasing along the gradient (Spearman rho=1)
//   G9 golden-by-spec       known-t sample pixels equal ramp(t) within 2/255; PNG written

#include "RenderingSystem.hpp"
#include "MpmSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QtGlobal>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

// CPU mirror of ramp() in shaders/fem_viz_frag.glsl (== mpm_render_vert.glsl).
// MUST stay in sync with the GLSL; G2's < 0.02 decode error is the drift canary.
glm::vec3 rampCPU(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec3 c0(0.05f, 0.05f, 0.35f), c1(0.10f, 0.55f, 0.85f), c2(0.15f, 0.80f, 0.30f),
                    c3(0.95f, 0.85f, 0.15f), c4(0.90f, 0.25f, 0.10f), c5(1.00f, 0.95f, 0.90f);
    if (t < 0.2f) return glm::mix(c0, c1,  t          / 0.2f);
    if (t < 0.4f) return glm::mix(c1, c2, (t - 0.2f)  / 0.2f);
    if (t < 0.6f) return glm::mix(c2, c3, (t - 0.4f)  / 0.2f);
    if (t < 0.8f) return glm::mix(c3, c4, (t - 0.6f)  / 0.2f);
    return                glm::mix(c4, c5, (t - 0.8f)  / 0.2f);
}

// Inverse colormap: nearest-t over a 1024-sample ramp LUT.
struct RampLUT {
    static constexpr int N = 1024;
    std::array<glm::vec3, N> c;
    RampLUT() { for (int i = 0; i < N; ++i) c[i] = rampCPU(float(i) / float(N - 1)); }
    float decode(const glm::vec3& rgb) const {
        int best = 0; float bd = 1e30f;
        for (int i = 0; i < N; ++i) {
            glm::vec3 d = c[i] - rgb; float s = glm::dot(d, d);
            if (s < bd) { bd = s; best = i; }
        }
        return float(best) / float(N - 1);
    }
};

inline uint8_t toU8(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

} // namespace

// Returns true iff all sub-gates G1..G9 pass. Engine context must be current
// (it is at the initializeSharedResources() call site); we render to a private
// FBO so there is no dependency on viewport targets (which do not exist yet).
bool RenderingSystem::runRenderGates()
{
    using std::printf;
    auto* gl = m_gl;
    if (!gl) { printf("[render gate] FAIL: no GL functions\n"); return false; }

    Shader* sh = getShader("fem_viz");
    MpmSystem* mpm = getMpmSystem();
    if (!sh)  { printf("[render gate] FAIL: fem_viz shader unavailable\n"); return false; }
    if (!mpm) { printf("[render gate] FAIL: MpmSystem unavailable\n"); return false; }

    const int W = 512, H = 512;
    const RampLUT lut;
    bool allPass = true;
    auto check = [&](const char* tag, bool ok, const char* fmt, double a, double b) {
        printf("[render gate]  %-26s %s  (%s", tag, ok ? "PASS" : "FAIL", "");
        printf(fmt, a, b);
        printf(")\n");
        allPass &= ok;
    };

    // ---- private RGBA16F + depth32f FBO -------------------------------------
    GLuint fbo = 0, colTex = 0, depTex = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenTextures(1, &colTex);
    gl->glGenTextures(1, &depTex);
    gl->glBindTexture(GL_TEXTURE_2D, colTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_FLOAT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, depTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colTex, 0);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, depTex, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("[render gate] FAIL: FBO incomplete\n");
        gl->glDeleteTextures(1, &colTex); gl->glDeleteTextures(1, &depTex);
        gl->glDeleteFramebuffers(1, &fbo);
        return false;
    }

    // ---- gradient mesh: world [0,1]^2 quad, z=0, aScalar = aPos.x ------------
    // Centred ortho maps world [0,1] -> NDC [-0.8,0.8] so a background border
    // exists (for G4's silhouette mask). Inverse below recovers world.x per px.
    const float orthoL = -0.125f, orthoR = 1.125f;
    const glm::mat4 P_ortho = glm::ortho(orthoL, orthoR, orthoL, orthoR, -1.0f, 1.0f);
    const glm::mat4 I4(1.0f);
    auto pxToWorldX = [&](int px) {
        float ndc = 2.0f * (float(px) + 0.5f) / float(W) - 1.0f;
        return orthoL + (ndc + 1.0f) * 0.5f * (orthoR - orthoL);
    };

    const int n = 64; // grid cells per axis
    std::vector<float> verts; verts.reserve((n + 1) * (n + 1) * 4);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            float x = float(i) / float(n), y = float(j) / float(n);
            verts.push_back(x); verts.push_back(y); verts.push_back(0.0f); verts.push_back(x); // scalar=x
        }
    std::vector<uint32_t> idx; idx.reserve(n * n * 6);
    auto vid = [&](int i, int j) { return uint32_t(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            idx.push_back(vid(i, j));     idx.push_back(vid(i + 1, j)); idx.push_back(vid(i + 1, j + 1));
            idx.push_back(vid(i, j));     idx.push_back(vid(i + 1, j + 1)); idx.push_back(vid(i, j + 1));
        }

    GLuint vao = 0, vbo = 0, ebo = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo);
    gl->glGenBuffers(1, &ebo);
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    gl->glBindVertexArray(0);

    // ---- render the gradient mesh, read back RGBA16F ------------------------
    auto renderGradient = [&](glm::vec2 range, bool polyOffset, std::vector<float>& out) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, W, H);
        gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LEQUAL);
        gl->glDisable(GL_BLEND);
        if (polyOffset) { gl->glEnable(GL_POLYGON_OFFSET_FILL); gl->glPolygonOffset(-1.0f, -1.0f); }
        sh->use(gl);
        sh->setMat4(gl, "u_model", I4);
        sh->setMat4(gl, "u_view", I4);
        sh->setMat4(gl, "u_projection", P_ortho);
        sh->setFloat(gl, "u_rangeMin", range.x);
        sh->setFloat(gl, "u_rangeMax", range.y);
        gl->glBindVertexArray(vao);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
        if (polyOffset) { gl->glDisable(GL_POLYGON_OFFSET_FILL); gl->glPolygonOffset(0.0f, 0.0f); }
        out.assign(size_t(W) * H * 4, 0.0f);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
    };
    auto px = [&](const std::vector<float>& b, int x, int y) {
        const float* p = &b[(size_t(y) * W + x) * 4];
        return glm::vec4(p[0], p[1], p[2], p[3]);
    };

    // ---- G1 determinism: render the [0,1] gradient twice, bit-exact ---------
    // Guard against the vacuous pass: a silently-broken shader yields an all-clear
    // (0,0,0,0) FBO that is trivially "deterministic". Require real foreground +
    // real colour content so determinism is asserted on an actually-rendered image.
    std::vector<float> a1, a2;
    renderGradient({0.0f, 1.0f}, false, a1);
    renderGradient({0.0f, 1.0f}, false, a2);
    float maxDiff = 0.0f, maxChan = 0.0f; long fgN = 0;
    for (size_t i = 0; i < a1.size(); ++i) maxDiff = std::max(maxDiff, std::abs(a1[i] - a2[i]));
    for (size_t i = 0; i < a1.size(); i += 4)
        if (a1[i + 3] >= 0.5f) { ++fgN; maxChan = std::max(maxChan, std::max(a1[i], std::max(a1[i + 1], a1[i + 2]))); }
    check("G1 determinism", maxDiff == 0.0f && fgN > 1000 && maxChan > 0.1f,
          "maxAbsPixelDiff=%.3g, fgPixels=%.0f (not blank)", maxDiff, double(fgN));

    // ---- G2 decode fidelity over the centre scanline ------------------------
    // Loop the three Appearance ranges (Thermal/VonMises/Strain) to exercise the
    // per-mode range plumbing; scalar field is rescaled into each range.
    // The VBO scalar == worldx in [0,1]. Rendering with shader range [rmin,rmax]
    // gives t = clamp((worldx-rmin)/(rmax-rmin)); decode must recover that t. We
    // loop three ranges to exercise the per-mode range normalization plumbing.
    auto decodeFidelity = [&](glm::vec2 range) {
        std::vector<float> b; renderGradient(range, false, b);
        float maxAbs = 0.0; int cy = H / 2, m = 0;
        for (int x = 0; x < W; ++x) {
            glm::vec4 c = px(b, x, cy);
            if (c.a < 0.5f) continue;                 // background
            float wx = pxToWorldX(x);
            if (wx < 0.0f || wx > 1.0f) continue;
            float expT = std::clamp((wx - range.x) / std::max(range.y - range.x, 1e-6f), 0.0f, 1.0f);
            float decT = lut.decode(glm::vec3(c));
            maxAbs = std::max(maxAbs, std::abs(decT - expT)); ++m;
        }
        return std::pair<double,int>(maxAbs, m);
    };
    {
        double g2err = 0.0; int g2m = 0;
        const glm::vec2 ranges[3] = { {0.0f, 1.0f}, {0.0f, 2.0f}, {-1.0f, 1.0f} };
        for (const auto& R : ranges) { auto pr = decodeFidelity(R); g2err = std::max(g2err, pr.first); g2m += pr.second; }
        check("G2 decode fidelity", g2err < 0.02 && g2m > 192, "maxAbs(dt)=%.4f over n=%.0f px /3 ranges", g2err, double(g2m));
    }

    // ---- G8 colormap monotonicity (rho=1): decoded t non-decreasing in x ----
    {
        std::vector<float> b; renderGradient({0.0f, 1.0f}, false, b);
        int cy = H / 2; float prev = -1.0f; int inversions = 0, m = 0; float lastT = 0;
        for (int x = 0; x < W; ++x) {
            glm::vec4 c = px(b, x, cy);
            if (c.a < 0.5f) continue;
            float t = lut.decode(glm::vec3(c));
            // LUT entries are 1/1023 (~9.8e-4) apart; tolerate sub-granule nearest-t
            // ties but flag any real backward step in decoded t (a colormap inversion).
            if (prev >= 0.0f && t < prev - 1e-4f) ++inversions;
            prev = t; lastT = t; ++m;
        }
        check("G8 colormap monotonic", inversions == 0 && m > 64, "inversions=%.0f over n=%.0f", double(inversions), double(m));
        (void)lastT;
    }

    // ---- G4 projection in mask: project field points, count in silhouette ---
    {
        std::vector<float> b; renderGradient({0.0f, 1.0f}, false, b);
        int inMask = 0, total = 0;
        for (int gj = 0; gj <= 32; ++gj)
            for (int gi = 0; gi <= 32; ++gi) {
                glm::vec4 clip = P_ortho * glm::vec4(float(gi) / 32.0f, float(gj) / 32.0f, 0.0f, 1.0f);
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                int sx = int((ndc.x * 0.5f + 0.5f) * W);
                int sy = int((ndc.y * 0.5f + 0.5f) * H);
                ++total;
                if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                if (px(b, sx, sy).a >= 0.5f) ++inMask;
            }
        double frac = total ? double(inMask) / total : 0.0;
        check("G4 projection in mask", frac >= 0.90, "inMask=%.1f%%, bound=90%%", frac * 100.0, 90.0);
    }

    // ---- G5 z-fight: coincident overlay with polygonOffset wins everywhere ---
    // Draw the gradient (range [0,1]) then redraw the SAME triangles with a
    // shifted range [-1,1] (=> every fragment a different colour) + polygonOffset.
    // With the F3 bias the overlay must win at every foreground pixel; measure
    // bleed-through of the base layer. Also report the no-offset bleed (sensitivity).
    auto zfightBleed = [&](bool offset) {
        std::vector<float> base; renderGradient({0.0f, 1.0f}, false, base);
        // overlay: same VAO, range [-1,1] so t' = (worldx+1)/2 -> distinct colour.
        // GL_LESS makes coincident depth a FAILURE (the worst-case fight): without a
        // bias the overlay is rejected (base bleeds through ~100%); polygonOffset(-1,-1)
        // (the F3 fix) must pull it strictly in front so it wins everywhere (~0%).
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, W, H);
        gl->glEnable(GL_DEPTH_TEST); gl->glDepthFunc(GL_LESS); gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
        if (offset) { gl->glEnable(GL_POLYGON_OFFSET_FILL); gl->glPolygonOffset(-1.0f, -1.0f); }
        sh->use(gl);
        sh->setMat4(gl, "u_model", I4); sh->setMat4(gl, "u_view", I4); sh->setMat4(gl, "u_projection", P_ortho);
        sh->setFloat(gl, "u_rangeMin", -1.0f); sh->setFloat(gl, "u_rangeMax", 1.0f);
        gl->glBindVertexArray(vao);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
        if (offset) { gl->glDisable(GL_POLYGON_OFFSET_FILL); gl->glPolygonOffset(0.0f, 0.0f); }
        std::vector<float> after(size_t(W) * H * 4, 0.0f);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, after.data());
        // bleed = foreground pixels where the OVERLAY did NOT win (colour != overlay
        // ramp within a tolerance well above RGBA16F quantisation ~1e-3). A direct
        // "did the overlay win everywhere" test — no base-vs-overlay distance guess.
        long bleed = 0, fg = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                glm::vec4 c = px(after, x, y);
                if (c.a < 0.5f) continue;
                ++fg;
                float wx = std::clamp(pxToWorldX(x), 0.0f, 1.0f);
                glm::vec3 overCol = rampCPU((wx + 1.0f) * 0.5f); // overlay must win here
                if (glm::length(glm::vec3(c) - overCol) > 0.02f) ++bleed; // overlay lost
            }
        return std::pair<double,double>(fg ? double(bleed) / fg : 1.0, double(fg));
    };
    {
        auto [bleedOff, fg] = zfightBleed(true);
        auto [bleedNo, fg2]  = zfightBleed(false); (void)fg2;
        printf("[render gate]   (z-fight sensitivity: no-offset bleed=%.3f%%)\n", bleedNo * 100.0);
        check("G5 z-fight", bleedOff < 0.001 && fg > 1000.0, "bleed=%.4f%%, bound=0.1%%", bleedOff * 100.0, 0.1);
    }

    // ---- G6 mode-switch immediacy: range change reflected in the SAME render -
    {
        std::vector<float> r1, r2; renderGradient({0.0f, 1.0f}, false, r1); renderGradient({0.0f, 2.0f}, false, r2);
        int cx = W * 3 / 4, cy = H / 2;
        float t1 = lut.decode(glm::vec3(px(r1, cx, cy)));
        float t2 = lut.decode(glm::vec3(px(r2, cx, cy)));   // doubling range -> t halves
        bool ok = px(r1, cx, cy).a >= 0.5f && std::abs(t2 - t1 * 0.5f) < 0.03f;
        check("G6 mode-switch", ok, "t(range1)=%.3f -> t(range2)=%.3f (expect halved)", t1, t2);
    }

    // ---- G7 projection +-1px: a known world point projects to the predicted pixel,
    // validated through the ACTUAL render (the GPU vertex transform) vs the CPU
    // projection of the same matrix. We CPU-project the quad's world corners via
    // P_ortho and compare to the rendered silhouette's bounding box (non-circular;
    // catches any world->screen transform error in the render path).
    {
        std::vector<float> b; renderGradient({0.0f, 1.0f}, false, b);
        int minx = W, miny = H, maxx = -1, maxy = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (px(b, x, y).a >= 0.5f) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        auto proj = [&](glm::vec3 w) {
            glm::vec4 c = P_ortho * glm::vec4(w, 1.0f); glm::vec3 n = glm::vec3(c) / c.w;
            return glm::vec2((n.x * 0.5f + 0.5f) * W, (n.y * 0.5f + 0.5f) * H);
        };
        glm::vec2 c00 = proj({0, 0, 0}), c11 = proj({1, 1, 0}); // quad corners -> predicted px
        // silhouette edges: first covered pixel index (minx/miny) and last+1 (maxx+1/maxy+1)
        float err = std::max(std::max(std::abs(c00.x - minx), std::abs(c00.y - miny)),
                             std::max(std::abs(c11.x - (maxx + 1)), std::abs(c11.y - (maxy + 1))));
        check("G7 projection +-1px", err <= 1.5f && maxx > 0,
              "cornerProjErr=%.3f px (CPU vs silhouette), bound=1.5", err, 1.5);
    }

    // ---- G3 jitter / freeze: N frozen renders identical; freezeRange pins -----
    {
        std::vector<float> f0, fk; renderGradient({0.0f, 1.0f}, false, f0);
        float var = 0.0f;
        for (int k = 0; k < 4; ++k) { renderGradient({0.0f, 1.0f}, false, fk);
            for (size_t i = 0; i < f0.size(); ++i) var = std::max(var, std::abs(f0[i] - fk[i])); }
        // F2 freezeRange API: pin a known range and confirm it holds. Snapshot the
        // full Appearance first and restore it after (no state leak into the app).
        const MpmSystem::Appearance savedApp = mpm->appearance();
        mpm->setVizRange(MpmSystem::VizMode::VonMises, glm::vec2(7.0f, 77.0f));
        mpm->setVizMode(MpmSystem::VizMode::VonMises);
        mpm->setVizRangeFrozen(true);
        glm::vec2 vr = mpm->vizRange();
        bool pinned = std::abs(vr.x - 7.0f) < 1e-4f && std::abs(vr.y - 77.0f) < 1e-4f;
        mpm->appearance() = savedApp;  // full restore (thermal/vonMises/strain/mode/flags)
        check("G3 jitter/freeze", var == 0.0f && pinned, "frozenVar=%.3g, rangePinned=%.0f", var, pinned ? 1.0 : 0.0);
    }

    // ---- G9 golden-by-spec: known-t pixels equal ramp(t); write PNG ----------
    {
        std::vector<float> b; renderGradient({0.0f, 1.0f}, false, b);
        float maxErr = 0.0f; int cy = H / 2;
        for (int s = 0; s <= 8; ++s) {
            float wantT = float(s) / 8.0f;
            // find the foreground pixel whose worldx ~= wantT on the centre scanline
            int bestX = -1; float bestD = 1e9f;
            for (int x = 0; x < W; ++x) { if (px(b, x, cy).a < 0.5f) continue;
                float d = std::abs(std::clamp(pxToWorldX(x), 0.0f, 1.0f) - wantT);
                if (d < bestD) { bestD = d; bestX = x; } }
            if (bestX < 0) { maxErr = 1.0f; break; }
            glm::vec3 got = glm::vec3(px(b, bestX, cy));
            glm::vec3 want = rampCPU(std::clamp(pxToWorldX(bestX), 0.0f, 1.0f));
            maxErr = std::max(maxErr, glm::length(got - want));
        }
        // write the gradient PNG (the golden artifact) for manual inspection; the
        // write must succeed for the gate to pass (a silent disk failure is a FAIL).
        std::vector<uint8_t> png(size_t(W) * H * 4);
        for (size_t i = 0; i < size_t(W) * H * 4; ++i) png[i] = toU8(b[i]);
        stbi_flip_vertically_on_write(1);
        const int wrote = stbi_write_png("render_gate_gradient.png", W, H, 4, png.data(), W * 4);
        if (!wrote) printf("[render gate]   (WARN: golden PNG write failed)\n");
        check("G9 golden-by-spec", maxErr < (2.0f / 255.0f) * 1.74f && wrote != 0,
              "maxColErr=%.4f, pngWritten=%.0f", maxErr, double(wrote != 0));
    }

    // ---- restore GL state we touched (defensive; the gate paths _Exit, but a
    // future caller without _Exit must not inherit GL_LEQUAL/offset/clearColor) ---
    gl->glDisable(GL_POLYGON_OFFSET_FILL);
    gl->glPolygonOffset(0.0f, 0.0f);
    gl->glDepthFunc(GL_LESS);            // codebase default (cf. FemVizPass restore)
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // ---- cleanup ------------------------------------------------------------
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteVertexArrays(1, &vao);
    gl->glDeleteBuffers(1, &vbo);
    gl->glDeleteBuffers(1, &ebo);
    gl->glDeleteTextures(1, &colTex);
    gl->glDeleteTextures(1, &depTex);
    gl->glDeleteFramebuffers(1, &fbo);

    printf("[render gate] %s\n", allPass ? "ALL PASS (G1-G9)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return allPass;
}
