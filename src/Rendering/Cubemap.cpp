#include "Cubemap.hpp"
#include "Texture2D.hpp"
#include "Shader.hpp" // We will bypass Shader::build to use our own debug version
#include "UtilityHeaders/GLUtils.hpp"

#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QDebug>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::value_ptr
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

// ===================================================================================
// ==                            PEDANTIC DEBUG HELPERS                             ==
// ===================================================================================

static void printMatrix(const glm::mat4& mat, const QString& name) {
    const float* p = glm::value_ptr(mat);
    qDebug().noquote() << "    " << name << ":"
        << "\n      [" << p[0] << p[4] << p[8] << p[12] << "]"
        << "\n      [" << p[1] << p[5] << p[9] << p[13] << "]"
        << "\n      [" << p[2] << p[6] << p[10] << p[14] << "]"
        << "\n      [" << p[3] << p[7] << p[11] << p[15] << "]";
}

// This new function replaces Shader::build to give us detailed compilation logs
static GLuint buildAndDebugShader(
    QOpenGLFunctions_4_3_Core* gl,
    const std::string& vertPath,
    const std::string& fragPath,
    const QString& debugName)
{
    qDebug() << "[" << debugName << " SHADER] Building...";
    // 1. Read shader code
    std::string vertCode, fragCode;
    std::ifstream vShaderFile, fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        vShaderFile.open(vertPath);
        fShaderFile.open(fragPath);
        std::stringstream vss, fss;
        vss << vShaderFile.rdbuf();
        fss << fShaderFile.rdbuf();
        vShaderFile.close();
        fShaderFile.close();
        vertCode = vss.str();
        fragCode = fss.str();
    }
    catch (std::ifstream::failure& e) {
        qWarning() << "  [!!!] FAILED TO READ SHADER FILE:" << e.what();
        return 0;
    }
    const char* vShaderCode = vertCode.c_str();
    const char* fShaderCode = fragCode.c_str();

    // 2. Compile shaders
    GLuint vertex, fragment;
    int success;
    char infoLog[512];

    vertex = gl->glCreateShader(GL_VERTEX_SHADER);
    gl->glShaderSource(vertex, 1, &vShaderCode, NULL);
    gl->glCompileShader(vertex);
    gl->glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        gl->glGetShaderInfoLog(vertex, 512, NULL, infoLog);
        qWarning() << "  [!!!] VERTEX SHADER COMPILE FAILED:" << infoLog;
        return 0;
    }
    qDebug() << "  [OK] Vertex shader compiled.";

    fragment = gl->glCreateShader(GL_FRAGMENT_SHADER);
    gl->glShaderSource(fragment, 1, &fShaderCode, NULL);
    gl->glCompileShader(fragment);
    gl->glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        gl->glGetShaderInfoLog(fragment, 512, NULL, infoLog);
        qWarning() << "  [!!!] FRAGMENT SHADER COMPILE FAILED:" << infoLog;
        return 0;
    }
    qDebug() << "  [OK] Fragment shader compiled.";

    // 3. Link program
    GLuint programID = gl->glCreateProgram();
    gl->glAttachShader(programID, vertex);
    gl->glAttachShader(programID, fragment);
    gl->glLinkProgram(programID);
    gl->glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        gl->glGetProgramInfoLog(programID, 512, NULL, infoLog);
        qWarning() << "  [!!!] SHADER PROGRAM LINK FAILED:" << infoLog;
        return 0;
    }
    qDebug() << "  [OK] Shader program linked. ID:" << programID;

    gl->glDeleteShader(vertex);
    gl->glDeleteShader(fragment);
    return programID;
}

