#include "Cubemap.hpp"
#include "Texture2D.hpp"
#include "Shader.hpp"
#include "GLUtils.hpp"
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

//--- Helper functions and data ---

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
            qDebug() << "[Cubemap] Deleting texture ID:" << _id;
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
    qDebug().noquote() << "\n==========================================================";
    qDebug() << "--- Starting [fromEquirectangular] ---";

    auto cube = std::make_shared<Cubemap>();
    cube->_size = faceSize;
    cube->_levels = 1;               // only base level

    // 1) FBO + RBO setup stays exactly the same
    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    // 2) Generate the single?level cube
    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (int face = 0; face < 6; ++face) {
        // level = 0 only
        gl->glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,                // only the base level
            GL_RGB32F,
            faceSize, faceSize,
            0,
            GL_RGB, GL_FLOAT,
            nullptr
        );
    }

    // 3) ***CRISP PARAMETERS*** – no mips, linear filtering
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // 4) Do your usual equirect ? cube render loop at level 0
    auto eqShader = Shader::build(gl, vertPath, fragPath);
    eqShader->use(gl);
    eqShader->setInt(gl, "equirectangularMap", 0);
    eqShader->setMat4(gl, "projection", captureProjection);
    hdrEquirect.bind(0);

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);

    gl->glDepthFunc(GL_LEQUAL);
    bool prevCull = gl->glIsEnabled(GL_CULL_FACE);
    gl->glDisable(GL_CULL_FACE);

    for (int i = 0; i < 6; ++i) {
        eqShader->setMat4(gl, "view", captureViews[i]);
        gl->glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
            cube->_id,
            0          // always level = 0
        );
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    // 5) cleanup exactly like before
    if (prevCull) gl->glEnable(GL_CULL_FACE);
    gl->glDepthFunc(GL_LESS);
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
    qDebug().noquote() << "\n==========================================================";
    qDebug() << "--- Starting [convolveIrradiance] ---";

    auto cube = std::make_shared<Cubemap>();
    cube->_size = faceSize;
    cube->_levels = 1;

    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);
    qDebug() << "  [Step 1] Generated FBO:" << fbo << "and RBO:" << rbo;

    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
    qDebug() << "  [Step 2] Configured FBO/RBO for size" << faceSize << "x" << faceSize;

    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (unsigned int i = 0; i < 6; ++i) {
        gl->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F, faceSize, faceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qDebug() << "  [Step 3] Generated destination Cubemap texture ID:" << cube->_id;

    auto convShader = Shader::build(gl, vertPath, fragPath);
    convShader->use(gl);
    convShader->setInt(gl, "environmentMap", 0);
    convShader->setMat4(gl, "projection", captureProjection);
    source.bind(gl, 0);
    qDebug() << "  [Step 4] Built shader and set uniforms. Source cubemap ID:" << source.getID();

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);
    qDebug() << "  [Step 5] Bound unit cube VAO:" << cubeVAO << ". Starting render loop...";

    gl->glDepthFunc(GL_LEQUAL);
    GLboolean prevCullFace = gl->glIsEnabled(GL_CULL_FACE);
    gl->glDisable(GL_CULL_FACE);

    for (unsigned int i = 0; i < 6; ++i) {
        qDebug().nospace() << "    [Face " << i << "] Attaching face to FBO...";
        convShader->setMat4(gl, "view", captureViews[i]);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, 0);

        GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) { qWarning() << "      !!!!!! FRAMEBUFFER INCOMPLETE! Status:" << status; }

        qDebug() << "      Clearing and drawing...";
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    gl->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    gl->glDepthFunc(GL_LESS);
    if (prevCullFace) { gl->glEnable(GL_CULL_FACE); }
    qDebug() << "  [Step 6] Render loop finished. Restored render state.";

    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);
    qDebug() << "  [Cleanup] Deleting FBO:" << fbo << "and RBO:" << rbo;
    qDebug() << "--- Finished [convolveIrradiance] ---";

    return cube;
}

std::shared_ptr<Cubemap> Cubemap::prefilter(
    const Cubemap& source, QOpenGLFunctions_4_3_Core* gl,
    const std::string& vertPath, const std::string& fragPath, int baseSize, int maxMipLevels)
{
    qDebug().noquote() << "\n==========================================================";
    qDebug() << "--- Starting [prefilter] with Trilinear Sampling ---"; // Updated log message

    auto cube = std::make_shared<Cubemap>();
    cube->_size = baseSize;
    cube->_levels = maxMipLevels;

    // 1) Create the destination texture with a full mipmap chain
    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    for (int mip = 0; mip < maxMipLevels; ++mip) {
        int mipSize = static_cast<int>(baseSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i) {
            gl->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB16F, mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, maxMipLevels - 1);

    // 2) Build and configure the NEW shader
    auto prefilterShader = Shader::build(gl, vertPath, fragPath); // This now refers to trilinear_prefilter_frag.glsl
    prefilterShader->use(gl);
    // Use the new uniform name from the shader
    prefilterShader->setInt(gl, "sourceEnvironment", 0);
    prefilterShader->setMat4(gl, "projection", captureProjection);
    source.bind(gl, 0); // Bind the sharp 2048x2048 cubemap as input

    // 3) FBO setup remains the same
    GLuint fbo, rbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glGenRenderbuffers(1, &rbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);
    gl->glDepthFunc(GL_LEQUAL);
    GLboolean prevCullFace = gl->glIsEnabled(GL_CULL_FACE);
    gl->glDisable(GL_CULL_FACE);

    // 4) The render loop logic is identical
    for (int mip = 0; mip < maxMipLevels; ++mip) {
        int mipSize = static_cast<int>(baseSize * std::pow(0.5, mip));
        float roughness = (float)mip / (float)(maxMipLevels - 1);

        gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        gl->glViewport(0, 0, mipSize, mipSize);
        prefilterShader->setFloat(gl, "roughness", roughness);

        for (unsigned int i = 0; i < 6; ++i) {
            prefilterShader->setMat4(gl, "view", captureViews[i]);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, mip);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            gl->glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    // 5) Cleanup is identical
    gl->glDepthFunc(GL_LESS);
    if (prevCullFace) { gl->glEnable(GL_CULL_FACE); }
    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);

    qDebug() << "--- Finished [prefilter] ---";
    return cube;
}