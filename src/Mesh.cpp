#include "Mesh.hpp"
#include <QDebug>

void Mesh::setupMeshBuffers(const std::vector<float>& vertices) {
    if (!m_gl) { qCritical() << "Mesh::setupMeshBuffers - m_gl is null!"; return; }
    m_gl->glGenVertexArrays(1, &m_VAO);
    m_gl->glGenBuffers(1, &m_VBO);
    m_gl->glBindVertexArray(m_VAO);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glBindVertexArray(0);
}

Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_EBO(0),
    m_vertexCount(0), m_indexCount(0), m_hasIndices(false) {
    if (!m_gl) { qCritical() << "Mesh (non-indexed) constructor: m_gl is null!"; return; }
    if (vertices.empty() || vertices.size() % 3 != 0) {
        qWarning() << "Mesh (non-indexed) created with invalid vertex data."; return;
    }
    setupMeshBuffers(vertices);
    m_vertexCount = vertices.size() / 3;
    qDebug() << "Mesh (non-indexed) Constructor: VAO ID:" << m_VAO << ", Vertex count:" << m_vertexCount;
}

Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_EBO(0),
    m_vertexCount(0), m_indexCount(0), m_hasIndices(true) {
    if (!m_gl) { qCritical() << "Mesh (indexed) constructor: m_gl is null!"; return; }
    if (vertices.empty()) { qWarning() << "Mesh (indexed) created with empty vertex data."; return; }
    if (indices.empty()) { qWarning() << "Mesh (indexed) created with empty index data."; m_hasIndices = false; return; }

    setupMeshBuffers(vertices);
    m_indexCount = indices.size();
    m_vertexCount = vertices.size() / 3; // Still useful for info

    m_gl->glGenBuffers(1, &m_EBO);
    m_gl->glBindVertexArray(m_VAO); // EBO setup needs VAO to be bound
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    m_gl->glBindVertexArray(0);
    qDebug() << "Mesh (indexed) Constructor: VAO ID:" << m_VAO << ", EBO ID:" << m_EBO << ", Index count:" << m_indexCount;
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
    if (m_hasIndices && m_EBO != 0 && m_indexCount > 0) {
        m_gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, (void*)0);
    }
    else if (!m_hasIndices && m_vertexCount > 0) {
        m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertexCount));
    }
    m_gl->glBindVertexArray(0);
}
