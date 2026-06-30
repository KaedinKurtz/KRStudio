// AssetPaths.cpp -- see AssetPaths.hpp. Pure std::filesystem; no Qt, no platform calls.
#include "AssetPaths.hpp"
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace krs::assets {

namespace fs = std::filesystem;

std::string assetDir(const std::string& sub, const char* envVar)
{
    // 1. explicit env override wins.
    if (envVar) {
        if (const char* e = std::getenv(envVar)) {
            if (e[0] != '\0') return std::string(e);
        }
    }

    std::error_code ec;
    std::vector<fs::path> bases;
    bases.push_back(fs::current_path(ec));   // deploy dir for the bench exe
    bases.emplace_back(".");
    bases.emplace_back("..");
    bases.emplace_back("../..");
#ifdef KRS_SOURCE_DIR
    bases.emplace_back(KRS_SOURCE_DIR);      // running from the build tree
#endif

    for (const auto& b : bases) {
        fs::path cand = b / "assets" / sub;
        if (fs::exists(cand, ec)) return cand.lexically_normal().string();
    }

    // 2. nothing on disk yet -- return a sane, machine-agnostic path so error messages and
    //    later "does this dir exist?" checks are meaningful (and SKIP, not garbage).
#ifdef KRS_SOURCE_DIR
    return (fs::path(KRS_SOURCE_DIR) / "assets" / sub).lexically_normal().string();
#else
    return (fs::path("assets") / sub).string();
#endif
}

std::string assetFile(const std::string& sub, const std::string& rel, const char* envVar)
{
    return assetDir(sub, envVar) + "/" + rel;
}

} // namespace krs::assets
