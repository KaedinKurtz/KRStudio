// MaterialLoader.cpp
#include "MaterialLoader.hpp"
#include "components.hpp"
#include "Texture2D.hpp"

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// Helper: lowercase a string
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Map key ? setter on MaterialComponent
struct MapDesc {
    const char* key;  // e.g. "albedo", "normal", ...
    std::function<void(MaterialComponent&, std::shared_ptr<Texture2D>)> setter;
};

static const std::vector<MapDesc> kMaps = {
    { "albedo",   [](auto& m, auto t) { m.albedoMap = t; } },
    { "opacity",  [](auto& m, auto t) { m.opacityMap = t; } },
    { "normal",   [](auto& m, auto t) { m.normalMap = t; } },
    { "roughness",[](auto& m, auto t) { m.roughnessMap = t; } },
    { "metallic", [](auto& m, auto t) { m.metallicMap = t; } },
    { "metalness",[](auto& m, auto t) { m.metallicMap = t; } }, // some packs use “metalness”
    { "ao",       [](auto& m, auto t) { m.aoMap = t; } },
    { "height",   [](auto& m, auto t) { m.heightMap = t; } },
    { "emissive", [](auto& m, auto t) { m.emissiveMap = t; } },
    // you can add “emissive”, “sheen”, etc. as needed
};

MaterialComponent loadMaterialFromDirectory(const std::string& dirPath)
{
    MaterialComponent mat;  // default values if nothing loads

    if (!fs::is_directory(dirPath)) {
        qWarning() << "[MaterialLoader] Not a directory:" << QString::fromStdString(dirPath);
        return mat;
    }

    // scan every file in the directory
    for (auto const& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;

        auto filename = entry.path().filename().string();
        auto lower = toLower(filename);

        // skip thumbnails/previews
        if (lower.find("preview") != std::string::npos)
            continue;

        // split name without extension
        auto stem = toLower(entry.path().stem().string());
        // e.g. "dragon-scales_albedo" or "dragon-scales_normal-ogl"

        for (auto const& map : kMaps) {
            // look for a “_key” or “-key” or ending in key
            // we require something like stem ends_with "_albedo" or contains "_albedo-"
            std::string tag1 = std::string("_") + map.key;
            std::string tag2 = std::string("-") + map.key;
            if (stem.size() >= strlen(map.key) + 1 &&
                (stem.rfind(tag1) != std::string::npos ||
                    stem.rfind(tag2) != std::string::npos ||
                    stem == map.key))
            {
                // load a Texture2D (gamma for albedo only)
                auto tex = std::make_shared<Texture2D>();
                bool gamma = (std::string(map.key) == "albedo" || std::string(map.key) == "emissive");
                if (tex->loadFromFile(entry.path().string(), gamma)) {
                    map.setter(mat, tex);
                }
                else {
                    qWarning() << "[MaterialLoader] Failed to load"
                        << QString::fromStdString(entry.path().string());
                }
                break; // done with this file
            }
        }
    }

    return mat;
}
