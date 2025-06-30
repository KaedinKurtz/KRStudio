#include "Shader.hpp"
#include <QOpenGLFunctions_4_1_Core>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector> // Needed for the dynamic error buffer
#include <QDebug>


Shader::Shader(QOpenGLFunctions_4_1_Core* gl,
    const char* vertexPath,
    const char* fragmentPath)
    : m_gl(gl)
{
    if (!m_gl)
        throw std::runtime_error("OpenGL functions pointer is null.");

    /* ── 1. Read shader files into strings ─────────────────────────────── */
    std::string   vertexCode;
    std::string   fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;

    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try
    {
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);

        std::stringstream vStream, fStream;
        vStream << vShaderFile.rdbuf();
        fStream << fShaderFile.rdbuf();

        vertexCode = vStream.str();
        fragmentCode = fStream.str();
    }
    catch (std::ifstream::failure&)
    {
        throw std::runtime_error(
            std::string("SHADER::FILE_NOT_READ: ") +
            vertexPath + " or " + fragmentPath);
    }

    const char* vSrc = vertexCode.c_str();
    const char* fSrc = fragmentCode.c_str();

    /* ── 2. Compile shaders ────────────────────────────────────────────── */
    GLuint vertex = m_gl->glCreateShader(GL_VERTEX_SHADER);
    m_gl->glShaderSource(vertex, 1, &vSrc, nullptr);
    m_gl->glCompileShader(vertex);
    this->checkCompileErrors(vertex, "VERTEX");

    GLuint fragment = m_gl->glCreateShader(GL_FRAGMENT_SHADER);
    m_gl->glShaderSource(fragment, 1, &fSrc, nullptr);
    m_gl->glCompileShader(fragment);
    this->checkCompileErrors(fragment, "FRAGMENT");

    /* ── 3. Link program ───────────────────────────────────────────────── */
    ID = m_gl->glCreateProgram();
    m_gl->glAttachShader(ID, vertex);
    m_gl->glAttachShader(ID, fragment);
    m_gl->glLinkProgram(ID);
    this->checkCompileErrors(ID, "PROGRAM");

    /* ── 4. Cleanup ────────────────────────────────────────────────────── */
    m_gl->glDeleteShader(vertex);
    m_gl->glDeleteShader(fragment);
}

Shader::Shader(QOpenGLFunctions_4_1_Core* gl,
    const std::vector<std::string>& paths)
    : m_gl(gl), ID(0)
{
    if (!m_gl) throw std::runtime_error("GL ptr null.");

    auto stageFromName = [](const std::string& p)->GLenum
        {
            if (p.find("_vert") != std::string::npos) return GL_VERTEX_SHADER;
            if (p.find("_tesc") != std::string::npos) return GL_TESS_CONTROL_SHADER;
            if (p.find("_tese") != std::string::npos) return GL_TESS_EVALUATION_SHADER;
            if (p.find("_geom") != std::string::npos) return GL_GEOMETRY_SHADER;
            if (p.find("_frag") != std::string::npos) return GL_FRAGMENT_SHADER;
            throw std::runtime_error("Cannot infer stage from name: " + p);
        };

    std::vector<GLuint> shaders; shaders.reserve(paths.size());

    for (const auto& file : paths)
    {
        std::ifstream ifs(file, std::ios::in | std::ios::binary);
        if (!ifs) throw std::runtime_error("Cannot open " + file);
        std::stringstream ss; ss << ifs.rdbuf();
        std::string src = ss.str();

        GLuint sh = m_gl->glCreateShader(stageFromName(file));
        const char* csrc = src.c_str();
        m_gl->glShaderSource(sh, 1, &csrc, nullptr);
        m_gl->glCompileShader(sh);
        checkCompileErrors(sh, "STAGE");
        shaders.push_back(sh);
    }

    ID = m_gl->glCreateProgram();
    for (auto sh : shaders) m_gl->glAttachShader(ID, sh);
    m_gl->glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");
    for (auto sh : shaders) m_gl->glDeleteShader(sh);
}

