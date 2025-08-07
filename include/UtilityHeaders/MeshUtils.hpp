#pragma once
#include "components.hpp" // For RenderableMeshComponent
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <QString>
#include <QFile>
#include <QDebug>
#include <stdexcept>

// This namespace will hold our pure data-loading and processing functions.
namespace MeshUtils {

    // The primary function to load any supported model file using Assimp.
    // It returns a complete RenderableMeshComponent struct.
    inline RenderableMeshComponent loadMeshFromFile(const QString& path)
    {
        RenderableMeshComponent mesh;
        mesh.sourcePath = path.toStdString();
        static Assimp::Importer importer;

        // Use Assimp's most powerful flags to process the model.
        constexpr unsigned int flags =
            aiProcess_Triangulate            // Ensure all faces are triangles.
            | aiProcess_JoinIdenticalVertices  // Optimize vertex count.
            | aiProcess_GenSmoothNormals       // Generate smooth normals if missing.
            | aiProcess_CalcTangentSpace       // Generate tangents/bitangents if there are UVs.
            | aiProcess_SortByPType          // Group triangles together.
            | aiProcess_ImproveCacheLocality   // Reorder vertices for better GPU cache performance.
            | aiProcess_GenBoundingBoxes;      // Let Assimp calculate the AABB for us.

        const aiScene* scene = nullptr;
        QByteArray fileData; // Keep in scope for ReadFileFromMemory

        if (path.startsWith(":/")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("Failed to open Qt resource: " + path.toStdString());
            }
            fileData = file.readAll();
            scene = importer.ReadFileFromMemory(fileData.constData(), fileData.size(), flags);
        }
        else {
            scene = importer.ReadFile(path.toStdString(), flags);
        }

        if (!scene || !scene->HasMeshes() || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
            throw std::runtime_error("Assimp Error loading " + path.toStdString() + ": " + importer.GetErrorString());
        }

        // We'll just load the first mesh in the file.
        const aiMesh* assimpMesh = scene->mMeshes[0];

        // --- Copy Metadata ---
        mesh.hasUVs = assimpMesh->HasTextureCoords(0);
        mesh.hasTangents = assimpMesh->HasTangentsAndBitangents();

        // --- Copy Bounding Box ---
        const auto& min = assimpMesh->mAABB.mMin;
        const auto& max = assimpMesh->mAABB.mMax;
        mesh.aabbMin = { min.x, min.y, min.z };
        mesh.aabbMax = { max.x, max.y, max.z };

        // --- Copy Vertex Data ---
        mesh.vertices.resize(assimpMesh->mNumVertices);
        for (unsigned int i = 0; i < assimpMesh->mNumVertices; ++i) {
            auto& vertex = mesh.vertices[i];
            vertex.position = { assimpMesh->mVertices[i].x, assimpMesh->mVertices[i].y, assimpMesh->mVertices[i].z };
            vertex.normal = { assimpMesh->mNormals[i].x, assimpMesh->mNormals[i].y, assimpMesh->mNormals[i].z };

            if (mesh.hasUVs) {
                vertex.uv = { assimpMesh->mTextureCoords[0][i].x, assimpMesh->mTextureCoords[0][i].y };
            }
            if (mesh.hasTangents) {
                vertex.tangent = { assimpMesh->mTangents[i].x, assimpMesh->mTangents[i].y, assimpMesh->mTangents[i].z };
                vertex.bitangent = { assimpMesh->mBitangents[i].x, assimpMesh->mBitangents[i].y, assimpMesh->mBitangents[i].z };
            }
        }

        // --- Copy Index Data ---
        mesh.indices.reserve(static_cast<size_t>(assimpMesh->mNumFaces) * 3);
        for (unsigned int i = 0; i < assimpMesh->mNumFaces; i++) {
            const aiFace& face = assimpMesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                mesh.indices.push_back(face.mIndices[j]);
            }
        }

        qDebug() << "Successfully loaded mesh from" << path
            << " | Vertices:" << mesh.vertices.size()
            << " | Indices:" << mesh.indices.size()
            << " | Has UVs:" << mesh.hasUVs;

        return mesh;
    }

    // You can keep these convenience overloads if you like.
    // They just need to return the component instead of taking it as an out-param.
    inline RenderableMeshComponent loadMeshFromFile(const std::string& utf8Path) {
        return loadMeshFromFile(QString::fromUtf8(utf8Path.c_str()));
    }

    inline RenderableMeshComponent loadMeshFromFile(const char* utf8Path) {
        return loadMeshFromFile(QString::fromUtf8(utf8Path));
    }

} // namespace MeshUtils