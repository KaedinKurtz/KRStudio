#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    unsigned int ID;

    Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath);
    ~Shader();

    void use();
    void setMat4(const std::string& name, const glm::mat4& mat) const;
    // --- ADDED MISSING FUNCTION ---
    void setVec4(const std::string& name, const glm::vec4& value) const;

private:
    QOpenGLFunctions_3_3_Core* m_gl;
    void checkCompileErrors(unsigned int shader, std::string type);
};