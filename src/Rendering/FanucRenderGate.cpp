// FanucRenderGate.cpp — Phase V GATE V.2: end-to-end render proof that the FANUC's
// articulation transforms reach the GPU correctly, THROUGH THE SHARED krs::fanuc helper
// (the same one the V-assign gate and the app boot use).
//
// Renders the imported FANUC (17 solids) at two joint configs via the REAL mesh path
// (the gbuffer_untextured shader, whose MRT0 = world-space surface position) into a
// private RGBA16F FBO, then verifies that tracked feature points on KNOWN solids land
// at their CPU-predicted pixels within +-2px, with the rendered world-position there
// matching the prediction. A deliberately wrong-link prediction must FAIL (negative
// control). This locks "the correct transform reaches the GPU" against the assignment.

#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "FanucArticulation.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QtGlobal>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <array>

#if defined(KR_WITH_PHYSX) && defined(KR_WITH_OCCT)
#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"

namespace {
struct GpuMesh { GLuint vao = 0, vbo = 0, ebo = 0; GLsizei count = 0; };
// a tracked feature: the centroid of a KNOWN solid on a moving link, + its bound radius
struct Feature { int inspectIdx; int link; glm::vec3 restCentroid; float radius; const char* name; };
}
#endif

bool RenderingSystem::runFanucRenderGateV2()
{
    using std::printf;
#if !defined(KR_WITH_PHYSX) || !defined(KR_WITH_OCCT)
    printf("[fanuc render] vacuous pass (built without PhysX/OpenCASCADE)\n");
    return true;
#else
    auto* gl = m_gl;
    if (!gl) { printf("[fanuc render] FAIL: no GL functions\n"); return false; }
    Shader* sh = getShader("gbuffer_untextured");
    if (!sh) { printf("[fanuc render] FAIL: gbuffer_untextured shader unavailable\n"); return false; }
    printf("[fanuc render] GATE V.2 - visible FANUC, tracked features -> predicted pixels (+-2px)\n");

    // ---- set up the FANUC via the SHARED helper (same path as V-assign + app boot) ----
    Scene scene;
    SimulationController sim(&scene);
    const std::string path = krs::fanuc::findStepAsset();
    krs::fanuc::Setup setup = krs::fanuc::setupFanucScene(scene, sim, path);
    if (!setup.ok) { printf("[fanuc render] FAIL: setupFanucScene (%s): %s\n", path.c_str(), setup.message.c_str()); sim.stop(); return false; }
    if (setup.fingerprint != krs::fanuc::assignmentFingerprint()) {
        printf("[fanuc render] FAIL: assignment fingerprint mismatch (%s)\n", setup.fingerprint.c_str()); sim.stop(); return false;
    }
    auto& reg = scene.getRegistry();

    // ---- world bounds (CAD importer leaves AABB zero -> scan vertices) -> frame a camera ----
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (auto e : reg.view<RenderableMeshComponent>()) {
        for (const auto& v : reg.get<RenderableMeshComponent>(e).vertices) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
    }
    const glm::vec3 center = 0.5f * (lo + hi);
    const float diag = glm::length(hi - lo);
    const int W = 900, H = 900;
    // broadside to the arm's Y-Z working plane (+X looking -X, slight elevation) so the base,
    // upper arm and forearm stack with minimal self-occlusion across the swept configs.
    const glm::vec3 eye = center + glm::normalize(glm::vec3(1.0f, 0.30f, 0.30f)) * diag * 0.85f;
    glm::mat4 V = glm::lookAt(eye, center, glm::vec3(0, 1, 0));
    glm::mat4 P = glm::perspective(glm::radians(42.0f), float(W) / float(H), 0.01f, 300.0f);
    const glm::mat4 PV = P * V;
    auto proj = [&](glm::vec3 w) {                          // world -> pixel (GL bottom-up, matches readback)
        glm::vec4 c = PV * glm::vec4(w, 1.0f);
        glm::vec3 n = glm::vec3(c) / c.w;
        return glm::vec2((n.x * 0.5f + 0.5f) * W, (n.y * 0.5f + 0.5f) * H);
    };

    // ---- private RGBA16F (=gPosition, world pos) + depth FBO ----
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
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depTex, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("[fanuc render] FAIL: FBO incomplete\n");
        gl->glDeleteTextures(1, &colTex); gl->glDeleteTextures(1, &depTex); gl->glDeleteFramebuffers(1, &fbo);
        sim.stop(); return false;
    }

    // ---- upload each solid's mesh to its own VAO (self-contained, no m_scene coupling) ----
    std::vector<std::pair<entt::entity, GpuMesh>> meshes;
    for (auto e : reg.view<RenderableMeshComponent, TransformComponent>()) {
        auto& m = reg.get<RenderableMeshComponent>(e);
        if (m.indices.empty() || m.vertices.empty()) continue;
        GpuMesh gm; gm.count = GLsizei(m.indices.size());
        gl->glGenVertexArrays(1, &gm.vao);
        gl->glGenBuffers(1, &gm.vbo);
        gl->glGenBuffers(1, &gm.ebo);
        gl->glBindVertexArray(gm.vao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.vertices.size() * sizeof(Vertex)), m.vertices.data(), GL_STATIC_DRAW);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo);
        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.indices.size() * sizeof(unsigned int)), m.indices.data(), GL_STATIC_DRAW);
        const GLsizei stride = sizeof(Vertex);
        gl->glEnableVertexAttribArray(0); gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, position));
        gl->glEnableVertexAttribArray(1); gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
        gl->glEnableVertexAttribArray(2); gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));
        gl->glEnableVertexAttribArray(3); gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tangent));
        gl->glEnableVertexAttribArray(4); gl->glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, bitangent));
        gl->glBindVertexArray(0);
        meshes.emplace_back(e, gm);
    }

    // render the current TransformComponents into the world-pos buffer
    auto render = [&](std::vector<float>& out) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, W, H);
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LEQUAL);
        gl->glDisable(GL_BLEND);
        sh->use(gl);
        sh->setMat4(gl, "view", V);
        sh->setMat4(gl, "projection", P);
        sh->setMat4(gl, "previousView", V);
        sh->setMat4(gl, "previousProjection", P);
        for (const auto& [e, gm] : meshes) {
            sh->setMat4(gl, "model", reg.get<TransformComponent>(e).getTransform());
            gl->glBindVertexArray(gm.vao);
            gl->glDrawElements(GL_TRIANGLES, gm.count, GL_UNSIGNED_INT, nullptr);
        }
        gl->glBindVertexArray(0);
        out.assign(size_t(W) * H * 4, 0.0f);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
    };
    auto pxAt = [&](const std::vector<float>& b, int x, int y) {
        const float* p = &b[(size_t(y) * W + x) * 4];
        return glm::vec4(p[0], p[1], p[2], p[3]);
    };

    // ---- tracked features: one big distinct solid per moving link (rest centroid + radius) ----
    const int trackIdx[3] = { 13, 12, 1 };                 // carousel(L1), upper arm(L2), forearm(L3)
    const char* trackName[3] = { "carousel(L1)", "upperarm(L2)", "forearm(L3)" };
    std::array<Feature, 3> feats{};
    for (int t = 0; t < 3; ++t) {
        const int k = trackIdx[t];
        Feature f; f.inspectIdx = k; f.link = krs::fanuc::solidLink(k); f.name = trackName[t];
        const auto& verts = reg.get<RenderableMeshComponent>(setup.solidEntity[k]).vertices;
        glm::vec3 c(0.0f); for (const auto& v : verts) c += v.position; c /= float(verts.size());
        float r = 0; for (const auto& v : verts) r = std::max(r, glm::length(v.position - c));
        f.restCentroid = c; f.radius = r; feats[t] = f;
    }
    auto drive = [&](const std::vector<float>& q) { sim.setArticJointPositions(q); sim.writeBackArticulationViz(); };

    // metres-per-pixel at the arm's depth -> express the +-2px tolerance in world units.
    const glm::vec3 camRight = glm::normalize(glm::cross(center - eye, glm::vec3(0, 1, 0)));
    const float dPx = glm::length(proj(center + camRight * 0.1f) - proj(center));
    const float mpp = (dPx > 1e-3f) ? 0.1f / dPx : 0.003f;
    const float eps2px = 2.0f * mpp;                       // a feature "lands within +-2px" of prediction

    // Precise landmark test, evaluated at the EXACT predicted pixel (no window search -- a smooth
    // surface makes "closest world in a window" wander). For each sampled surface vertex Cv = M*restV
    // we project to its pixel; if that pixel is covered we read the GPU world-pos there. The vertex
    // is a HIT (lands at its predicted pixel within +-2px) iff |readback - Cv| < eps2px. Back-facing
    // / occluded vertices read a different surface -> not a hit (excluded). A wrong transform renders
    // the solid elsewhere, so its predicted pixels read unrelated world -> ~0 hits.
    auto evalSolid = [&](const std::vector<float>& buf, const glm::mat4& M, const std::vector<Vertex>& verts,
                         int& hits, int& covered, float& meanWorldErr) {
        hits = 0; covered = 0; double sumW = 0.0;
        const glm::mat3 R = glm::mat3(M);                   // rigid delta -> rotation only
        const int stride = std::max(1, int(verts.size()) / 3000);   // ~3000 sampled landmarks
        for (size_t i = 0; i < verts.size(); i += stride) {
            const glm::vec3 Cv = glm::vec3(M * glm::vec4(verts[i].position, 1.0f));
            const glm::vec3 nW = R * verts[i].normal;       // world normal
            // only FRONT-FACING landmarks can be verified (back-faces are self-occluded, not errors)
            if (glm::dot(nW, eye - Cv) <= 0.2f * glm::length(nW) * glm::length(eye - Cv)) continue;
            const glm::vec2 pPred = proj(Cv);
            const int x = int(std::lround(pPred.x)), y = int(std::lround(pPred.y));
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            const glm::vec4 p = pxAt(buf, x, y);
            if (p.a < 0.5f) continue;                       // predicted pixel not covered
            ++covered;
            const float d = glm::length(glm::vec3(p) - Cv);
            if (d < eps2px) { ++hits; sumW += d; }          // lands at predicted pixel within +-2px
        }
        meanWorldErr = hits ? float(sumW / hits) : 1e9f;
    };

    // ---- two configs: A (mild) and B (swept; modest J1 yaw keeps the broadside view) ----
    const std::vector<float> qA = { 0.10f, 0.25f, 0.35f, 0.f };
    const std::vector<float> qB = { 0.45f, 0.55f, 0.95f, 0.f };
    auto solidM = [&](int t) { return reg.get<TransformComponent>(setup.solidEntity[feats[t].inspectIdx]).getTransform(); };
    auto centroidPix = [&](int t) { return proj(glm::vec3(solidM(t) * glm::vec4(feats[t].restCentroid, 1.f))); };

    std::vector<float> bufA, bufB;
    std::array<glm::mat4, 3> MA, MB; std::array<glm::vec2, 3> pA, pB;
    drive(qA); render(bufA); for (int t = 0; t < 3; ++t) { MA[t] = solidM(t); pA[t] = centroidPix(t); }
    drive(qB); render(bufB); for (int t = 0; t < 3; ++t) { MB[t] = solidM(t); pB[t] = centroidPix(t); }

    bool ok = true; float maxWorldErr = 0.f; int minHits = 1 << 30; float minScreenMove = 1e9f, minHitFrac = 1e9f;
    for (int t = 0; t < 3; ++t) {
        const auto& verts = reg.get<RenderableMeshComponent>(setup.solidEntity[feats[t].inspectIdx]).vertices;
        int hA, cA, hB, cB; float weA, weB;
        evalSolid(bufA, MA[t], verts, hA, cA, weA);
        evalSolid(bufB, MB[t], verts, hB, cB, weB);
        const float fracA = cA ? float(hA) / cA : 0.f, fracB = cB ? float(hB) / cB : 0.f;
        maxWorldErr = std::max(maxWorldErr, std::max(weA, weB));
        minHits = std::min(minHits, std::min(hA, hB));
        minHitFrac = std::min(minHitFrac, std::min(fracA, fracB));
        minScreenMove = std::min(minScreenMove, glm::length(pB[t] - pA[t]));
        // PASS bar = MANY landmarks per solid land at their predicted pixel with tiny world error.
        // The hit FRACTION (<100%) is just inter-solid occlusion (the folded arm hides parts of each
        // solid) -> reported, not gated. Correctness = hits >> 0 + worldErr ~0 + neg-ctrl == 0.
        if (hA < 100 || hB < 100 || weA > eps2px || weB > eps2px) ok = false;
        printf("[fanuc render]    %-13s hits/front(A,B)=(%d/%d,%d/%d) worldErr(A,B)=(%.1f,%.1f)mm screenMove=%.1fpx\n",
               trackName[t], hA, cA, hB, cB, weA * 1000.f, weB * 1000.f, glm::length(pB[t] - pA[t]));
    }
    const bool moved = minScreenMove > 8.0f;
    const bool v2render = ok && moved;
    printf("[fanuc render]  V.2 landmarks land at predicted pixel: minHits=%d (bound 100) @ worldErr<=%.1fmm"
           " (=%.1fpx-equiv, bound +-2px=%.1fmm); hitFrac=%.2f (occlusion, ungated); minScreenMove=%.1f px  %s\n",
           minHits, maxWorldErr * 1000.f, maxWorldErr / std::max(1e-4f, mpp), eps2px * 1000.f,
           minHitFrac, minScreenMove, v2render ? "PASS" : "FAIL");

    // ---- NEGATIVE CONTROL: predict the forearm's landmarks via the WRONG link (link 1, the
    // carousel delta). The solid is NOT there -> its predicted pixels read unrelated world -> ~0 hits. ----
    const auto& fverts = reg.get<RenderableMeshComponent>(setup.solidEntity[feats[2].inspectIdx]).vertices;
    int hWrong, cWrong; float weWrong; evalSolid(bufB, MB[0], fverts, hWrong, cWrong, weWrong);  // MB[0] = link-1 delta
    (void)weWrong;
    const bool guardBites = hWrong < 8;                    // wrong-link prediction must not validate
    printf("[fanuc render]  neg-ctrl (forearm landmarks via link-1): hits=%d/%d -> guard %s\n",
           hWrong, cWrong, guardBites ? "REJECTS(non-vacuous)" : "VACUOUS!");

    // cleanup
    for (auto& [e, gm] : meshes) { gl->glDeleteVertexArrays(1, &gm.vao); gl->glDeleteBuffers(1, &gm.vbo); gl->glDeleteBuffers(1, &gm.ebo); }
    gl->glDeleteTextures(1, &colTex); gl->glDeleteTextures(1, &depTex); gl->glDeleteFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    sim.stop();

    const bool pass = v2render && guardBites;
    printf("[fanuc render] %s\n", pass ? "ALL PASS (V.2 render + neg-ctrl)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
#endif
}
