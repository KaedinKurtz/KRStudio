#include "ResourceManager.hpp"
#include "DatabaseManager.hpp"
#include "MeshUtils.hpp"

ResourceManager& ResourceManager::instance() {
    static ResourceManager instance;
    return instance;
}

ResourceManager::ResourceManager() : QObject(nullptr) {}

MeshID ResourceManager::loadMesh(const QString& path) {
    std::string pathStr = path.toStdString();

    if (m_meshPathToId.count(pathStr)) {
        return m_meshPathToId.at(pathStr);
    }

    // This is a simplified version of your full DB logic for clarity
    RenderableMeshComponent newMesh;
    try {
        qDebug() << "[ResourceManager] Loading mesh from file:" << path;
        newMesh = MeshUtils::loadMeshFromFile(path);
    }
    catch (const std::exception& e) {
        qWarning() << "[ResourceManager] Failed to load mesh from file:" << e.what();
        return MeshID::None;
    }

    MeshID newId = m_nextMeshID;
    m_nextMeshID = static_cast<MeshID>(static_cast<uint32_t>(m_nextMeshID) + 1);

    auto meshPtr = std::make_unique<RenderableMeshComponent>(std::move(newMesh));

    // --- NEW MEMORY LOGGING ---
    qDebug() << "  [ResourceManager] Mesh data created for" << path
        << "at memory address:" << meshPtr.get();
    qDebug() << "    - Vertex Count:" << meshPtr->vertices.size()
        << "| Index Count:" << meshPtr->indices.size();
    // --------------------------

    m_meshes[newId] = std::move(meshPtr);
    m_meshPathToId[pathStr] = newId;
    return newId;
}

const RenderableMeshComponent* ResourceManager::getMesh(MeshID id) const {
    auto it = m_meshes.find(id);
    if (it != m_meshes.end()) {
        const RenderableMeshComponent* meshData = it->second.get();
        // --- NEW MEMORY LOGGING ---
        qDebug() << "  [ResourceManager] getMesh called for ID" << static_cast<uint32_t>(id)
            << ". Returning pointer to address:" << meshData;
        // --------------------------
        return meshData;
    }
    return nullptr;
}
