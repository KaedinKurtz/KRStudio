#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <vector>
// #include <QtGui/qopengl.h> // For GLuint, GLsizei if not from QOpenGLFunctions

class Mesh {
public:
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices);
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();
    void draw();

private:
    void setupMeshBuffers(const std::vector<float>& vertices);
    QOpenGLFunctions_3_3_Core* m_gl;
    unsigned int m_VAO; // Using unsigned int as GLuint might require extra include
    unsigned int m_VBO;
    unsigned int m_EBO;
    size_t m_vertexCount; // For glDrawArrays (number of vertices)
    size_t m_indexCount;  // For glDrawElements (number of indices)
    bool m_hasIndices;
};
