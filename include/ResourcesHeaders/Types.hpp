#pragma once
#include <cstdint>
#include <glm/glm.hpp>

// Use strongly-typed enums for resource IDs to prevent accidental mix-ups.
enum class MeshID : uint32_t { None = 0 };
enum class TextureID : uint32_t { None = 0 };
enum class MaterialID : uint32_t { None = 0 };

// A namespace for useful default values.
namespace Defaults
{
    const glm::vec3 AlbedoColor(0.8f, 0.8f, 0.8f);
    const float Metallic(0.0f);
    const float Roughness(0.5f);

    const TextureID AlbedoMap = TextureID::None;
    const TextureID NormalMap = TextureID::None;
    // ... etc. for other default textures
}