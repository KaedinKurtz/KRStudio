#pragma once
#include "components.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <glm/vec3.hpp>  
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN   // optional: slim down <windows.h>
#    include <windows.h>
#endif
#include <cstdio>

inline void earlyLog(const char* msg)
{
#ifdef _WIN32
    ::OutputDebugStringA(msg);
    ::OutputDebugStringA("\n");
#endif
    std::fprintf(stderr, "%s\n", msg);
    std::fflush(stderr);
}

inline void loadStlIntoRenderable(QString                qtPath,
    RenderableMeshComponent& meshOut,
    bool                    recalcNormals = true)
{
    static Assimp::Importer importer;

    constexpr unsigned Flags =
        aiProcess_Triangulate
        | aiProcess_JoinIdenticalVertices
        | aiProcess_GenSmoothNormals;

    const aiScene* scene = nullptr;

    /* ---- 1) Qt resource ?  --------------------------------------------- */
    if (qtPath.startsWith(u":/"))               //  »:/external/…« etc.
    {
        QFile f(qtPath);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                "Failed to open Qt resource \"" + qtPath.toStdString() + "\"");

        QByteArray bytes = f.readAll();
        scene = importer.ReadFileFromMemory(
            bytes.constData(),
            static_cast<size_t>(bytes.size()),
            Flags,
            "stl");                     // file type hint is mandatory
    }
    /* ---- 2) plain file on disk ----------------------------------------- */
    else
    {
        scene = importer.ReadFile(qtPath.toStdString(), Flags);
    }

    /* ---- error handling ------------------------------------------------- */
    if (!scene || !scene->HasMeshes())
    {
        std::string reason = importer.GetErrorString();
        if (reason.empty()) reason = "Unknown reason";
        throw std::runtime_error(
            "Failed to load STL \"" + qtPath.toStdString() + "\": " + reason);
    }

    /* ---- copy into your RenderableMeshComponent ------------------------ */
    const aiMesh* m = scene->mMeshes[0];
    meshOut.vertices.resize(m->mNumVertices);

    for (unsigned i = 0; i < m->mNumVertices; ++i) {
        meshOut.vertices[i].position = {
            m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z };
        meshOut.vertices[i].normal = {
            m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z };
    }

    meshOut.indices.reserve(m->mNumFaces * 3);
    for (unsigned f = 0; f < m->mNumFaces; ++f)
        for (unsigned k = 0; k < 3; ++k)
            meshOut.indices.push_back(m->mFaces[f].mIndices[k]);
}

inline void loadStlIntoRenderable(const std::string& utf8Path,
    RenderableMeshComponent& meshOut,
    bool recalcNormals = true)
{
    loadStlIntoRenderable(QString::fromUtf8(utf8Path.c_str()),
        meshOut,
        recalcNormals);
}

inline void loadStlIntoRenderable(const char* utf8Path,
    RenderableMeshComponent& meshOut,
    bool recalcNormals = true)
{
    loadStlIntoRenderable(QString::fromUtf8(utf8Path),
        meshOut,
        recalcNormals);
}

inline QString dataDir()
{
    // Path that holds shaders, meshes, icons … next to the executable
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("data");        // …/RoboticsSoftware.exe + /data
}

namespace MeshUtils {

    inline void calculateTangentsAndBitangents(RenderableMeshComponent& mesh)
    {
        if (mesh.indices.empty() || mesh.vertices.empty()) {
            return; // Cannot process mesh without indices or vertices
        }

        // Go through each triangle
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            // Get the vertices of the triangle
            Vertex& v0 = mesh.vertices[mesh.indices[i]];
            Vertex& v1 = mesh.vertices[mesh.indices[i + 1]];
            Vertex& v2 = mesh.vertices[mesh.indices[i + 2]];

            // Edges of the triangle in position space
            glm::vec3 edge1 = v1.position - v0.position;
            glm::vec3 edge2 = v2.position - v0.position;

            // Edges of the triangle in UV space
            glm::vec2 deltaUV1 = v1.uv - v0.uv;
            glm::vec2 deltaUV2 = v2.uv - v0.uv;

            // Calculate tangent and bitangent
            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

            glm::vec3 tangent, bitangent;

            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
            tangent = glm::normalize(tangent);

            bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
            bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
            bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
            bitangent = glm::normalize(bitangent);

            // Accumulate the tangent and bitangent for each vertex of the triangle.
            // This averages the values for vertices shared between multiple triangles.
            v0.tangent += tangent;
            v1.tangent += tangent;
            v2.tangent += tangent;

            v0.bitangent += bitangent;
            v1.bitangent += bitangent;
            v2.bitangent += bitangent;
        }

        // Normalize the accumulated tangent and bitangent vectors for each vertex
        for (auto& vertex : mesh.vertices) {
            vertex.tangent = glm::normalize(vertex.tangent);
            vertex.bitangent = glm::normalize(vertex.bitangent);
        }
    }
}