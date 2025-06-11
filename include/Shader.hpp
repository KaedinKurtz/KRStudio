#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    unsigned int ID;

    Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath);
    ~Shader();

    // Deleted copy functions
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use();
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;

    // Added the missing function required by Robot.cpp
    void setVec4(const std::string& name, const glm::vec4& value) const;

private:
    QOpenGLFunctions_3_3_Core* m_gl;
    bool checkCompileErrors(unsigned int objectID, std::string type);
};