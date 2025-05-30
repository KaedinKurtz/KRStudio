#include "Shader.hpp"
#include <QOpenGLFunctions_3_3_Core>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath) : m_gl(gl)
{
    // ... constructor code is unchanged ...
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
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
    catch (std::ifstream::failure& e) {
        std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
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

void Shader::use() {
    m_gl->glUseProgram(ID);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    m_gl->glUniformMatrix4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

// Add this new function implementation
void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    m_gl->glUniform4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}

void Shader::checkCompileErrors(unsigned int shader, std::string type) {
    // ... this function is unchanged ...
    int success;
    char infoLog[1024];
    if (type != "PROGRAM") {
        m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            m_gl->glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
    else {
        m_gl->glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            m_gl->glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
}