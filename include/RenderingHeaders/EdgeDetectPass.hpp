#pragma once
#include "IRenderPass.hpp"
#include "RenderingSystem.hpp"

class EdgeDetectPass : public IRenderPass {
public:
    ~EdgeDetectPass() override = default;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override {
        m_vs = renderer.getShader("post_process_vert");
        m_fs = renderer.getShader("edge_detect");
    }

    void execute(const RenderFrameContext& ctx) override {
        auto* gl = ctx.gl;
        if (!m_vs || !m_fs) return;
        m_fs->use(gl);

        // Bind the G-Buffer position texture
        gl->glActiveTexture(GL_TEXTURE0);
        GLuint posTex = ctx.renderer.getGBuffer().positionTexture;
        qDebug() << "[EdgeDetectPass] Binding depth (position) texture =" << posTex;
        gl->glBindTexture(GL_TEXTURE_2D, posTex);
        m_fs->setInt(gl, "uDepthTex", 0);
        m_fs->setFloat(gl, "uWidth", float(ctx.viewportWidth));
        m_fs->setFloat(gl, "uHeight", float(ctx.viewportHeight));

        // Setup fullscreen quad VAO (singleton per context)
        QOpenGLContext* qc = QOpenGLContext::currentContext();
        static QHash<QOpenGLContext*, GLuint> vaos;
        GLuint vao;
        if (!vaos.contains(qc)) {
            gl->glGenVertexArrays(1, &vao);
            setupFullscreenQuadAttribs(gl, vao);
            vaos[qc] = vao;
        }
        else {
            vao = vaos[qc];
        }

        // Draw full-screen triangle
        gl->glBindVertexArray(vao);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        gl->glBindVertexArray(0);
    }

    void onContextDestroyed(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) override {
        static QHash<QOpenGLContext*, GLuint> vaos;
        if (vaos.contains(ctx)) {
            GLuint v = vaos.take(ctx);
            gl->glDeleteVertexArrays(1, &v);
        }
    }

private:
    Shader* m_vs = nullptr;
    Shader* m_fs = nullptr;
};

// ----------------------------------------------------------------------------------
// Usage in RenderingSystem.cpp:
// -----------------------------------------
// #include "EdgeDetectPass.hpp"
// 
// ... in your init or constructor:
// m_postProcessingPasses.clear();
// m_postProcessingPasses.push_back(
//     std::make_unique<EdgeDetectPass>() // note: no arguments
// );
// ----------------------------------------------------------------------------------
