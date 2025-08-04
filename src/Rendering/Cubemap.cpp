#include "Cubemap.hpp"
#include "Texture2D.hpp"
#include "Shader.hpp"
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

//--- Helper functions and data ---

// Creates and returns a VAO for a unit cube, used for skybox rendering and capture passes.
static GLuint getUnitCubeVAO(QOpenGLFunctions_4_3_Core* gl) {
    static GLuint cubeVAO = 0;
    if (cubeVAO == 0) {
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
    }
    return cubeVAO;
}

// Standard projection and view matrices for capturing all 6 faces of a cubemap.
static const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
static const glm::mat4 captureViews[] = {
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
   glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
};


//--- Cubemap Implementation ---

Cubemap::Cubemap() = default;

Cubemap::~Cubemap() {
    if (_id != 0) {
        // In a complex app, you might need to ensure a context is current before deleting.
        // For now, we get the context and functions pointer directly.
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
        if (gl) {
            gl->glDeleteTextures(1, &_id);
        }
    }
}

GLuint Cubemap::getID() const {
    return _id;
}

void Cubemap::bind(QOpenGLFunctions_4_3_Core* gl, unsigned int unit) const {
    gl->glActiveTexture(GL_TEXTURE0 + unit);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, _id);
}

std::shared_ptr<Cubemap> Cubemap::fromEquirectangular(
    const Texture2D& hdrEquirect, QOpenGLFunctions_4_3_Core* gl,
    const std::string& vertPath, const std::string& fragPath, int faceSize)
{
    auto cube = std::make_shared<Cubemap>();
    cube->_size = faceSize;
    cube->_levels = 1;

    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (unsigned int i = 0; i < 6; ++i) {
        gl->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, faceSize, faceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    auto eqShader = Shader::build(gl, vertPath, fragPath);
    eqShader->use(gl);
    eqShader->setInt(gl, "equirectangularMap", 0);
    eqShader->setMat4(gl, "projection", captureProjection);
    hdrEquirect.bind(0);

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);

    for (unsigned int i = 0; i < 6; ++i) {
        eqShader->setMat4(gl, "view", captureViews[i]);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);

    return cube;
}

std::shared_ptr<Cubemap> Cubemap::convolveIrradiance(
    const Cubemap& source, QOpenGLFunctions_4_3_Core* gl,
    const std::string& vertPath, const std::string& fragPath, int faceSize)
{
    auto cube = std::make_shared<Cubemap>();
    cube->_size = faceSize;
    cube->_levels = 1;

    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (unsigned int i = 0; i < 6; ++i) {
        gl->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, faceSize, faceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    auto convShader = Shader::build(gl, vertPath, fragPath);
    convShader->use(gl);
    convShader->setInt(gl, "environmentMap", 0);
    convShader->setMat4(gl, "projection", captureProjection);
    source.bind(gl, 0);

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);

    for (unsigned int i = 0; i < 6; ++i) {
        convShader->setMat4(gl, "view", captureViews[i]);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);

    return cube;
}

std::shared_ptr<Cubemap> Cubemap::prefilter(
    const Cubemap& source, QOpenGLFunctions_4_3_Core* gl,
    const std::string& vertPath, const std::string& fragPath, int baseSize, int maxMipLevels)
{
    auto cube = std::make_shared<Cubemap>();
    cube->_size = baseSize;
    cube->_levels = maxMipLevels;

    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (int mip = 0; mip < maxMipLevels; ++mip) {
        int mipSize = static_cast<int>(baseSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i) {
            gl->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB16F, mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // This is important! Generate the full mipmap chain.
    gl->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    auto prefilterShader = Shader::build(gl, vertPath, fragPath);
    prefilterShader->use(gl);
    prefilterShader->setInt(gl, "environmentMap", 0);
    prefilterShader->setMat4(gl, "projection", captureProjection);
    source.bind(gl, 0);

    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint cubeVAO = getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);

    for (int mip = 0; mip < maxMipLevels; ++mip) {
        int mipSize = static_cast<int>(baseSize * std::pow(0.5, mip));
        gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        gl->glViewport(0, 0, mipSize, mipSize);

        float roughness = (float)mip / (float)(maxMipLevels - 1);
        prefilterShader->setFloat(gl, "roughness", roughness);

        for (unsigned int i = 0; i < 6; ++i) {
            prefilterShader->setMat4(gl, "view", captureViews[i]);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, mip);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            gl->glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);

    return cube;
}
