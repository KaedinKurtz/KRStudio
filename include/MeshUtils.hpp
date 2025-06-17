#pragma once
#include "components.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/vec3.hpp>  

inline void loadStlIntoRenderable(const std::string& path,
    RenderableMeshComponent& meshOut,
    bool recalcNormals = true)
{
    static Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals);

    if (!scene || !scene->HasMeshes())
        throw std::runtime_error("Failed to load " + path);

    const aiMesh* m = scene->mMeshes[0];
    meshOut.vertices.resize(m->mNumVertices);

    for (unsigned i = 0; i < m->mNumVertices; ++i) {
        meshOut.vertices[i].position = glm::vec3(      // ← changed
            m->mVertices[i].x,
            m->mVertices[i].y,
            m->mVertices[i].z);

        meshOut.vertices[i].normal = glm::vec3(      // ← changed
            m->mNormals[i].x,
            m->mNormals[i].y,
            m->mNormals[i].z);
    }

    meshOut.indices.reserve(m->mNumFaces * 3);
    for (unsigned f = 0; f < m->mNumFaces; ++f)
        for (unsigned k = 0; k < 3; ++k)
            meshOut.indices.push_back(m->mFaces[f].mIndices[k]);
}