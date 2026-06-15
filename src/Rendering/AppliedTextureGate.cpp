// AppliedTextureGate.cpp -- Phase A-CLOSE GATE U (AC1/AC2/AC3): headless PROOF that an APPLIED
// texture rides a UV body in OBJECT space (not world-space triplanar), and that per-body tiling
// scales texel density. This closes the operator-reported bug whose only prior evidence was "look
// in the viewer": applying a pack still swam in world space.
//
// AC3 (CPU, no GL): run the REAL apply (krs::material::applyPackTags) + REAL shader selection
//   (krs::render::selectGBufferShaderKind) -- the single sources of truth shared with
//   TextureBrowserWidget and OpaquePass -- and assert a UV body STAYS on the UV path after a pack
//   is applied (height-map OR plain). NEG control: a no-UV primitive + height pack goes world-space.
//
// AC1 (GL render): a cube with world-scale planar UVs is textured with a UV-ENCODING albedo
//   (texel(s,t) = (s,t,*)), so the rendered albedo at a pixel DECODES the texcoord actually
//   sampled there. Over >=50 random rigid poses, through the real gbuffer_textured path, the
//   decoded texcoord equals the body-frame vertex UV (fract(uv*scale)) -- pose-INVARIANT: the
//   texture is fixed ON the surface. NEG control: the gbuffer_triplanar path decodes f(worldPos),
//   which SLIDES under motion (reported magnitude).
//
// AC2 (GL render): texels/metre measured by finite-differencing decoded-UV vs world position along
//   a face; asserted to scale exactly with the tiling control (albedoTiling.x / u_texture_scale).

#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "GBufferShaderSelect.hpp"
#include "MaterialApply.hpp"

#include <QOpenGLFunctions_4_3_Core>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

const char* kindName(krs::render::GBufferShaderKind k)
{
    using K = krs::render::GBufferShaderKind;
    switch (k) {
        case K::Untextured:           return "Untextured";
        case K::UVTextured:           return "UVTextured(object-space)";
        case K::Triplanar:            return "Triplanar(world-space)";
        case K::Tessellated:          return "Tessellated";
        case K::TessellatedTriplanar: return "TessellatedTriplanar(world-space)";
        case K::ParallaxPOM:          return "ParallaxPOM(world-space)";
    }
    return "?";
}

// torus distance on [0,1)^2 -- the correct metric for fract-encoded UVs (handles the 1->0 wrap;
// recovers the true step exactly as long as the step is < 0.5 per component).
float torusDist(glm::vec2 a, glm::vec2 b)
{
    glm::vec2 d = glm::abs(a - b);
    d.x = std::min(d.x, 1.0f - d.x);
    d.y = std::min(d.y, 1.0f - d.y);
    return glm::length(d);
}

struct Mesh { std::vector<Vertex> v; std::vector<unsigned> idx; };

// A subdivided unit cube (side 2h) with WORLD-SCALE planar UVs derived from LOCAL in-plane coords
// (1 uv unit == 1 metre), so fract(uv*scale) decodes cleanly and d(uv)/d(world) == 1 along a face.
Mesh buildSubdividedCube(int N, float h)
{
    Mesh m;
    struct Face { glm::vec3 n, U, V; };
    const Face faces[6] = {
        {{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{ 0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{ 0, 0,-1}, {1, 0, 0}, {0, 1, 0}},
    };
    for (const auto& f : faces) {
        const unsigned base = unsigned(m.v.size());
        for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i) {
                const float s = float(i) / N, t = float(j) / N;
                const glm::vec3 pos = f.n * h + f.U * ((s * 2 - 1) * h) + f.V * ((t * 2 - 1) * h);
                Vertex vert;
                vert.position = pos;
                vert.normal = f.n;
                // world-scale planar UV from local in-plane coords, offset to [0,1]
                vert.uv = glm::vec2(glm::dot(pos, f.U) + h, glm::dot(pos, f.V) + h);
                vert.tangent = f.U;
                vert.bitangent = f.V;
                m.v.push_back(vert);
            }
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                const unsigned a = base + j * (N + 1) + i, b = a + 1, c = a + (N + 1), d = c + 1;
                m.idx.insert(m.idx.end(), { a, b, c, b, d, c });
            }
    }
    return m;
}

} // namespace

