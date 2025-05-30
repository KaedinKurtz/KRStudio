#pragma once
#include <string>
#include <glm/glm.hpp>

class QOpenGLFunctions_3_3_Core;

class Shader
{
public:
    unsigned int ID;
    Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath);
    void use();
    void setMat4(const std::string& name, const glm::mat4& mat) const;
    void setVec4(const std::string& name, const glm::vec4& value) const; // <-- Add this line

private:
    void checkCompileErrors(unsigned int shader, std::string type);
    QOpenGLFunctions_3_3_Core* m_gl;
};