static void debugSampleCenterPixel(QOpenGLFunctions_4_3_Core* gl, GLuint cubemapID, int faceSize, int mipLevel, const QString& stageName) {
    if (!gl || faceSize <= 0 || cubemapID == 0) return;
    std::vector<float> pixelData(3, 0.0f);
    GLuint tempFBO;
    gl->glGenFramebuffers(1, &tempFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, cubemapID, mipLevel);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        gl->glReadPixels(faceSize / 2, faceSize / 2, 1, 1, GL_RGB, GL_FLOAT, pixelData.data());
    }
    else {
        qWarning() << "  [PIXEL TRACE] Could not read pixel for" << stageName << "- FBO incomplete.";
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &tempFBO);
    qDebug().noquote() << QString("  [PIXEL TRACE] Center pixel color after [%1]: R=%2, G=%3, B=%4")
        .arg(stageName, -22).arg(pixelData[0], 0, 'f', 4).arg(pixelData[1], 0, 'f', 4).arg(pixelData[2], 0, 'f', 4);
}

// ===================================================================================
// ==                           CUBEMAP IMPLEMENTATION                              ==
// ===================================================================================

static const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
static const glm::mat4 captureViews[] = {
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
};

Cubemap::Cubemap() = default;
Cubemap::~Cubemap() {
    if (_id != 0) {
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
        if (gl) { gl->glDeleteTextures(1, &_id); }
    }
}
GLuint Cubemap::getID() const { return _id; }
void Cubemap::bind(QOpenGLFunctions_4_3_Core* gl, unsigned int unit) const {
    gl->glActiveTexture(GL_TEXTURE0 + unit);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, _id);
}

// --- PASS 1: EQUIRECTANGULAR TO CUBEMAP ---
std::shared_ptr<Cubemap> Cubemap::fromEquirectangular(const Texture2D& hdrEquirect, QOpenGLFunctions_4_3_Core* gl, const std::string& vertPath, const std::string& fragPath, int faceSize)
{
    qDebug() << "\n[EQUIRECT DEBUG] Starting fromEquirectangular operation...";
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
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint eqShaderID = buildAndDebugShader(gl, vertPath, fragPath, "EQUIRECT");
    if (eqShaderID == 0) return nullptr;
    gl->glUseProgram(eqShaderID);

    GLint projLoc = gl->glGetUniformLocation(eqShaderID, "projection");
    GLint viewLoc = gl->glGetUniformLocation(eqShaderID, "view");
    GLint texLoc = gl->glGetUniformLocation(eqShaderID, "equirectangularMap");
    qDebug() << "  [Uniform Locations] projection:" << projLoc << "view:" << viewLoc << "equirectangularMap:" << texLoc;

    gl->glUniform1i(texLoc, 0);
    gl->glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(captureProjection));
    printMatrix(captureProjection, "projection");
    hdrEquirect.bind(0);

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);
    gl->glDepthFunc(GL_LEQUAL);

    for (unsigned int i = 0; i < 6; ++i) {
        qDebug() << "  Rendering face" << i;
        gl->glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(captureViews[i]));
        printMatrix(captureViews[i], "view");
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    gl->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    debugSampleCenterPixel(gl, cube->getID(), faceSize, 0, "fromEquirectangular");

    gl->glDepthFunc(GL_LESS);
    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);
    gl->glDeleteProgram(eqShaderID);
    return cube;
}

// --- PASS 2: IRRADIANCE CONVOLUTION ---
std::shared_ptr<Cubemap> Cubemap::convolveIrradiance(const Cubemap& source, QOpenGLFunctions_4_3_Core* gl, const std::string& vertPath, const std::string& fragPath, int faceSize)
{
    qDebug() << "\n[IRRADIANCE DEBUG] Starting convolveIrradiance operation...";
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

    GLuint convShaderID = buildAndDebugShader(gl, vertPath, fragPath, "IRRADIANCE");
    if (convShaderID == 0) return nullptr;
    gl->glUseProgram(convShaderID);

    GLint projLoc = gl->glGetUniformLocation(convShaderID, "projection");
    GLint viewLoc = gl->glGetUniformLocation(convShaderID, "view");
    GLint texLoc = gl->glGetUniformLocation(convShaderID, "environmentMap");
    qDebug() << "  [Uniform Locations] projection:" << projLoc << "view:" << viewLoc << "environmentMap:" << texLoc;

    gl->glUniform1i(texLoc, 0);
    gl->glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(captureProjection));
    printMatrix(captureProjection, "projection");
    source.bind(gl, 0);

    gl->glViewport(0, 0, faceSize, faceSize);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);
    gl->glDepthFunc(GL_LEQUAL);

    for (unsigned int i = 0; i < 6; ++i) {
        qDebug() << "  Rendering face" << i;
        gl->glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(captureViews[i]));
        printMatrix(captureViews[i], "view");
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, 0);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    debugSampleCenterPixel(gl, cube->getID(), faceSize, 0, "convolveIrradiance");
    gl->glDepthFunc(GL_LESS);
    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteRenderbuffers(1, &rbo);
    gl->glDeleteProgram(convShaderID);
    return cube;
}