// =====================================================================================
// AC3 -- CPU tag/selection gate (no GL). Runs the shared apply + selection functions.
// =====================================================================================
bool RenderingSystem::runApplyTagGateAC3()
{
    using namespace krs::render;
    using krs::material::applyPackTags;
    std::printf("[applytex] AC3 tag/selection (shared applyPackTags + selectGBufferShaderKind):\n");

    entt::registry reg;
    auto kindOf = [&](entt::entity e) {
        return selectGBufferShaderKind(
            reg.all_of<UVTexturedMaterialTag>(e),
            reg.all_of<TessellatedMaterialTag>(e),
            reg.all_of<TriPlanarMaterialTag>(e),
            reg.all_of<ParallaxMaterialTag>(e),
            /*hasTexture*/ true); // an applied pack always brings an albedo map
    };

    // A real-UV body (imported CAD): UVTexturedMaterialTag.
    auto uvBody = reg.create();
    reg.emplace<UVTexturedMaterialTag>(uvBody);

    // Apply a HEIGHT-MAP (parallax) pack -> must STAY on the UV path.
    applyPackTags(reg, uvBody, /*packHasHeightMap*/ true);
    const auto kUVh = kindOf(uvBody);
    const bool keptUV_h = reg.all_of<UVTexturedMaterialTag>(uvBody)
                       && !reg.all_of<TriPlanarMaterialTag>(uvBody)
                       && kUVh == GBufferShaderKind::UVTextured;

    // Apply a PLAIN pack -> must stay on the UV path, parallax removed.
    applyPackTags(reg, uvBody, /*packHasHeightMap*/ false);
    const auto kUVp = kindOf(uvBody);
    const bool keptUV_p = reg.all_of<UVTexturedMaterialTag>(uvBody)
                       && !reg.all_of<ParallaxMaterialTag>(uvBody)
                       && kUVp == GBufferShaderKind::UVTextured;

    // NEGATIVE CONTROL: a primitive with NO UVs (TriPlanarMaterialTag) + a height pack -> must take
    // the WORLD-SPACE parallax/triplanar path. Proves the gate distinguishes UV from world space
    // (a broken policy that forced UV bodies world-space would also force this one, but if THIS
    // failed to be world-space the gate would be vacuous).
    auto prim = reg.create();
    reg.emplace<TriPlanarMaterialTag>(prim);
    applyPackTags(reg, prim, /*packHasHeightMap*/ true);
    const auto kPrim = kindOf(prim);
    const bool primWorld = isWorldSpaceKind(kPrim);

    std::printf("[applytex]   UV body + height pack -> %-32s (keep UV, no triplanar) %s\n",
                kindName(kUVh), keptUV_h ? "PASS" : "FAIL");
    std::printf("[applytex]   UV body + plain  pack -> %-32s (keep UV, parallax off) %s\n",
                kindName(kUVp), keptUV_p ? "PASS" : "FAIL");
    std::printf("[applytex]   NEG-CTRL prim(no UV)+height -> %-26s (world-space)     %s\n",
                kindName(kPrim), primWorld ? "REJECTS(non-vacuous)" : "VACUOUS!");
    std::fflush(stdout);
    return keptUV_h && keptUV_p && primWorld;
}

