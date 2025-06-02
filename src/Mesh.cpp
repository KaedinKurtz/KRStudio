#include "Mesh.hpp"
#include <QDebug>

Mesh::Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices)
    : m_gl(gl), m_VAO(0), m_VBO(0), m_vertexCount(0)
{
    if (!m_gl) {
        qCritical() << "Mesh received null QOpenGLFunctions!";
        return;
    }

    if (vertices.empty() || vertices.size() % 3 != 0) {
        qWarning() << "Mesh created with invalid vertex data.";
        return;
    }
    m_vertexCount = static_cast<GLsizei>(vertices.size() / 3);

    m_gl->glGenVertexArrays(1, &m_VAO);
    m_gl->glGenBuffers(1, &m_VBO);

    m_gl->glBindVertexArray(m_VAO);

    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m_gl->glEnableVertexAttribArray(0);

    m_gl->glBindVertexArray(0);

    // --- CORRECTED LOGGING ---
    qDebug() << "Mesh created with VAO ID:" << m_VAO;
}

Mesh::~Mesh() {
    if (!m_gl) return;
    if (m_VAO != 0) {
        m_gl->glDeleteVertexArrays(1, &m_VAO);
    }
    if (m_VBO != 0) {
        m_gl->glDeleteBuffers(1, &m_VBO);
    }
    qDebug() << "Mesh with VAO ID:" << m_VAO << "deleted.";
}

void Mesh::draw() {
    if (m_VAO == 0) return; // Don't draw if VAO wasn't created

    m_gl->glBindVertexArray(m_VAO);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    m_gl->glBindVertexArray(0);
}