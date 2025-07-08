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