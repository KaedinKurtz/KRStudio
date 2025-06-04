#include "Shader.hpp"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <vector> // For GLchar array in checkCompileErrors

Shader::Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath) : m_gl(gl), ID(0) {
    if (!m_gl) {
        qCritical() << "Shader constructor: Received null QOpenGLFunctions pointer!";
        return;
    }

    QString vertexCodeStr, fragmentCodeStr;
    QFile vShaderFile(vertexPath), fShaderFile(fragmentPath);

    if (!vShaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SHADER::FILE_NOT_READ:" << vertexPath; return;
    }
    vertexCodeStr = QTextStream(&vShaderFile).readAll(); vShaderFile.close();

    if (!fShaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SHADER::FILE_NOT_READ:" << fragmentPath; return;
    }
    fragmentCodeStr = QTextStream(&fShaderFile).readAll(); fShaderFile.close();

    QByteArray vBytes = vertexCodeStr.toUtf8(); const char* vCode = vBytes.constData();
    QByteArray fBytes = fragmentCodeStr.toUtf8(); const char* fCode = fBytes.constData();

    unsigned int vertex = 0, fragment = 0;

    vertex = m_gl->glCreateShader(GL_VERTEX_SHADER);
    m_gl->glShaderSource(vertex, 1, &vCode, NULL);
    m_gl->glCompileShader(vertex);
    if (!checkCompileErrors(vertex, "VERTEX")) {
        m_gl->glDeleteShader(vertex); return;
    }

    fragment = m_gl->glCreateShader(GL_FRAGMENT_SHADER);
    m_gl->glShaderSource(fragment, 1, &fCode, NULL);
    m_gl->glCompileShader(fragment);
    if (!checkCompileErrors(fragment, "FRAGMENT")) {
        m_gl->glDeleteShader(vertex); m_gl->glDeleteShader(fragment); return;
    }

    this->ID = m_gl->glCreateProgram();
    m_gl->glAttachShader(this->ID, vertex);
    m_gl->glAttachShader(this->ID, fragment);
    m_gl->glLinkProgram(this->ID);

    m_gl->glDetachShader(this->ID, vertex); // Detach after linking
    m_gl->glDetachShader(this->ID, fragment);

    if (!checkCompileErrors(this->ID, "PROGRAM")) { // This will set this->ID to 0 on failure
        // ID is already 0 if linking failed, no need to delete program ID 0
    }

    m_gl->glDeleteShader(vertex); // Delete shaders whether linking succeeded or failed
    m_gl->glDeleteShader(fragment);

    if (this->ID != 0) {
        qDebug() << "Shader compiled/linked successfully. ID:" << this->ID << "for" << vertexPath << "&" << fragmentPath;
    }
    else {
        qCritical() << "Shader program FAILED for" << vertexPath << "&" << fragmentPath;
    }
}

Shader::~Shader() {
    if (ID != 0 && m_gl) m_gl->glDeleteProgram(ID);
}

void Shader::use() {
    if (ID != 0 && m_gl) m_gl->glUseProgram(ID);
}

// Uniform setters (ensure all check ID != 0 && m_gl)
void Shader::setBool(const std::string& name, bool value) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform1i(m_gl->glGetUniformLocation(ID, name.c_str()), (int)value);
}
void Shader::setInt(const std::string& name, int value) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform1i(m_gl->glGetUniformLocation(ID, name.c_str()), value);
}
void Shader::setFloat(const std::string& name, float value) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform1f(m_gl->glGetUniformLocation(ID, name.c_str()), value);
}
void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform3fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform3f(m_gl->glGetUniformLocation(ID, name.c_str()), x, y, z);
}
void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniform4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    if (ID == 0 || !m_gl) return;
    m_gl->glUniformMatrix4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

bool Shader::checkCompileErrors(unsigned int objectID, std::string type) {
    if (!m_gl) return false;
    GLint success;
    std::vector<GLchar> infoLog(1024);
    if (type != "PROGRAM") {
        m_gl->glGetShaderiv(objectID, GL_COMPILE_STATUS, &success);
        if (!success) {
            m_gl->glGetShaderInfoLog(objectID, 1024, NULL, infoLog.data());
            qWarning() << "SHADER_COMPILATION_ERROR (" << type.c_str() << "):\n" << infoLog.data();
            return false;
        }
    }
    else {
        m_gl->glGetProgramiv(objectID, GL_LINK_STATUS, &success);
        if (!success) {
            m_gl->glGetProgramInfoLog(objectID, 1024, NULL, infoLog.data());
            qWarning() << "PROGRAM_LINKING_ERROR:\n" << infoLog.data();
            if (objectID == this->ID) this->ID = 0; // Invalidate program ID on link failure
            return false;
        }
    }
    return true;
}
