#include "Shader.hpp"
#include <QFile>
#include <QTextStream>
#include <QDebug>

Shader::Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath) : m_gl(gl), ID(0) {
    if (!m_gl) {
        qCritical() << "Shader received null QOpenGLFunctions!";
        return;
    }

    QString vertexCode;
    QString fragmentCode;
    QFile vShaderFile(vertexPath);
    QFile fShaderFile(fragmentPath);

    if (vShaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream vStream(&vShaderFile);
        vertexCode = vStream.readAll();
        vShaderFile.close();
    }
    else {
        qWarning() << "SHADER::FILE_NOT_SUCCESFULLY_READ:" << vertexPath;
        return;
    }

    if (fShaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream fStream(&fShaderFile);
        fragmentCode = fStream.readAll();
        fShaderFile.close();
    }
    else {
        qWarning() << "SHADER::FILE_NOT_SUCCESFULLY_READ:" << fragmentPath;
        return;
    }

    QByteArray vShaderCodeBytes = vertexCode.toUtf8();
    const char* vShaderCode = vShaderCodeBytes.constData();
    QByteArray fShaderCodeBytes = fragmentCode.toUtf8();
    const char* fShaderCode = fShaderCodeBytes.constData();

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
    // --- CORRECTED LOGGING ---
    qDebug() << "Shader compiled and linked with new ID:" << ID;
}

Shader::~Shader() {
    if (ID != 0 && m_gl) {
        m_gl->glDeleteProgram(ID);
        qDebug() << "Shader program" << ID << "deleted.";
    }
}


void Shader::use() {
    if (ID) m_gl->glUseProgram(ID);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    GLint loc = m_gl->glGetUniformLocation(ID, name.c_str());
    if (loc != -1) {
        m_gl->glUniformMatrix4fv(loc, 1, GL_FALSE, &mat[0][0]);
    }
}

// --- ADDED MISSING FUNCTION IMPLEMENTATION ---
void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    GLint loc = m_gl->glGetUniformLocation(ID, name.c_str());
    if (loc != -1) {
        m_gl->glUniform4fv(loc, 1, &value[0]);
    }
}

void Shader::checkCompileErrors(unsigned int shader, std::string type) {
    int success;
    char infoLog[1024];
    if (type != "PROGRAM") {
        m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            m_gl->glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            qWarning() << "SHADER_COMPILATION_ERROR of type:" << QString::fromStdString(type) << "\n" << infoLog;
        }
    }
    else {
        m_gl->glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            m_gl->glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            qWarning() << "PROGRAM_LINKING_ERROR of type:" << QString::fromStdString(type) << "\n" << infoLog;
        }
    }
}