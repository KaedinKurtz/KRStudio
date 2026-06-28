#include "UtilityHeaders/GLUtils.hpp"

#include "RenderingHeaders/Texture2D.hpp"
#include "RenderingHeaders/Shader.hpp"
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLContext>
#include <memory>
#include <unordered_map>
#include <glm/vec4.hpp> // For glm::u8vec4


namespace {
    // This helper is now in an anonymous namespace,
    // making it local to just this .cpp file.
    GLuint getFullscreenQuadVAO(QOpenGLFunctions_4_3_Core* gl) {
        // VAOs are PER-CONTEXT (never shared, even under AA_ShareOpenGLContexts which
        // shares only buffers/textures). With multiple viewports = multiple contexts, a
        // single static VAO is valid in just ONE context and draws garbage in the others.
        // Cache one VAO per current context.
        static std::unordered_map<QOpenGLContext*, GLuint> quadVAOs;
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        auto it = quadVAOs.find(ctx);
        if (it != quadVAOs.end()) return it->second;

        float quadVertices[] = {
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        GLuint quadVAO = 0, quadVBO = 0;
        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);
        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        gl->glBindVertexArray(0);
        quadVAOs[ctx] = quadVAO;
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

    GLuint getUnitCubeVAO(QOpenGLFunctions_4_3_Core* gl) {
        // VAOs are PER-CONTEXT (never shared across GL contexts). A single static cube
        // VAO is valid in only the context that created it; in a SECOND viewport's
        // context it draws garbage -> the skybox / grey-room / IBL-bake cube renders
        // with black wedges. (This is the multi-viewport "black planes" regression.)
        // Cache one cube VAO per current context.
        static std::unordered_map<QOpenGLContext*, GLuint> cubeVAOs;
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        {
            auto it = cubeVAOs.find(ctx);
            if (it != cubeVAOs.end()) return it->second;
        }
        GLuint cubeVAO = 0;
        {
            float vertices[] = {
                // positions
                -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
                 1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
                -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
                -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
                 1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
                 1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
                -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
                 1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
                -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
                 1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
                -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
                 1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
            };
            GLuint cubeVBO;
            gl->glGenVertexArrays(1, &cubeVAO);
            gl->glGenBuffers(1, &cubeVBO);
            gl->glBindVertexArray(cubeVAO);
            gl->glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
            gl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            gl->glEnableVertexAttribArray(0);
            gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            gl->glBindVertexArray(0);
            // We can delete the VBO now that the VAO has captured the state.
            gl->glDeleteBuffers(1, &cubeVBO);
        }
        cubeVAOs[ctx] = cubeVAO;
        return cubeVAO;
    }

} // namespace GLUtils
