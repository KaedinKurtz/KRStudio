#pragma once
// AssetPaths.hpp -- machine-agnostic asset resolution (pure std::filesystem, no Qt).
//
// Replaces the old hardcoded "C:/Users/<dev>/KRStudio/..." paths that were baked into the
// grasp catalogs, the FANUC importer and the node profile gate. Mirrors the candidate-search
// pattern RenderingSystem::shadersRootDir() uses for shaders, but with zero Qt dependency so
// it is usable from the pure-std src/Grasp translation units.
//
// Resolution order for assetDir("ycb", "KRS_YCB_DIR"):
//   1. $KRS_YCB_DIR if set & non-empty                       -> used verbatim
//   2. first existing of  {cwd, ., .., ../.., <KRS_SOURCE_DIR>}/assets/ycb
//   3. fallback: <KRS_SOURCE_DIR>/assets/ycb if compiled in, else "assets/ycb"
// The deployed exe runs with assets/ copied beside it (CMake POST_BUILD), and the bench's
// working directory is its deploy dir, so the cwd-relative "assets/<sub>" candidate matches
// the shipped layout. KRS_SOURCE_DIR (injected by CMake) covers running from the build tree.
#include <string>

namespace krs::assets {

// Resolve an asset subdirectory (e.g. "ycb", "gso") to an absolute/usable path. envVar, when
// given, is an environment variable that overrides the search entirely if set & non-empty.
std::string assetDir(const std::string& sub, const char* envVar = nullptr);

// Resolve a file beneath an asset subdir: assetDir(sub, envVar) + "/" + rel.
std::string assetFile(const std::string& sub, const std::string& rel, const char* envVar = nullptr);

} // namespace krs::assets
