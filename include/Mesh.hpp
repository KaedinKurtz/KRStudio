#pragma once
#include <vector>

class QOpenGLFunctions_3_3_Core;

class Mesh {
public:
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;
private:
    void setupMesh(const std::vector<float>& vertices);
    QOpenGLFunctions_3_3_Core* m_gl;
    unsigned int m_VAO, m_VBO, m_VertexCount;
};