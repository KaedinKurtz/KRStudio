#pragma once
#include "Types.hpp"
#include "components.hpp"
#include "Texture2D.hpp"
#include "MeshMaterialSource.hpp"
#include <string>
#include <unordered_map>
#include <memory> // Required for std::unique_ptr
#include <QObject>

class ResourceManager : public QObject {
    Q_OBJECT
private:
    ResourceManager();

public:
    static ResourceManager& instance();
    ResourceManager(const ResourceManager&) = delete;
    void operator=(const ResourceManager&) = delete;

    MeshID loadMesh(const QString& path);
    const RenderableMeshComponent* getMesh(MeshID id) const;

    /// Mesh-native (baked) texture references for a loaded mesh. Extracted
    /// lazily from the source file on first call — DB-cached meshes skip
    /// Assimp entirely on load, so this is the only reliable point — and
    /// memoised (including negative results).
    const MeshMaterialSource* getMeshMaterial(MeshID id);

private:
    // --- HOT CACHE (In-Memory Storage) ---
    std::unordered_map<std::string, MeshID> m_meshPathToId;

    // THE FIX: Store unique_ptr to the mesh data. This guarantees a stable memory address.
    std::unordered_map<MeshID, std::unique_ptr<RenderableMeshComponent>> m_meshes;

    std::unordered_map<MeshID, MeshMaterialSource> m_meshMaterials;

    MeshID m_nextMeshID = static_cast<MeshID>(1);
};
