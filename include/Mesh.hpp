#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <vector>

class Mesh {
public:
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices);
    ~Mesh();

    // The crucial draw call
    void draw();

private:
    QOpenGLFunctions_3_3_Core* m_gl;
    GLuint m_VAO, m_VBO;
    GLsizei m_vertexCount;
};