// =================================================================
//                      Mesh.hpp
// =================================================================
#pragma once

#include <vector>

class Mesh {
public:
    Mesh(const std::vector<float>& vertices);
    Mesh(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);

    const std::vector<float>& vertices() const { return m_vertices; }
    const std::vector<unsigned int>& indices() const { return m_indices; }
    bool hasIndices() const { return !m_indices.empty(); }

    static const std::vector<float>& getLitCubeVertices();
    static const std::vector<unsigned int>& getLitCubeIndices();

private:
    std::vector<float> m_vertices;
    std::vector<unsigned int> m_indices;
};