GLint Shader::getLoc(const char* name) const
{
    GLint loc = m_gl->glGetUniformLocation(ID, name);
    if (loc < 0)
        throw std::runtime_error(std::string("Uniform not found: ") + name);
    return loc;
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
void Shader::setVec4(const std::string& name, const glm::vec4& value) const { if (m_gl) m_gl->glUniform4fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]); }
void Shader::setVec2(const std::string& name, const glm::vec2& value) const { if (m_gl) m_gl->glUniform2fv(m_gl->glGetUniformLocation(ID, name.c_str()), 1, &value[0]); }

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

std::unique_ptr<Shader> Shader::buildGeometryShader(QOpenGLFunctions_4_1_Core* gl, const std::string& vertexPath, const std::string& geometryPath, const std::string& fragmentPath)
{
    // This is a simplified constructor path for shaders with a geometry stage.
    // It's created as a static function for convenience.
    std::vector<std::string> paths = { vertexPath, geometryPath, fragmentPath };
    return std::make_unique<Shader>(gl, paths);
}

std::unique_ptr<Shader> Shader::buildTessellatedShader(
    QOpenGLFunctions_4_1_Core* gl,
    const char* vsPath,
    const char* tcsPath,
    const char* tesPath,
    const char* fsPath)
{
    auto loadFile = [](const char* p) -> std::string
        {
            std::ifstream in(p, std::ios::in | std::ios::binary);
            if (!in) throw std::runtime_error(std::string("Cannot open ") + p);
            std::ostringstream s;  s << in.rdbuf();  return s.str();
        };

    std::string vsCode = loadFile(vsPath);
    std::string tcsCode = loadFile(tcsPath);
    std::string tesCode = loadFile(tesPath);
    std::string fsCode = loadFile(fsPath);

    auto check = [&](GLuint obj, bool isProgram, const char* label)
        {
            GLint ok = 0;
            if (!isProgram)
            {
                gl->glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
                if (!ok)
                {
                    GLint len = 0;
                    gl->glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
                    std::string log(len > 0 ? len : 1, '\0');
                    gl->glGetShaderInfoLog(obj, len, nullptr, log.data());
                    throw std::runtime_error(
                        std::string("SHADER::") + label +
                        "::COMPILATION_FAILED\n" + log);
                }
            }
            else
            {
                gl->glGetProgramiv(obj, GL_LINK_STATUS, &ok);
                if (!ok)
                {
                    GLint len = 0;
                    gl->glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
                    std::string log(len > 0 ? len : 1, '\0');
                    gl->glGetProgramInfoLog(obj, len, nullptr, log.data());
                    throw std::runtime_error(
                        std::string("SHADER::PROGRAM::LINK_FAILED\n") + log);
                }
            }
        };

    auto compile = [&](const std::string& src, GLenum type) -> GLuint
        {
            GLuint id = gl->glCreateShader(type);
            const char* csrc = src.c_str();
            gl->glShaderSource(id, 1, &csrc, nullptr);
            gl->glCompileShader(id);

            check(id, /*isProgram=*/false,
                type == GL_VERTEX_SHADER ? "VERTEX" :
                type == GL_TESS_CONTROL_SHADER ? "TESS_CTRL" :
                type == GL_TESS_EVALUATION_SHADER ? "TESS_EVAL" :
                /* else */                          "FRAGMENT");
            return id;
        };

    GLuint vs = compile(vsCode, GL_VERTEX_SHADER);
    GLuint tcs = compile(tcsCode, GL_TESS_CONTROL_SHADER);
    GLuint tes = compile(tesCode, GL_TESS_EVALUATION_SHADER);
    GLuint fs = compile(fsCode, GL_FRAGMENT_SHADER);

    GLuint prog = gl->glCreateProgram();
    gl->glAttachShader(prog, vs);
    gl->glAttachShader(prog, tcs);
    gl->glAttachShader(prog, tes);
    gl->glAttachShader(prog, fs);
    gl->glLinkProgram(prog);
    check(prog, /*isProgram=*/true, "PROGRAM");

    gl->glDeleteShader(vs);
    gl->glDeleteShader(tcs);
    gl->glDeleteShader(tes);
    gl->glDeleteShader(fs);

    auto sh = std::unique_ptr<Shader>(new Shader);
    // private default-ctor substitute: we immediately patch its members
    sh->m_gl = gl;
    sh->ID = prog;
    return sh;
}