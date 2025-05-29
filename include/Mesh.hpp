#pragma once

#include <glad/glad.h>
#include <vector>

class Mesh {
public:
    // Constructor: Takes a vector of floats representing vertex positions.
    Mesh(const std::vector<float>& vertices);

    // Destructor: Cleans up the OpenGL buffer objects.
    ~Mesh();

    // Deleted copy constructor and assignment operator to prevent shallow copies
    // of OpenGL objects, which would lead to double-freeing resources.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Renders the mesh.
    void draw() const;

private:
    unsigned int m_VAO;
    unsigned int m_VBO;
    int m_VertexCount;

    // Helper function to set up the OpenGL buffers.
    void setupMesh(const std::vector<float>& vertices);
};