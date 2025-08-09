#include "SelectionGlowPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

// Helper: get/create a trivial VAO for gl_VertexID fullscreen triangle
static GLuint getFullscreenVAO(QOpenGLFunctions_4_3_Core* gl) {
    static QHash<QOpenGLContext*, GLuint> s_vaos;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return 0;
    if (s_vaos.contains(ctx)) return s_vaos[ctx];
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);           // no VBO/attribs needed (VS uses gl_VertexID)
    gl->glBindVertexArray(0);
    s_vaos[ctx] = vao;
    qDebug() << "[SelectionGlowPass] Created fullscreen VAO =" << vao << "for ctx" << ctx;
    return vao;
}

SelectionGlowPass::~SelectionGlowPass() {}

void SelectionGlowPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    Q_UNUSED(gl);
    m_shaderMask = renderer.getShader("pp_mesh_mask");
    m_shaderBlur = renderer.getShader("blur");
    m_shaderEdge = renderer.getShader("outline_edge");
    m_shaderComposite = renderer.getShader("composite_simple"); // <- one-sampler composite

    qDebug() << "[SelectionGlowPass::initialize] shaders:"
        << "mask=" << (void*)m_shaderMask
        << " blur=" << (void*)m_shaderBlur
        << " edge=" << (void*)m_shaderEdge
        << " composite=" << (void*)m_shaderComposite;
    if (!m_shaderComposite) {
        qWarning() << "  [WARN] composite_simple shader not found. Add it in initializeSharedResources().";
    }
}

static void logFBOStatus(QOpenGLFunctions_4_3_Core* gl, const char* tag) {
    GLenum st = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    qDebug().nospace()
        << tag << "FBO status = 0x"
        << QString::number(static_cast<unsigned int>(st), 16).toUpper();
}


