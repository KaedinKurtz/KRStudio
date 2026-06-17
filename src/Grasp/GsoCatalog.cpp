#include "GsoCatalog.hpp"
#include <cstdlib>
#include <algorithm>
#include <filesystem>

namespace krs::grasp {

namespace fs = std::filesystem;

static std::string gsoRoot() {
    if (const char* env = std::getenv("KRS_GSO_DIR")) return std::string(env);
    return "C:/Users/kurtz/KRStudio/KRStudio/assets/gso";
}

std::string GsoObject::meshPath() const  { return gsoRoot() + "/" + name + "/model.obj"; }
std::string GsoObject::coacdPath() const { return gsoRoot() + "/" + name + "/coacd.bin"; }

const std::vector<GsoObject>& gsoCatalog() {
    static std::vector<GsoObject> cat;
    static bool built = false;
    if (built) return cat;
    built = true;
    std::error_code ec;
    const fs::path root(gsoRoot());
    if (fs::is_directory(root, ec)) {
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_directory()) continue;
            const fs::path obj = e.path() / "model.obj";
            if (fs::exists(obj, ec) && fs::file_size(obj, ec) > 0)
                cat.push_back({e.path().filename().string()});
        }
    }
    std::sort(cat.begin(), cat.end(), [](const GsoObject& a, const GsoObject& b) { return a.name < b.name; });
    return cat;
}

} // namespace krs::grasp
