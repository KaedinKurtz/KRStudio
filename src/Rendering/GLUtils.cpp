#include "UtilityHeaders/GLUtils.hpp"

#include "RenderingHeaders/Texture2D.hpp"
#include "RenderingHeaders/Shader.hpp"
#include <QOpenGLFunctions_4_3_Core>
#include <memory>
#include <glm/vec4.hpp> // For glm::u8vec4


namespace {
    // This helper is now in an anonymous namespace,
    // making it local to just this .cpp file.
    GLuint getFullscreenQuadVAO(QOpenGLFunctions_4_3_Core* gl) {
        static GLuint quadVAO = 0;
        if (quadVAO == 0) {
            float quadVertices[] = {
                -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
                -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            };
            GLuint quadVBO;
            gl->glGenVertexArrays(1, &quadVAO);
            gl->glGenBuffers(1, &quadVBO);
            gl->glBindVertexArray(quadVAO);
            gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
            gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
            gl->glEnableVertexAttribArray(0);
            gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            gl->glEnableVertexAttribArray(1);
            gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        }
        return quadVAO;
    }
} // anonymous namespace

namespace GLUtils {

    std::shared_ptr<Texture2D> generateBRDFLUT(
        QOpenGLFunctions_4_3_Core* gl,
        const std::string& vertPath,
        const std::string& fragPath,
        int size)
    {
        auto lut = std::make_shared<Texture2D>();
        lut->generate(size, size, GL_RG16F, GL_RG, nullptr);
        gl->glBindTexture(GL_TEXTURE_2D, lut->getID());
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glBindTexture(GL_TEXTURE_2D, 0);

        GLuint fbo, rbo;
        gl->glGenFramebuffers(1, &fbo);
        gl->glGenRenderbuffers(1, &rbo);

        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lut->getID(), 0);

        auto brdfShader = Shader::build(gl, vertPath, fragPath);
        brdfShader->use(gl);

        gl->glViewport(0, 0, size, size);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        GLuint quadVAO = getFullscreenQuadVAO(gl);
        gl->glBindVertexArray(quadVAO);
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl->glBindVertexArray(0);

        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glDeleteFramebuffers(1, &fbo);
        gl->glDeleteRenderbuffers(1, &rbo);

        return lut;
    }

} // namespace GLUtils
