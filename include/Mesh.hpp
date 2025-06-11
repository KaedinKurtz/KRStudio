// =================================================================
//                      Mesh.hpp
// =================================================================
#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <vector>

class Mesh {
public:
    // Constructor for simple, non-indexed, non-lit meshes (e.g., the grid)
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices);

    // --- THIS IS THE FIX ---
    // A default value has been added to the 'hasNormals' parameter.
    // This allows this constructor to be called with either 3 or 4 arguments.
    // When called with 3 arguments (from Grid.cpp), 'hasNormals' will default to false.
    // When called with 4 arguments (from ViewportWidget.cpp), the provided value will be used.
    Mesh(QOpenGLFunctions_3_3_Core* gl, const std::vector<float>& vertices, const std::vector<unsigned int>& indices, bool hasNormals = false);

    ~Mesh();
    void draw();

private:
    QOpenGLFunctions_3_3_Core* m_gl;
    unsigned int m_VAO, m_VBO, m_EBO;
    size_t m_vertexCount, m_indexCount;
    bool m_hasIndices;
};