void SelectionGlowPass::execute(const RenderFrameContext& ctx)
{
    auto* gl = ctx.gl;
    if (!gl) { qWarning() << "[SelectionGlowPass] No GL"; return; }

    // ===== Debug toggles =====================================================
    const bool DEBUG_SHOW_MASK_ONLY = true;
    const bool DEBUG_SHOW_EDGE_ONLY = false;
    const bool DEBUG_READBACK_PIXELS = true;
    const bool DEBUG_LOG_GL_ERRORS = true;
    // Choose ONE: coverage overlay (premultiplied) OR additive glow
    const bool USE_COVERAGE_BLEND = true;   // if false => additive
    // ========================================================================

    auto& reg = ctx.registry;
    auto viewSel = reg.view<RenderableMeshComponent, TransformComponent, SelectedComponent>();
    const int selectedCt = int(viewSel.size_hint());
    qDebug() << "[SelectionGlowPass] begin: selected =" << selectedCt;
    if (selectedCt == 0) return;

    // Frame resources ---------------------------------------------------------
    const auto* pp = ctx.renderer.getPPFBOs();    // ping-pong [0], [1]
    const auto& tgt = ctx.targetFBOs;

    // Shaders -----------------------------------------------------------------
    Shader* maskSolid = ctx.renderer.getShader("solid_mask");       // draws white
    Shader* blur = ctx.renderer.getShader("blur");
    Shader* edge = ctx.renderer.getShader("outline_edge");
    Shader* comp = ctx.renderer.getShader("composite_simple");

    if (!maskSolid || !blur || !edge || !comp) {
        qWarning() << "[SelectionGlowPass] Missing shader(s)."
            << " solid_mask=" << (void*)maskSolid
            << " blur=" << (void*)blur
            << " edge=" << (void*)edge
            << " composite=" << (void*)comp;
        return;
    }

    // Helpers -----------------------------------------------------------------
    auto bindDrawFBO = [&](GLuint fbo, int w, int h, const char* tag) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
        gl->glViewport(0, 0, w, h);
        GLenum st = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        qDebug() << "[FBO]" << tag << " fbo=" << fbo << " size=(" << w << "x" << h << ") status=0x"
            << QString::number(uint(st), 16).toUpper();
        };
    auto readCenterRGBA = [&](const char* tag, int w, int h) {
        if (!DEBUG_READBACK_PIXELS) return;
        unsigned char px[4] = { 0,0,0,0 };
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        qDebug() << tag << "center RGBA =" << int(px[0]) << int(px[1]) << int(px[2]) << int(px[3]);
        };
    auto dumpGLErr = [&](const char* tag) {
        if (!DEBUG_LOG_GL_ERRORS) return;
        for (GLenum err = gl->glGetError(); err != GL_NO_ERROR; err = gl->glGetError()) {
            qWarning() << "[SelectionGlowPass]" << tag
                << "GL error =" << QString("0x%1").arg(uint(err), 0, 16).toUpper();
        }
        };

    // Fullscreen VAO (gl_VertexID triangle)
    static QHash<QOpenGLContext*, GLuint> s_fsVAO;
    QOpenGLContext* qctx = QOpenGLContext::currentContext();
    if (!qctx) { qWarning() << "[SelectionGlowPass] No current context"; return; }
    if (!s_fsVAO.contains(qctx)) {
        GLuint vao = 0; gl->glGenVertexArrays(1, &vao); s_fsVAO[qctx] = vao;
    }
    GLuint fsVAO = s_fsVAO[qctx];

    // -------------------------------------------------------------------------
    // PASS 1: depth-correct mask into PP[0] (WHITE where selected is visible)
    // -------------------------------------------------------------------------
    bindDrawFBO(pp[0].fbo, pp[0].w, pp[0].h, "PP[0]/MASK ");

    gl->glDisable(GL_BLEND);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);
    gl->glDepthMask(GL_FALSE);      // do not write depth
    gl->glEnable(GL_CULL_FACE);
    gl->glCullFace(GL_BACK);
    gl->glClearColor(0, 0, 0, 0);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // Attach the viewport's depth texture temporarily to PP[0]
    GLint prevType = GL_NONE, prevDepthObj = 0;
    gl->glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &prevType);
    if (prevType != GL_NONE) {
        gl->glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &prevDepthObj);
    }
    qDebug() << "  [PASS1] PP[0] prev depth type =" << prevType << " name =" << prevDepthObj;

    if (tgt.finalDepthTexture) {
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
            tgt.finalDepthTexture, 0);
        GLenum stA = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        qDebug() << "  [PASS1] PP[0] after attach finalDepth status = 0x"
            << QString::number(uint(stA), 16).toUpper();
    }
    else {
        qWarning() << "  [PASS1] target.finalDepthTexture == 0 (mask may not match scene depth)";
    }

    // Draw selected meshes to WHITE
    maskSolid->use(gl);
    maskSolid->setMat4(gl, "view", ctx.view);
    maskSolid->setMat4(gl, "projection", ctx.projection);

    int maskDrawn = 0;
    for (auto e : viewSel) {
        const auto& mesh = viewSel.get<RenderableMeshComponent>(e);
        if (mesh.indices.empty()) continue;

        const auto& xf = viewSel.get<TransformComponent>(e);
        maskSolid->setMat4(gl, "model", xf.getTransform());

        const auto& bufs = ctx.renderer.getOrCreateMeshBuffers(gl, qctx, e);
        if (!bufs.VAO) { qWarning() << "  [PASS1] entity" << (uint32_t)e << "no VAO"; continue; }

        gl->glBindVertexArray(bufs.VAO);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh.indices.size()), GL_UNSIGNED_INT, 0);
        ++maskDrawn;
    }
    gl->glBindVertexArray(fsVAO); // restore FS VAO for screen passes

    // Restore depth attachment & state
    if (prevType == GL_TEXTURE) {
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
            (GLuint)prevDepthObj, 0);
    }
    else {
        // Detach depth if none was attached before
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    }
    GLenum stRestore = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    qDebug() << "  [PASS1] PP[0] depth RESTORE status = 0x"
        << QString::number(uint(stRestore), 16).toUpper();

    gl->glDisable(GL_CULL_FACE);
    gl->glDepthMask(GL_TRUE);
    gl->glDepthFunc(GL_LESS);

    qDebug() << "  [PASS1] drew" << maskDrawn << "selected meshes.";
    readCenterRGBA("  [PASS1] mask", pp[0].w, pp[0].h);
    dumpGLErr("after PASS1");

    if (DEBUG_SHOW_MASK_ONLY) {
        bindDrawFBO(tgt.finalFBO, tgt.w, tgt.h, "FINAL/MASK ");
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_BLEND);
        comp->use(gl);
        comp->setInt(gl, "screenTexture", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, pp[0].colorTexture);
        gl->glBindVertexArray(fsVAO);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        qDebug() << "[SelectionGlowPass] DEBUG_SHOW_MASK_ONLY done.";
        return;
    }

    // Preserve HARD mask before blur ------------------------------------------------
    auto ensureMaskCopyTex = [&](int w, int h) {
        if (m_maskCopyTex == 0) {
            gl->glGenTextures(1, &m_maskCopyTex);
            gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        else {
            GLint curW = 0, curH = 0;
            gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex);
            gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &curW);
            gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &curH);
            if (curW != w || curH != h) {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
        }
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        };
    ensureMaskCopyTex(pp[0].w, pp[0].h);

    // Copy the hard mask from pp[0] into m_maskCopyTex (GL 4.3+)
    gl->glCopyImageSubData(
        pp[0].colorTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        m_maskCopyTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        pp[0].w, pp[0].h, 1);

    // -------------------------------------------------------------------------
    // PASS 2: two-pass Gaussian blur (H+V) using PP ping-pong
    // -------------------------------------------------------------------------
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_BLEND);
    gl->glBindVertexArray(fsVAO);

    bool horizontal = true;
    int  lastDstIdx = -1;
    for (int i = 0; i < 2; ++i) {
        int dst = horizontal ? 1 : 0;
        int src = horizontal ? 0 : 1;

        bindDrawFBO(pp[dst].fbo, pp[dst].w, pp[dst].h,
            horizontal ? "PP[1]/BLUR_H " : "PP[0]/BLUR_V ");
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        blur->use(gl);
        blur->setBool(gl, "horizontal", horizontal);
        blur->setInt(gl, "screenTexture", 0);

        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, pp[src].colorTexture);

        qDebug() << "  [PASS2] blur step i=" << i
            << " horiz=" << horizontal
            << " srcTex=" << pp[src].colorTexture
            << " -> fbo=" << pp[dst].fbo;

        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        readCenterRGBA(horizontal ? "  [PASS2] blurH" : "  [PASS2] blurV", pp[dst].w, pp[dst].h);

        lastDstIdx = dst;
        horizontal = !horizontal;
    }
    const GLuint blurredTex = pp[lastDstIdx].colorTexture;

    // -------------------------------------------------------------------------
    // PASS 3: edge = max(blur - mask, 0) * color  -> PP[0]
    // -------------------------------------------------------------------------
    bindDrawFBO(pp[0].fbo, pp[0].w, pp[0].h, "PP[0]/EDGE ");
    gl->glClearColor(0, 0, 0, 0);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    edge->use(gl);
    edge->setInt(gl, "uMask", 0);
    edge->setInt(gl, "uBlur", 1);
    edge->setVec3(gl, "uColor", glm::vec3(1.0f, 0.84f, 0.20f)); // gold
    edge->setFloat(gl, "uIntensity", 3.5f);   // start obvious; tune later
    edge->setFloat(gl, "uThreshold", 0.0f);

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex);   // HARD mask
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, blurredTex);      // BLURRED mask

    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    readCenterRGBA("  [PASS3] edge", pp[0].w, pp[0].h);

    if (DEBUG_SHOW_EDGE_ONLY) {
        bindDrawFBO(tgt.finalFBO, tgt.w, tgt.h, "FINAL/EDGE ");
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_BLEND);
        comp->use(gl);
        comp->setInt(gl, "screenTexture", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, pp[0].colorTexture);
        gl->glBindVertexArray(fsVAO);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        qDebug() << "[SelectionGlowPass] DEBUG_SHOW_EDGE_ONLY done.";
        return;
    }

    // -------------------------------------------------------------------------
    // FINAL: composite onto this viewport’s final FBO
    // -------------------------------------------------------------------------
    gl->glBindFramebuffer(GL_FRAMEBUFFER, tgt.finalFBO);
    gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl->glViewport(0, 0, tgt.w, tgt.h);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glEnable(GL_BLEND);

    if (USE_COVERAGE_BLEND) {
        // Overlay/tint style (expects PREMULTIPLIED src: rgb = color*k, a = k)
        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    else {
        // Additive “glow” (expects NON-premult src: rgb = color, a = k)
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    comp->use(gl);
    comp->setInt(gl, "screenTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, pp[0].colorTexture);

    qDebug() << "  [FINAL] composite -> finalFBO=" << tgt.finalFBO
        << " srcTex=" << pp[0].colorTexture;

    gl->glBindVertexArray(fsVAO);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    gl->glDisable(GL_BLEND);
    gl->glBindVertexArray(0);
    dumpGLErr("end");
    qDebug() << "[SelectionGlowPass] done.";
}




void SelectionGlowPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_compositeVAOs.contains(dyingContext)) {
        GLuint vao = m_compositeVAOs.take(dyingContext);
        gl->glDeleteVertexArrays(1, &vao);
    }
}

void SelectionGlowPass::createCompositeVAOForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    // Unused now; we use getFullscreenVAO() above with gl_VertexID VS.
    Q_UNUSED(ctx); Q_UNUSED(gl);
}
