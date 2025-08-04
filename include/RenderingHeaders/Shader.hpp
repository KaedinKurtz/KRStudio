#pragma once

#include <string>
#include <vector>
#include <memory> // For std::unique_ptr
#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>

class Shader
{
public:
    // The shader program ID is the only public data member.
    unsigned int ID = 0;

    unsigned int id() const {
        return ID;
    }
    // --- Static Factory Methods ---
    // These are now the ONLY way to create a shader. They handle compilation and linking.
    static std::unique_ptr<Shader> build(QOpenGLFunctions_4_3_Core* gl, const std::vector<std::string>& paths);
    static std::unique_ptr<Shader> build(QOpenGLFunctions_4_3_Core* gl, const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath = "");

    // --- Destructor is now simple ---
    // It no longer makes OpenGL calls.
    ~Shader() = default;

    // --- Explicit Cleanup ---
    // This will be called by RenderingSystem::shutdown()
    void destroy(QOpenGLFunctions_4_3_Core* gl);
    void checkUniform(QOpenGLFunctions_4_3_Core* gl, const std::string& name);
    // --- MODIFIED: All public methods now take a GL functions pointer ---
    void use(QOpenGLFunctions_4_3_Core* gl);
    void setBool(QOpenGLFunctions_4_3_Core* gl, const std::string& name, bool value) const;
    void setInt(QOpenGLFunctions_4_3_Core* gl, const std::string& name, int value) const;
    void setFloat(QOpenGLFunctions_4_3_Core* gl, const std::string& name, float value) const;
    void setVec2(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec2& value) const;
    void setVec3(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec3& value) const;
    void setVec4(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::vec4& value) const;
    void setMat4(QOpenGLFunctions_4_3_Core* gl, const std::string& name, const glm::mat4& mat) const;

private:
    // --- Private Constructor ---
    // This ensures shaders can only be created via the static build methods.
    Shader() = default;

    // --- Private Helper ---
    // This now takes the gl pointer it needs to check for errors.
    static void checkCompileErrors(QOpenGLFunctions_4_3_Core* gl, unsigned int shader, const std::string& type);
};