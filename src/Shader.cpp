#include "Shader.hpp"
#include <QOpenGLFunctions_3_3_Core>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector> // Needed for the dynamic error buffer
#include <QDebug>

Shader::Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath)
    : m_gl(gl), ID(0)
{
    if (!m_gl) {
        throw std::runtime_error("OpenGL functions pointer is null.");
    }

    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
    {
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;
        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();
        vShaderFile.close();
        fShaderFile.close();
        vertexCode = vShaderStream.str();
        fragmentCode = fShaderStream.str();
    }
    catch (std::ifstream::failure& e)
    {
        throw std::runtime_error(std::string("SHADER::FILE_NOT_READ: ") + vertexPath + " or " + fragmentPath);
    }
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    unsigned int vertex, fragment;

    vertex = m_gl->glCreateShader(GL_VERTEX_SHADER);
    m_gl->glShaderSource(vertex, 1, &vShaderCode, NULL);
    m_gl->glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    fragment = m_gl->glCreateShader(GL_FRAGMENT_SHADER);
    m_gl->glShaderSource(fragment, 1, &fShaderCode, NULL);
    m_gl->glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    ID = m_gl->glCreateProgram();
    m_gl->glAttachShader(ID, vertex);
    m_gl->glAttachShader(ID, fragment);
    m_gl->glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    m_gl->glDeleteShader(vertex);
    m_gl->glDeleteShader(fragment);
}

Shader::~Shader()
{
    if (m_gl && ID != 0) {
        m_gl->glDeleteProgram(ID);
    }
}

void Shader::use()
{
    if (m_gl) m_gl->glUseProgram(ID);
}

void Shader::setBool(const std::string& name, bool value) const { if (m_gl) m_gl->glUniform1i(m_gl->glGetUniformLocation(ID, name.c_str()), (int)value); }
void Shader::setInt(const std::string& name, int value) const { if (m_gl) m_gl->glUniform1i(m_gl->glGetUniformLocation(ID, name.c_str()), value); }
void Shader::setFloat(const std::string& name, float value) const { if (m_gl) m_gl->glUniform1f(m_gl->glGetUniformLocation(ID, name.c_str()), value); }
void Shader::setMat4(const std::string& name, const glm::mat4& mat) const { if (m_gl) m_gl->glUniformMatrix4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]); }
void Shader::setVec3(const std::string& name, const glm::vec3& value) const { if (m_gl) m_gl->glUniform3fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]); }

// This function is now memory-safe. It dynamically allocates a buffer
// of the correct size for the error log, preventing any buffer overflows.
bool Shader::checkCompileErrors(unsigned int shader, std::string type)
{
    int success;
    if (type != "PROGRAM")
    {
        m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            int logLength = 0;
            m_gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            if (logLength <= 0) logLength = 1;                 // safety
            std::vector<char> infoLog(static_cast<size_t>(logLength) + 1, 0);
            m_gl->glGetShaderInfoLog(shader, logLength + 1, nullptr,
                infoLog.data());
            throw std::runtime_error("SHADER::" + type +
               "::COMPILATION_FAILED\n" +
               std::string(infoLog.data()));
        }
    }
    else
    {
        m_gl->glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success)
        {
            int logLength = 0;
            m_gl->glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            if (logLength <= 0) logLength = 1;                 // safety
            std::vector<char> infoLog(static_cast<size_t>(logLength) + 1, 0);
            m_gl->glGetProgramInfoLog(shader, logLength + 1, nullptr,
                infoLog.data());
            throw std::runtime_error("SHADER::" + type +
                "::COMPILATION_FAILED\n" +
                std::string(infoLog.data()));
        }
    }
    return true;
}
