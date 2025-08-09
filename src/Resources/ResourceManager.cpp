#include "ResourceManager.hpp"
#include "DatabaseManager.hpp"
#include "MeshUtils.hpp"

namespace {
    inline void computeLocalAABB(RenderableMeshComponent& mesh) {
        if (mesh.vertices.empty()) {
            mesh.aabbMin = glm::vec3(0.0f);
            mesh.aabbMax = glm::vec3(0.0f);
            return;
        }
        glm::vec3 minV(std::numeric_limits<float>::max());
        glm::vec3 maxV(-std::numeric_limits<float>::max());
        for (const auto& v : mesh.vertices) {
            const glm::vec3 p = v.position;
            minV.x = std::min(minV.x, p.x);
            minV.y = std::min(minV.y, p.y);
            minV.z = std::min(minV.z, p.z);
            maxV.x = std::max(maxV.x, p.x);
            maxV.y = std::max(maxV.y, p.y);
            maxV.z = std::max(maxV.z, p.z);
        }
        mesh.aabbMin = minV;
        mesh.aabbMax = maxV;
    }
}

ResourceManager& ResourceManager::instance() {
    static ResourceManager instance;
    return instance;
}

ResourceManager::ResourceManager() : QObject(nullptr) {}

MeshID ResourceManager::loadMesh(const QString& path) {
    std::string pathStr = path.toStdString();

    // 1) Hot cache
    if (auto it = m_meshPathToId.find(pathStr); it != m_meshPathToId.end()) {
        return it->second;
    }

    auto& dbm = db::DatabaseManager::instance();

    // 2) Try DB cache first
    std::optional<RenderableMeshComponent> meshOpt = dbm.loadMeshAsset(path);

    RenderableMeshComponent mesh;
    if (meshOpt) {
        mesh = std::move(*meshOpt);
        qDebug() << "[ResourceManager] Loaded mesh from DB cache for:" << path;
    }
    else {
        // 3) Fallback: load from file, then persist into DB cache
        try {
            qDebug() << "[ResourceManager] Loading mesh from file:" << path;
            mesh = MeshUtils::loadMeshFromFile(path);
        }
        catch (const std::exception& e) {
            qWarning() << "[ResourceManager] Failed to load mesh from file:" << e.what();
            return MeshID::None;
        }
        (void)dbm.saveMeshAsset(path, mesh); // best-effort cache
    }

    computeLocalAABB(mesh);
    // Optional: if DB meshes were missing bounds, write them back so next run is hot:
    // (safe no-op for file-loaded path because we already saved above)
    (void)dbm.saveMeshAsset(path, mesh);

    // 4) Ensure metadata needed elsewhere (e.g., SceneBuilder tag uses sourcePath)
    mesh.sourcePath = pathStr;  // SceneBuilder reads this for tag naming
    // leave hasUVs/hasTangents as loaded; DB-read meshes lack tangents so defaults are fine

    // 5) Stable ID from DB
    MeshID id = dbm.getOrCreateMeshIdForPath(path);
    if (id == MeshID::None) {
        qWarning() << "[ResourceManager] Could not allocate MeshID for path:" << path;
        return MeshID::None;
    }

    // 6) Put into hot cache
    auto meshPtr = std::make_unique<RenderableMeshComponent>(std::move(mesh));
    qDebug() << "  [ResourceManager] Mesh cached at address:" << meshPtr.get()
        << " verts:" << meshPtr->vertices.size()
        << " idx:" << meshPtr->indices.size();

    m_meshes[id] = std::move(meshPtr);
    m_meshPathToId[pathStr] = id;

    // Touch the asset entry for LRU maintenance (you already support this)
    dbm.touchAsset(path);  // optional, cheap :contentReference[oaicite:8]{index=8}

    return id;
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
