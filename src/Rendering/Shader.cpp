#include "Shader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <QDebug>
#include <glm/gtc/type_ptr.hpp>

// --- Primary Static Build Method ---
std::unique_ptr<Shader> Shader::build(QOpenGLFunctions_4_3_Core* gl, const std::vector<std::string>& paths)
{
    if (!gl) {
        throw std::runtime_error("Shader::build called with null OpenGL functions pointer.");
    }

    auto stageFromName = [](const std::string& p) -> GLenum {
        if (p.find("_vert") != std::string::npos) return GL_VERTEX_SHADER;
        if (p.find("_tesc") != std::string::npos) return GL_TESS_CONTROL_SHADER;
        if (p.find("_tese") != std::string::npos) return GL_TESS_EVALUATION_SHADER;
        if (p.find("_geom") != std::string::npos) return GL_GEOMETRY_SHADER;
        if (p.find("_frag") != std::string::npos) return GL_FRAGMENT_SHADER;
        if (p.find("_comp") != std::string::npos) return GL_COMPUTE_SHADER;
        qWarning() << "Cannot infer shader stage from name:" << QString::fromStdString(p) << ". Defaulting to FRAGMENT.";
        return GL_FRAGMENT_SHADER; // Provide a default to avoid exceptions on unknown types
        };

    std::vector<GLuint> shaderIDs;
    shaderIDs.reserve(paths.size());

    for (const auto& path : paths) {
        if (path.empty()) continue;

        std::string code;
        std::ifstream shaderFile;
        shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            shaderFile.open(path);
            std::stringstream shaderStream;
            shaderStream << shaderFile.rdbuf();
            shaderFile.close();
            code = shaderStream.str();
        }
        catch (const std::ifstream::failure& e) {
            throw std::runtime_error("SHADER::FILE_NOT_SUCCESSFULLY_READ: " + path);
        }

        const char* shaderCode = code.c_str();
        GLuint shaderID = gl->glCreateShader(stageFromName(path));
        gl->glShaderSource(shaderID, 1, &shaderCode, NULL);
        gl->glCompileShader(shaderID);
        checkCompileErrors(gl, shaderID, "SHADER"); // Pass gl pointer
        shaderIDs.push_back(shaderID);
    }

    // Create shader program
    auto shader = std::unique_ptr<Shader>(new Shader()); // Use private constructor
    shader->ID = gl->glCreateProgram();
    for (GLuint shaderID : shaderIDs) {
        gl->glAttachShader(shader->ID, shaderID);
    }
    gl->glLinkProgram(shader->ID);
    checkCompileErrors(gl, shader->ID, "PROGRAM"); // Pass gl pointer

    // Delete the shaders as they're linked into our program now and no longer necessary
    for (GLuint shaderID : shaderIDs) {
        gl->glDeleteShader(shaderID);
    }

    return shader;
}

// --- Convenience Overload for simple Vertex/Fragment shaders ---
std::unique_ptr<Shader> Shader::build(QOpenGLFunctions_4_3_Core* gl, const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath)
{
    std::vector<std::string> paths = { vertexPath, fragmentPath };
    if (!geometryPath.empty()) {
        paths.push_back(geometryPath);
    }
    return build(gl, paths);
}

// --- Explicit Cleanup Method ---
void Shader::destroy(QOpenGLFunctions_4_3_Core* gl)
{
    if (gl && ID != 0) {
        gl->glDeleteProgram(ID);
        ID = 0;
    }
}

// --- All public methods now use the passed-in gl pointer ---
void Shader::use(QOpenGLFunctions_4_3_Core* gl)
{
    if (gl) gl->glUseProgram(ID);
}

void Shader::setBool(QOpenGLFunctions_4_3_Core* gl, const std::string& name, bool value) const
{
    if (gl) gl->glUniform1i(gl->glGetUniformLocation(ID, name.c_str()), (int)value);
}

void Shader::setInt(QOpenGLFunctions_4_3_Core* gl, const std::string& name, int value) const
{
    if (gl) gl->glUniform1i(gl->glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setFloat(QOpenGLFunctions_4_3_Core* gl, const std::string& name, float value) const
{
    if (gl) gl->glUniform1f(gl->glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setVec2(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec2& value) const
{
    if (gl) gl->glUniform2fv(gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}

void Shader::setVec3(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec3& value) const
{
    if (gl) gl->glUniform3fv(gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}

void Shader::setVec4(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec4& value) const
{
    if (gl) gl->glUniform4fv(gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}

void Shader::setMat4(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::mat4& mat) const
{
    if (gl) gl->glUniformMatrix4fv(gl->glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

// --- The private helper now takes the gl pointer it needs ---
void Shader::checkCompileErrors(QOpenGLFunctions_4_3_Core* gl, unsigned int shader, const std::string& type)
{
    GLint success;
    GLint logLength = 0;
    if (type != "PROGRAM") {
        gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> infoLog(logLength);
            gl->glGetShaderInfoLog(shader, logLength, &logLength, &infoLog[0]);
            throw std::runtime_error("SHADER::" + type + "::COMPILATION_FAILED\n" + std::string(infoLog.begin(), infoLog.end()));
        }
    }
    else {
        gl->glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            gl->glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> infoLog(logLength);
            gl->glGetProgramInfoLog(shader, logLength, &logLength, &infoLog[0]);
            throw std::runtime_error("SHADER::PROGRAM::LINKING_FAILED\n" + std::string(infoLog.begin(), infoLog.end()));
        }
    }
}