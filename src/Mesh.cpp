#include "Mesh.hpp"

Mesh::Mesh(const std::vector<float>& vertices) : m_VAO(0), m_VBO(0), m_VertexCount(0) {
    if (!vertices.empty()) {
        // In this simple case, each vertex is just 3 floats (x, y, z).
        m_VertexCount = static_cast<int>(vertices.size() / 3);
        setupMesh(vertices);
    }
}

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void Mesh::setupMesh(const std::vector<float>& vertices) {
    // 1. Create VAO and VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    // 2. Bind VAO and VBO
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    // 3. Copy vertex data into VBO
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // 4. Set vertex attribute pointers
    // Position attribute (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 5. Unbind VAO to prevent accidental modification
    glBindVertexArray(0);
}

void Mesh::draw() const {
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
    glBindVertexArray(0); // Unbind after drawing for good practice
}