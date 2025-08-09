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

    // ===== Effect Toggles ===================================================
    const bool isOutline = true; // <-- SET THIS TO TRUE/FALSE TO SWITCH EFFECTS
	const bool isInternal = false; // <-- Use internal edge detection or sobel outline
    // ========================================================================

    // --- Standard checks and resource setup ---
    auto& reg = ctx.registry;
    auto viewSel = reg.view<RenderableMeshComponent, TransformComponent, SelectedComponent>();
    if (viewSel.size_hint() == 0) return;

    const auto* pp = ctx.renderer.getPPFBOs();
    const auto& tgt = ctx.targetFBOs;

    // --- Get Shaders ---
    Shader* maskSolid = ctx.renderer.getShader("solid_mask");
    Shader* comp = ctx.renderer.getShader("composite_simple");
    if (!maskSolid || !comp) {
        qWarning() << "[SelectionGlowPass] Missing common shader(s).";
        return;
    }

    // --- Helpers and Fullscreen VAO ---
    auto bindDrawFBO = [&](GLuint fbo, int w, int h) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glDrawBuffer(GL_COLOR_ATTACHMENT0);
        gl->glViewport(0, 0, w, h);
        };

    static QHash<QOpenGLContext*, GLuint> s_fsVAO;
    QOpenGLContext* qctx = QOpenGLContext::currentContext();
    if (!qctx || !s_fsVAO.contains(qctx)) {
        GLuint vao = 0; gl->glGenVertexArrays(1, &vao); s_fsVAO[qctx] = vao;
    }
    GLuint fsVAO = s_fsVAO[qctx];

    // ========================================================================
    // PASS 1: RENDER MASK (This part is common to both effects)
    // ========================================================================
    bindDrawFBO(pp[0].fbo, pp[0].w, pp[0].h);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE);
    gl->glEnable(GL_CULL_FACE);
    gl->glClearColor(0, 0, 0, 0);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // Attach the main scene's depth buffer to ensure correct occlusion
    if (tgt.finalDepthTexture) {
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tgt.finalDepthTexture, 0);
    }

    // Draw selected meshes into the mask texture (pp[0])
    maskSolid->use(gl);
    maskSolid->setMat4(gl, "view", ctx.view);
    maskSolid->setMat4(gl, "projection", ctx.projection);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-10.0f, -10.0f);

    for (auto e : viewSel) {
        const auto& mesh = viewSel.get<RenderableMeshComponent>(e);
        if (mesh.indices.empty()) continue;
        const auto& xf = viewSel.get<TransformComponent>(e);
        maskSolid->setMat4(gl, "model", xf.getTransform());
        const auto& bufs = ctx.renderer.getOrCreateMeshBuffers(gl, qctx, e);
        if (!bufs.VAO) continue;
        gl->glBindVertexArray(bufs.VAO);
        gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh.indices.size()), GL_UNSIGNED_INT, 0);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);

    // Detach external depth buffer and restore state
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_CULL_FACE);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glBindVertexArray(fsVAO);

    // This variable will hold the result of our effect pass
    GLuint finalEffectTexture = 0;

    // ========================================================================
    //  EFFECT-SPECIFIC LOGIC
    // ========================================================================
    if (isOutline)
    {
        // The reliable mask pass is now the first step for BOTH outline types.
        // It draws a clean, white silhouette of ONLY the selected object(s) into pp[0].
        bindDrawFBO(pp[0].fbo, pp[0].w, pp[0].h);
        gl->glEnable(GL_POLYGON_OFFSET_FILL);
        gl->glPolygonOffset(-2.0f, -2.0f); // Ensure the mask wins the depth test
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthMask(GL_FALSE);
        gl->glEnable(GL_CULL_FACE);
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        if (tgt.finalDepthTexture) {
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tgt.finalDepthTexture, 0);
        }
        maskSolid->use(gl);
        maskSolid->setMat4(gl, "view", ctx.view);
        maskSolid->setMat4(gl, "projection", ctx.projection);
        for (auto e : viewSel) {
            const auto& mesh = viewSel.get<RenderableMeshComponent>(e);
            if (mesh.indices.empty()) continue;
            const auto& xf = viewSel.get<TransformComponent>(e);
            maskSolid->setMat4(gl, "model", xf.getTransform());
            const auto& bufs = ctx.renderer.getOrCreateMeshBuffers(gl, qctx, e);
            if (bufs.VAO) {
                gl->glBindVertexArray(bufs.VAO);
                gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh.indices.size()), GL_UNSIGNED_INT, 0);
            }
        }
        gl->glDisable(GL_POLYGON_OFFSET_FILL);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glBindVertexArray(fsVAO);

        // Now, choose which filter to apply to our scene data.
        if (isInternal)
        {
            // --- ADVANCED (INTERNAL) OUTLINE PATH ---
            Shader* advOutlineShader = ctx.renderer.getShader("edge_detect_advanced");
            if (!advOutlineShader) { qWarning() << "Missing edge_detect_advanced shader."; return; }

            // Draw the final outline into pp[1]
            bindDrawFBO(pp[1].fbo, pp[1].w, pp[1].h);
            gl->glClearColor(0, 0, 0, 0);
            gl->glClear(GL_COLOR_BUFFER_BIT);

            advOutlineShader->use(gl);
            advOutlineShader->setInt(gl, "gPosition", 0);
            advOutlineShader->setInt(gl, "gNormal", 1);
            advOutlineShader->setInt(gl, "uSelectionMask", 2); // Pass in the new selection mask
            advOutlineShader->setVec3(gl, "uOutlineColor", glm::vec3(1.0f, 0.84f, 0.20f));
            advOutlineShader->setFloat(gl, "uNormalThreshold", 0.25f);
            advOutlineShader->setFloat(gl, "uDepthThreshold", 0.1f);
            advOutlineShader->setVec2(gl, "uTexelSize", 1.0f / glm::vec2(pp[1].w, pp[1].h));

            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, ctx.renderer.getGBuffer().positionTexture);
            gl->glActiveTexture(GL_TEXTURE1);
            gl->glBindTexture(GL_TEXTURE_2D, ctx.renderer.getGBuffer().normalTexture);
            gl->glActiveTexture(GL_TEXTURE2);
            gl->glBindTexture(GL_TEXTURE_2D, pp[0].colorTexture); // Bind our clean mask from the first pass

            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            finalEffectTexture = pp[1].colorTexture;
        }
        else
        {
            // --- SIMPLE (SILHOUETTE) OUTLINE PATH ---
            // (This path can now use the robust shader instead of the old Sobel one)
            Shader* outlineShader = ctx.renderer.getShader("outline_sobel"); // The robust one
            if (!outlineShader) { qWarning() << "Missing robust outline shader."; return; }

            bindDrawFBO(pp[1].fbo, pp[1].w, pp[1].h);
            gl->glClearColor(0, 0, 0, 0);
            gl->glClear(GL_COLOR_BUFFER_BIT);

            outlineShader->use(gl);
            outlineShader->setInt(gl, "uMaskTexture", 0);
            outlineShader->setVec3(gl, "uOutlineColor", glm::vec3(1.0f, 0.84f, 0.20f));
            outlineShader->setFloat(gl, "uThickness", 1.5f);
            outlineShader->setVec2(gl, "uTexelSize", 1.0f / glm::vec2(pp[0].w, pp[0].h));

            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, pp[0].colorTexture);

            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            finalEffectTexture = pp[1].colorTexture;
        }
    }
    else
    {
        // --- GLOW EFFECT PATH ---
        Shader* blurShader = ctx.renderer.getShader("blur");
        Shader* edgeShader = ctx.renderer.getShader("outline_edge");
        if (!blurShader || !edgeShader) { qWarning() << "Missing glow-related shaders."; return; }

        // 1. Preserve the sharp mask before blurring
        ensureMaskCopyTex(gl, pp[0].w, pp[0].h); // Your helper to create/resize m_maskCopyTex
        gl->glCopyImageSubData(pp[0].colorTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
            m_maskCopyTex, GL_TEXTURE_2D, 0, 0, 0, 0,
            pp[0].w, pp[0].h, 1);

        // 2. Two-pass Gaussian blur
        bool horizontal = true;
        for (int i = 0; i < 2; ++i) {
            int dst = horizontal ? 1 : 0;
            int src = horizontal ? 0 : 1;
            bindDrawFBO(pp[dst].fbo, pp[dst].w, pp[dst].h);
            blurShader->use(gl);
            blurShader->setBool(gl, "horizontal", horizontal);
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, pp[src].colorTexture);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            horizontal = !horizontal;
        }
        const GLuint blurredTex = pp[0].colorTexture; // Blurred result ends up in pp[0]

        // 3. Edge pass (blur - mask) -> writes to pp[1] to avoid hazards
        bindDrawFBO(pp[1].fbo, pp[1].w, pp[1].h);
        gl->glClearColor(0, 0, 0, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        edgeShader->use(gl);
        edgeShader->setFloat(gl, "uIntensity", 3.5f);
        edgeShader->setVec3(gl, "uColor", glm::vec3(1.0f, 0.84f, 0.20f));
        // For additive glow, output should be non-premultiplied.
        // If you had a uPremultiplyAlpha uniform, you'd set it to false here.

        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex); // Sharp mask
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, blurredTex);   // Blurred mask
        edgeShader->setInt(gl, "uMask", 0);
        edgeShader->setInt(gl, "uBlur", 1);

        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        finalEffectTexture = pp[1].colorTexture;
    }


    // ========================================================================
    // FINAL COMPOSITE PASS (Common to both effects)
    // ========================================================================
    gl->glBindFramebuffer(GL_FRAMEBUFFER, tgt.finalFBO);
    gl->glViewport(0, 0, tgt.w, tgt.h);
    gl->glEnable(GL_BLEND);

    if (isOutline) {
        // Standard alpha blending for a crisp outline
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else {
        // Additive blending for a "glow" effect
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    comp->use(gl);
    comp->setInt(gl, "screenTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, finalEffectTexture); // Use the result from the effect pass

    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Final Cleanup ---
    gl->glDisable(GL_BLEND);
    gl->glBindVertexArray(0);
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

void SelectionGlowPass::ensureMaskCopyTex(QOpenGLFunctions_4_3_Core* gl, int w, int h)
{
    if (m_maskCopyTex == 0)
    {
        // First-time creation
        gl->glGenTextures(1, &m_maskCopyTex);
        gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Allocate storage for the texture
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    else
    {
        // Texture exists, so just bind it and check if its size is correct
        gl->glBindTexture(GL_TEXTURE_2D, m_maskCopyTex);
        GLint currentW = 0, currentH = 0;
        gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &currentW);
        gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &currentH);

        // Resize the texture only if the dimensions have changed
        if (currentW != w || currentH != h)
        {
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
    }

    // Unbind the texture to be safe
    gl->glBindTexture(GL_TEXTURE_2D, 0);
}