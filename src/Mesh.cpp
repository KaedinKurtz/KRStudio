
// =================================================================
//                      Mesh.cpp
// =================================================================
#include "Mesh.hpp"
#include <QDebug>

// Constructor for simple meshes (Grid)
Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_EBO(0), m_vertexCount(0), m_indexCount(0), m_hasIndices(false)
{
    if (!m_gl || vertices.empty()) return;
    m_vertexCount = vertices.size() / 3;

    m_gl->glGenVertexArrays(1, &m_VAO);
    m_gl->glGenBuffers(1, &m_VBO);

    m_gl->glBindVertexArray(m_VAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Attribute 0: Vertex Positions
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);
}

// Implementation of the new 4-argument constructor for indexed meshes
Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices, const std::vector<unsigned int>& indices, bool hasNormals)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_EBO(0), m_vertexCount(0), m_indexCount(0), m_hasIndices(true)
{
    if (!m_gl || vertices.empty() || indices.empty()) return;

    m_indexCount = indices.size();
    // The number of floats per vertex depends on whether normals are present.
    m_vertexCount = vertices.size() / (hasNormals ? 6 : 3);

    m_gl->glGenVertexArrays(1, &m_VAO);
    m_gl->glGenBuffers(1, &m_VBO);
    m_gl->glGenBuffers(1, &m_EBO);

    m_gl->glBindVertexArray(m_VAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    if (hasNormals) {
        // Interleaved layout: [Pos.x, Pos.y, Pos.z, Norm.x, Norm.y, Norm.z]
        GLsizei stride = 6 * sizeof(float);
        // Attribute 0: Vertex Positions
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        m_gl->glEnableVertexAttribArray(0);
        // Attribute 1: Normal Vectors
        m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        m_gl->glEnableVertexAttribArray(1);
    }
    else {
        // Layout with only positions
        GLsizei stride = 3 * sizeof(float);
        m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        m_gl->glEnableVertexAttribArray(0);
    }

    m_gl->glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (!m_gl) return;
    if (m_VAO != 0) m_gl->glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO != 0) m_gl->glDeleteBuffers(1, &m_VBO);
    if (m_EBO != 0) m_gl->glDeleteBuffers(1, &m_EBO);
}

void Mesh::draw() {
    if (m_VAO == 0 || !m_gl) return;
    m_gl->glBindVertexArray(m_VAO);
    if (m_hasIndices) {
        m_gl->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    }
    else {
        m_gl->glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    }
    m_gl->glBindVertexArray(0);
}
