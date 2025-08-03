#include "LightingPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "PrimitiveBuilders.hpp" // For setupFullscreenQuadAttribs

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>

LightingPass::~LightingPass() {}

void LightingPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    // Shaders are retrieved during execute()
}

void LightingPass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    Shader* lightingShader = context.renderer.getShader("lighting");
    if (!gl || !lightingShader) {
        qDebug() << "[LightingPass] Missing GL or shader!";
        return;
    }

    // 1) Bind the offscreen FBO as DRAW target, set draw buffer
    const GLuint dstFBO = context.targetFBOs.finalFBO;
    qDebug() << "[LightingPass] Binding DRAW_FRAMEBUFFER =" << dstFBO;
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);

    // Tell GL to write into COLOR_ATTACHMENT0 of that FBO
    GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
    gl->glDrawBuffers(1, drawBufs);
    qDebug() << "[LightingPass] Set DRAW_BUFFERS[0] =" << drawBufs[0];

    // (Optional) clear the color so you can see if something actually writes
    gl->glClearColor(0, 0, 0, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // 2) Set up your shader & G-Buffer textures
    lightingShader->use(gl);
    lightingShader->setVec3(gl, "viewPos",   context.camera.getPosition());
    lightingShader->setVec3(gl, "lightPos",  glm::vec3(5,10,5));
    lightingShader->setVec3(gl, "lightColor",glm::vec3(1,1,1));

    const auto& gbuf = context.renderer.getGBuffer();
    qDebug() << "[LightingPass] G-Buffer IDs ?"
             << "Position="   << gbuf.positionTexture
             << "Normal="     << gbuf.normalTexture
             << "AlbedoSpec=" << gbuf.albedoSpecTexture;

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, gbuf.positionTexture);
    lightingShader->setInt(gl, "gPosition", 0);

    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, gbuf.normalTexture);
    lightingShader->setInt(gl, "gNormal", 1);

    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_2D, gbuf.albedoSpecTexture);
    lightingShader->setInt(gl, "gAlbedoSpec", 2);

    // 3) Bind VAO, draw
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!m_fullscreenVAOs.contains(ctx)) {
        qDebug() << "[LightingPass] Creating fullscreen VAO for context" << ctx;
        createFullscreenQuad(ctx, gl);
    }
    GLuint vao = m_fullscreenVAOs[ctx];
    gl->glBindVertexArray(vao);

    GLint bound = 0;
    gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound);
    qDebug() << "[LightingPass] Bound VAO =" << bound << "(expected" << vao << ")";

    qDebug() << "[LightingPass] Drawing fullscreen triangle...";
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    // 4) Cleanup
    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    qDebug() << "[LightingPass] Draw complete.";
}


void LightingPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    if (m_fullscreenVAOs.contains(dyingContext)) {
        GLuint vao_id = m_fullscreenVAOs.value(dyingContext);
        gl->glDeleteVertexArrays(1, &vao_id);
        m_fullscreenVAOs.remove(dyingContext);
    }
}

void LightingPass::createFullscreenQuad(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    // This helper function (already in your PrimitiveBuilders file)
    // sets up a VAO for drawing a fullscreen triangle without a VBO.
    setupFullscreenQuadAttribs(gl, vao);
    m_fullscreenVAOs[ctx] = vao;
}