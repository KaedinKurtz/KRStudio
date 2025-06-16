#pragma once
#include <glm/glm.hpp>
#include <string>

// Forward-declaration to avoid including heavy Qt headers here.
class QOpenGLFunctions_3_3_Core;

class Shader
{
public:
    unsigned int ID;

    // The constructor takes a pointer to the OpenGL function implementation.
    Shader(QOpenGLFunctions_3_3_Core* gl, const char* vertexPath, const char* fragmentPath);
    // The destructor is now declared to be implemented in the .cpp file.
    ~Shader();

    void use();

    // Uniform setter functions
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;

private:
    QOpenGLFunctions_3_3_Core* m_gl; // Non-owning pointer to the GL context

    // Utility function for checking shader compilation/linking errors.
    // The implementation is now memory-safe.
    bool checkCompileErrors(unsigned int shader, std::string type);
};