// --- PASS 3: SPECULAR PREFILTER ---
std::shared_ptr<Cubemap> Cubemap::prefilter(const Cubemap& source, QOpenGLFunctions_4_3_Core* gl, const std::string& vertPath, const std::string& fragPath, int baseSize, int maxMipLevels)
{
    qDebug() << "\n[PREFILTER DEBUG] Starting prefilter operation...";
    auto cube = std::make_shared<Cubemap>();
    cube->_size = baseSize;
    cube->_levels = maxMipLevels;

    gl->glGenTextures(1, &cube->_id);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cube->_id);
    gl->glTexStorage2D(GL_TEXTURE_CUBE_MAP, maxMipLevels, GL_RGB16F, baseSize, baseSize); // Use immutable storage
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // OK to set now with immutable storage

    GLuint prefilterShaderID = buildAndDebugShader(gl, vertPath, fragPath, "PREFILTER");
    if (prefilterShaderID == 0) return nullptr;
    gl->glUseProgram(prefilterShaderID);

    GLint projLoc = gl->glGetUniformLocation(prefilterShaderID, "projection");
    GLint viewLoc = gl->glGetUniformLocation(prefilterShaderID, "view");
    GLint texLoc = gl->glGetUniformLocation(prefilterShaderID, "environmentMap");
    GLint roughLoc = gl->glGetUniformLocation(prefilterShaderID, "roughness");
    qDebug() << "  [Uniform Locations] projection:" << projLoc << "view:" << viewLoc << "environmentMap:" << texLoc << "roughness:" << roughLoc;

    gl->glUniform1i(texLoc, 0);
    gl->glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(captureProjection));
    printMatrix(captureProjection, "projection");
    source.bind(gl, 0);

    GLuint fbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLuint cubeVAO = GLUtils::getUnitCubeVAO(gl);
    gl->glBindVertexArray(cubeVAO);
    gl->glDisable(GL_DEPTH_TEST);

    for (int mip = 0; mip < maxMipLevels; ++mip) {
        int mipSize = static_cast<int>(baseSize * std::pow(0.5, mip));
        float roughness = (float)mip / (float)(maxMipLevels - 1);
        qDebug().noquote() << QString("  [Mip %1] Size: %2x%2, Roughness: %3").arg(mip).arg(mipSize).arg(roughness, 0, 'f', 4);

        gl->glViewport(0, 0, mipSize, mipSize);
        gl->glUniform1f(roughLoc, roughness);

        for (unsigned int i = 0; i < 6; ++i) {
            qDebug() << "    Rendering face" << i;
            gl->glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(captureViews[i]));
            printMatrix(captureViews[i], "view");
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cube->_id, mip);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            gl->glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    debugSampleCenterPixel(gl, cube->getID(), baseSize, 0, "prefilter (mip 0)");
    int lastMipSize = static_cast<int>(baseSize * std::pow(0.5, maxMipLevels - 1));
    debugSampleCenterPixel(gl, cube->getID(), lastMipSize, maxMipLevels - 1, "prefilter (last mip)");

    gl->glBindVertexArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteProgram(prefilterShaderID);
    return cube;
}