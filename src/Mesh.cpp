#include "Mesh.hpp"
#include <QOpenGLFunctions_3_3_Core>

Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_VertexCount(0)
{
    if (!vertices.empty()) {
        m_VertexCount = static_cast<int>(vertices.size() / 3);
        setupMesh(vertices);
    }
}

Mesh::~Mesh() {
    // This can cause issues if the GL context is already destroyed.
    // In a real app, resource management needs care. For now, we let it be.
    if (m_gl) {
        m_gl->glDeleteVertexArrays(1, &m_VAO);
        m_gl->glDeleteBuffers(1, &m_VBO);
    }
}

void Mesh::setupMesh(const std::vector<float>& vertices) {
    m_gl->glGenVertexArrays(1, &m_VAO);
    m_gl->glGenBuffers(1, &m_VBO);
    m_gl->glBindVertexArray(m_VAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);
}

void Mesh::draw() const {
    m_gl->glBindVertexArray(m_VAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
    m_gl->glBindVertexArray(0);
}