// =====================================================================================
// AC1/AC2 -- GL render gate: UV-encoding albedo, decode the sampled texcoord per pixel.
// =====================================================================================
bool RenderingSystem::runAppliedTextureGate()
{
    using std::printf;
    printf("[applytex] GATE U AC1/AC2/AC3 -- applied texture rides UV body + tiling scales\n");

    // ---- AC3 first (CPU, independent of GL) ----
    const bool ac3 = runApplyTagGateAC3();

    auto* gl = m_gl;
    if (!gl) { printf("[applytex] FAIL: no GL functions\n"); return false; }
    Shader* texturedSh = getShader("gbuffer_textured");
    Shader* triplanarSh = getShader("gbuffer_triplanar");
    if (!texturedSh || !triplanarSh) { printf("[applytex] FAIL: gbuffer_textured/triplanar unavailable\n"); return false; }

    const int W = 640, H = 640;
    const int N = 20;
    const float h = 0.5f;
    const Mesh mesh = buildSubdividedCube(N, h);

    // ---- camera framing 3 faces ----
    const glm::vec3 center(0.0f);
    const glm::vec3 eye = center + glm::normalize(glm::vec3(1.0f, 0.45f, 0.55f)) * 3.0f;
    const glm::mat4 V = glm::lookAt(eye, center, glm::vec3(0, 1, 0));
    const glm::mat4 P = glm::perspective(glm::radians(42.0f), float(W) / float(H), 0.05f, 50.0f);
    const glm::mat4 PV = P * V;
    auto proj = [&](glm::vec3 w) {
        glm::vec4 c = PV * glm::vec4(w, 1.0f);
        glm::vec3 n = glm::vec3(c) / c.w;
        return glm::vec2((n.x * 0.5f + 0.5f) * W, (n.y * 0.5f + 0.5f) * H);
    };

    // ---- UV-encoding albedo texture: texel(i,j) = (i/(T-1), j/(T-1), 0). NEAREST + REPEAT.
    // NEAREST (not LINEAR) is essential: LINEAR blends across the REPEAT seam (texel T-1 ~1.0 vs
    // texel 0 ~0.0), so a fragment whose uv lands on the seam decodes ~0.5 instead of ~0/1 -- a
    // filtering artifact, not a real texcoord. NEAREST reads the exact ramp texel (decode error <=
    // 0.5/T), and the seam jumps cleanly 1.0<->0.0 (torusDist handles the wrap). ----
    const int T = 2048;
    std::vector<float> uvtex(size_t(T) * T * 4);
    for (int j = 0; j < T; ++j)
        for (int i = 0; i < T; ++i) {
            float* p = &uvtex[(size_t(j) * T + i) * 4];
            p[0] = float(i) / (T - 1);
            p[1] = float(j) / (T - 1);
            p[2] = 0.0f; p[3] = 1.0f;
        }
    GLuint encTex = 0;
    gl->glGenTextures(1, &encTex);
    gl->glBindTexture(GL_TEXTURE_2D, encTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, T, T, 0, GL_RGBA, GL_FLOAT, uvtex.data());
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // ---- flat normal map (tangent-space (0,0,1)) bound to material.normalMap, so gNormal carries
    // the GEOMETRIC world normal (not a perturbed one). Used to reject edge pixels that read a
    // perpendicular adjacent face (the source of the residual outliers). ----
    const float flat[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    GLuint flatNrm = 0;
    gl->glGenTextures(1, &flatNrm);
    gl->glBindTexture(GL_TEXTURE_2D, flatNrm);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, flat);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // ---- MRT FBO: attach0 = gPosition (world), attach1 = gNormal, attach2 = gAlbedoAO (decoded uv) ----
    GLuint fbo = 0, tex[3] = { 0,0,0 }, dep = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenTextures(3, tex);
    gl->glGenTextures(1, &dep);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (int a = 0; a < 3; ++a) {
        gl->glBindTexture(GL_TEXTURE_2D, tex[a]);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_FLOAT, nullptr);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + a, GL_TEXTURE_2D, tex[a], 0);
    }
    gl->glBindTexture(GL_TEXTURE_2D, dep);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("[applytex] FAIL: FBO incomplete\n");
        gl->glDeleteTextures(3, tex); gl->glDeleteTextures(1, &dep);
        gl->glDeleteTextures(1, &encTex); gl->glDeleteFramebuffers(1, &fbo);
        return false;
    }

    // ---- upload the cube ----
    GLuint vao = 0, vbo = 0, ebo = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo);
    gl->glGenBuffers(1, &ebo);
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.v.size() * sizeof(Vertex)), mesh.v.data(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mesh.idx.size() * sizeof(unsigned)), mesh.idx.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(Vertex);
    gl->glEnableVertexAttribArray(0); gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, position));
    gl->glEnableVertexAttribArray(1); gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
    gl->glEnableVertexAttribArray(2); gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));
    gl->glEnableVertexAttribArray(3); gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tangent));
    gl->glEnableVertexAttribArray(4); gl->glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, bitangent));
    gl->glBindVertexArray(0);

    std::vector<float> posBuf(size_t(W) * H * 4), albBuf(size_t(W) * H * 4), nrmBuf(size_t(W) * H * 4);
    auto render = [&](Shader* sh, const glm::mat4& M, float scale,
                      std::vector<float>& outPos, std::vector<float>& outAlb, std::vector<float>& outNrm) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, W, H);
        GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        gl->glDrawBuffers(3, bufs);
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LEQUAL);
        gl->glDisable(GL_BLEND);
        gl->glDisable(GL_CULL_FACE);
        sh->use(gl);
        sh->setMat4(gl, "view", V);
        sh->setMat4(gl, "projection", P);
        sh->setMat4(gl, "model", M);
        sh->setFloat(gl, "u_texture_scale", scale);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, encTex);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, flatNrm);
        sh->setInt(gl, "material.albedoMap", 0);
        sh->setInt(gl, "material.normalMap", 1);    // flat -> gNormal = geometric world normal
        sh->setInt(gl, "material.aoMap", 0);
        sh->setInt(gl, "material.metallicMap", 0);
        sh->setInt(gl, "material.roughnessMap", 0);
        sh->setInt(gl, "material.emissiveMap", 0);
        gl->glBindVertexArray(vao);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh.idx.size()), GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0); gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, outPos.data());
        gl->glReadBuffer(GL_COLOR_ATTACHMENT1); gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, outNrm.data());
        gl->glReadBuffer(GL_COLOR_ATTACHMENT2); gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, outAlb.data());
        gl->glActiveTexture(GL_TEXTURE0);
    };
    auto pxAt = [&](const std::vector<float>& b, int x, int y) {
        const float* p = &b[(size_t(y) * W + x) * 4];
        return glm::vec4(p[0], p[1], p[2], p[3]);
    };
    // If vertex i (body-frame) is front-facing AND covered AND the surface read back at its pixel
    // is THIS vertex, returns true and the decoded texcoord sampled there.
    auto sampleVertex = [&](const std::vector<float>& posB, const std::vector<float>& albB,
                            const std::vector<float>& nrmB, const glm::mat4& M, const glm::mat3& R,
                            const Vertex& vert, glm::vec2& decoded) -> bool {
        const glm::vec3 w = glm::vec3(M * glm::vec4(vert.position, 1.0f));
        const glm::vec3 nW = glm::normalize(R * vert.normal);
        if (glm::dot(nW, glm::normalize(eye - w)) <= 0.2f) return false; // back-facing / grazing
        const glm::vec2 pp = proj(w);
        const int x = int(std::lround(pp.x)), y = int(std::lround(pp.y));
        if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1) return false;
        const glm::vec4 gp = pxAt(posB, x, y);
        if (gp.w < 0.5f) return false;                       // not covered
        if (glm::length(glm::vec3(gp) - w) > 0.015f) return false; // a different (nearer) surface here
        // reject edge pixels that read a perpendicular adjacent face (different UV chart): the
        // read-back geometric normal must match this vertex's face normal.
        const glm::vec4 gn = pxAt(nrmB, x, y);
        if (glm::dot(glm::normalize(glm::vec3(gn)), nW) < 0.9f) return false;
        const glm::vec4 ga = pxAt(albB, x, y);
        decoded = glm::vec2(ga.x, ga.y);
        return true;
    };

    // ============================ AC1: rides body over >=50 random poses ============================
    std::mt19937 rng(1234567u);                              // fixed seed -> deterministic
    std::uniform_real_distribution<float> uA(-1.0f, 1.0f), uT(-0.25f, 0.25f), uAng(0.0f, 6.2831853f);
    const float scale1 = 1.0f;
    const int POSES = 60;
    std::vector<float> residuals;
    residuals.reserve(60000);
    for (int s = 0; s < POSES; ++s) {
        glm::vec3 axis(uA(rng), uA(rng), uA(rng));
        if (glm::length(axis) < 1e-3f) axis = glm::vec3(0, 1, 0);
        const glm::quat q = glm::angleAxis(uAng(rng), glm::normalize(axis));
        const glm::vec3 t(uT(rng), uT(rng), uT(rng));
        const glm::mat4 M = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q);
        const glm::mat3 R = glm::mat3(M);
        render(texturedSh, M, scale1, posBuf, albBuf, nrmBuf);
        for (size_t i = 0; i < mesh.v.size(); ++i) {
            glm::vec2 dec;
            if (!sampleVertex(posBuf, albBuf, nrmBuf, M, R, mesh.v[i], dec)) continue;
            const glm::vec2 expected = glm::fract(mesh.v[i].uv * scale1);
            residuals.push_back(torusDist(dec, expected));
        }
    }
    const long hitCount = long(residuals.size());
    double sumR = 0.0; float maxR = 0.0f;
    for (float r : residuals) { sumR += r; maxR = std::max(maxR, r); }
    const float meanResidual = hitCount ? float(sumR / hitCount) : 1e9f;
    float p99 = 1e9f;
    if (hitCount) {
        const size_t k = size_t(0.99 * (hitCount - 1));
        std::nth_element(residuals.begin(), residuals.begin() + k, residuals.end());
        p99 = residuals[k];
    }
    // PASS bar: in EVERY random pose the decoded texcoord matches the body-frame vertex UV -- the
    // texture is fixed ON the surface. Gated on mean + 99th-percentile (robust to the handful of
    // silhouette/edge pixels that read a neighbouring face); max reported. NEAREST encoding removes
    // the LINEAR seam artifact, so residual is just texcoord-interp + readback quantization.
    const bool ridesBody = hitCount > 5000 && meanResidual < 0.015f && p99 < 0.03f;
    printf("[applytex]  AC1 rides body (UV path, %d random poses): mean=%.4f p99=%.4f max=%.4f over %ld samples (mean<0.015,p99<0.03)  %s\n",
           POSES, meanResidual, p99, maxR, hitCount, ridesBody ? "PASS" : "FAIL");

    // ---- NEG CONTROL: triplanar path SLIDES under motion. Two poses; average the per-vertex
    //      decoded-texcoord shift for vertices visible in BOTH. UV path ~0; triplanar large. ----
    auto twoPoseMeanSlide = [&](Shader* sh, const glm::mat4& MA, const glm::mat4& MB) -> std::pair<float, long> {
        std::vector<float> pA(size_t(W) * H * 4), aA(size_t(W) * H * 4), nA(size_t(W) * H * 4);
        std::vector<float> pB(size_t(W) * H * 4), aB(size_t(W) * H * 4), nB(size_t(W) * H * 4);
        render(sh, MA, scale1, pA, aA, nA);
        render(sh, MB, scale1, pB, aB, nB);
        const glm::mat3 RA = glm::mat3(MA), RB = glm::mat3(MB);
        double sum = 0.0; long n = 0;
        for (size_t i = 0; i < mesh.v.size(); ++i) {
            glm::vec2 dA, dB;
            if (!sampleVertex(pA, aA, nA, MA, RA, mesh.v[i], dA)) continue;
            if (!sampleVertex(pB, aB, nB, MB, RB, mesh.v[i], dB)) continue;
            sum += torusDist(dA, dB); ++n;
        }
        return { n ? float(sum / n) : 0.0f, n };
    };
    const glm::mat4 MA = glm::mat4(1.0f);
    const glm::mat4 MB = glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(0, 1, 0));
    auto [slideUV, nUV] = twoPoseMeanSlide(texturedSh, MA, MB);
    auto [slideTri, nTri] = twoPoseMeanSlide(triplanarSh, MA, MB);
    const bool negCtrlBites = slideUV < 0.02f && slideTri > 0.10f && nUV > 200 && nTri > 200;
    printf("[applytex]  AC1 neg-ctrl (35deg yaw): UV-path slide=%.4f (n=%ld, fixed) vs triplanar slide=%.4f (n=%ld, SWIMS)  %s\n",
           slideUV, nUV, slideTri, nTri, negCtrlBites ? "REJECTS(non-vacuous)" : "VACUOUS!");

    // ============================ AC2: tiling scales texel density ============================
    // texels/metre = torusDist(decoded_i, decoded_j) / worldDist, finite-differenced along a face
    // (adjacent grid vertices share a UV row -> step along one UV axis). Equals u_texture_scale.
    auto measureTexelsPerM = [&](float scale) -> double {
        render(texturedSh, MA, scale, posBuf, albBuf, nrmBuf);
        const glm::mat3 R = glm::mat3(MA);
        std::vector<float> ratios;
        for (int f = 0; f < 6; ++f) {
            const unsigned base = unsigned(f) * unsigned((N + 1) * (N + 1));
            for (int j = 0; j <= N; ++j)
                for (int i = 0; i < N; ++i) {       // pair (i, i+1) in the same row -> step along U
                    const Vertex& va = mesh.v[base + j * (N + 1) + i];
                    const Vertex& vb = mesh.v[base + j * (N + 1) + i + 1];
                    glm::vec2 da, db;
                    if (!sampleVertex(posBuf, albBuf, nrmBuf, MA, R, va, da)) continue;
                    if (!sampleVertex(posBuf, albBuf, nrmBuf, MA, R, vb, db)) continue;
                    const float worldD = glm::length(va.position - vb.position);
                    if (worldD < 1e-4f) continue;
                    ratios.push_back(torusDist(da, db) / worldD);
                }
        }
        if (ratios.empty()) return 0.0;
        // MEDIAN texels/m: robust to the few edge/occluded pairs that read a wrong surface.
        const size_t mid = ratios.size() / 2;
        std::nth_element(ratios.begin(), ratios.begin() + mid, ratios.end());
        return double(ratios[mid]);
    };
    const double tpm1 = measureTexelsPerM(1.0f);
    const double tpm2 = measureTexelsPerM(2.0f);
    const double tpm4 = measureTexelsPerM(4.0f);
    const double err1 = std::abs(tpm1 - 1.0) / 1.0;
    const double r2 = tpm2 / std::max(1e-9, tpm1), r4 = tpm4 / std::max(1e-9, tpm1);
    const bool tilingOk = err1 < 0.01 && std::abs(r2 - 2.0) < 0.02 && std::abs(r4 - 4.0) < 0.04;
    printf("[applytex]  AC2 tiling texels/m (median): scale1=%.4f (err%.2f%%) scale2=%.4f (x%.3f) scale4=%.4f (x%.3f)  %s\n",
           tpm1, err1 * 100.0, tpm2, r2, tpm4, r4, tilingOk ? "PASS" : "FAIL");

    // cleanup
    gl->glDeleteVertexArrays(1, &vao);
    gl->glDeleteBuffers(1, &vbo); gl->glDeleteBuffers(1, &ebo);
    gl->glDeleteTextures(3, tex); gl->glDeleteTextures(1, &dep);
    gl->glDeleteTextures(1, &encTex); gl->glDeleteTextures(1, &flatNrm);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const bool pass = ac3 && ridesBody && negCtrlBites && tilingOk;
    printf("[applytex] %s  (AC3 %s, AC1 rides %s, AC1 neg-ctrl %s, AC2 tiling %s)\n",
           pass ? "ALL PASS" : "FAILURES PRESENT",
           ac3 ? "ok" : "FAIL", ridesBody ? "ok" : "FAIL",
           negCtrlBites ? "ok" : "FAIL", tilingOk ? "ok" : "FAIL");
    std::fflush(stdout);
    return pass;
}
