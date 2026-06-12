#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief CPU-side description of the textures a model file ships with
 * (baked, mesh-native materials): either paths on disk resolved relative to
 * the model, or byte blobs copied out of embedded textures (.glb/.fbx).
 * Extracted by MeshUtils::extractMaterialSource, cached per MeshID by the
 * ResourceManager, and turned into a live MaterialComponent on the engine
 * GL context by RenderingSystem::processMaterialReloads.
 */
struct MeshMaterialSource {
    struct Map {
        std::string filePath;            // on-disk source (empty if embedded)
        std::vector<uint8_t> bytes;      // compressed image bytes (embedded)
        bool srgb = false;               // sample as sRGB (albedo/emissive)
    };

    std::optional<Map> albedo, normal, roughness, metallic, ao, emissive, height;

    bool any() const
    {
        return albedo || normal || roughness || metallic || ao || emissive || height;
    }
};

/// ECS component: spawn-time request to build this entity's
/// MaterialComponent from its mesh's native textures.
struct PendingMaterialData {
    MeshMaterialSource source;
};
