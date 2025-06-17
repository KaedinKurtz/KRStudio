#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>         
#include <QtOpenGL/QOpenGLFunctions_4_1_Core>

// Forward-declaration to avoid including heavy Qt headers here.
class QOpenGLFunctions_4_1_Core;

class Shader
{
public:
    unsigned int ID;
    Shader(QOpenGLFunctions_4_1_Core* gl,
        const char* vs, const char* fs);
    // The constructor takes a pointer to the OpenGL function implementation.
    Shader(QOpenGLFunctions_4_1_Core* gl,
        const std::vector<std::string>& paths);
    static std::unique_ptr<Shader> buildTessellatedShader(
        QOpenGLFunctions_4_1_Core* gl,
        const char* vsPath,
        const char* tcsPath,
        const char* tesPath,
        const char* fsPath);
    // The destructor is now declared to be implemented in the .cpp file.
    ~Shader();

    void use();
    GLint  getLoc(const char* name) const;

    // Uniform setter functions
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;
	void setVec4(const std::string& name, const glm::vec4& value) const;
	void setVec2(const std::string& name, const glm::vec2& value) const;

protected:
    Shader() = default;

private:
    

    QOpenGLFunctions_4_1_Core* m_gl = nullptr;
    // Utility function for checking shader compilation/linking errors.
    // The implementation is now memory-safe.
    bool checkCompileErrors(unsigned int shader, std::string type);
};
