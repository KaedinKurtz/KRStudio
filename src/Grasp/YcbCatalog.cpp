#include "YcbCatalog.hpp"
#include "AssetPaths.hpp"
#include "GateOutcome.hpp"
#include <cstdio>
#include <filesystem>

namespace krs::grasp {

static std::string ycbRoot() {
    return krs::assets::assetDir("ycb", "KRS_YCB_DIR");
}

std::string YcbObject::meshPath() const {
    return ycbRoot() + "/" + id + "/google_16k/nontextured.ply";
}

std::string YcbObject::coacdPath() const {
    return ycbRoot() + "/" + id + "/coacd.bin";   // offline-generated CoACD convex parts (scripts/gen_coacd.py)
}

const std::vector<YcbObject>& ycbCatalog() {
    // anchorLongest{Min,Max}: a tight scale band on the AABB longest axis for EVERY object. The center is the
    // as-shipped measured longest (the meshes are verified real-meter via independent published anchors --
    // tennis ball 0.067 = ITF diameter, soup can 0.102 = published height, bowl 0.162 = published rim), so each
    // object's measured longest IS its real dimension. The band is measured x [0.85, 1.18] (~ +-16%): the exact
    // shipped mesh passes, a x2 or x0.5 re-scaled file FAILS. The 3 with independent published dims keep their
    // (slightly tighter) published bands. This closes the "17/20 unanchored, 2x passes" hole.
    static const std::vector<YcbObject> kCat = {
        { "003_cracker_box",     "box",      false, 0.181, 0.252 },  // measured 0.2134
        { "004_sugar_box",       "box",      false, 0.150, 0.208 },  // measured 0.1760
        { "005_tomato_soup_can", "can",      false, 0.095, 0.108 },  // published ~0.102 m tall
        { "006_mustard_bottle",  "bottle",   false, 0.163, 0.226 },  // measured 0.1913
        { "007_tuna_fish_can",   "can",      false, 0.073, 0.101 },  // measured 0.0856
        { "009_gelatin_box",     "box",      false, 0.086, 0.119 },  // measured 0.1011
        { "010_potted_meat_can", "can",      false, 0.087, 0.121 },  // measured 0.1021
        { "011_banana",          "thin",     false, 0.152, 0.211 },  // measured 0.1784
        { "019_pitcher_base",    "concave",  true,  0.206, 0.286 },  // measured 0.2424
        { "021_bleach_cleanser", "bottle",   false, 0.213, 0.296 },  // measured 0.2506
        { "024_bowl",            "concave",  true,  0.145, 0.175 },  // published ~0.159 m rim diameter
        { "025_mug",             "concave",  true,  0.099, 0.138 },  // measured 0.1170 (handle-skewed)
        { "035_power_drill",     "tool",     false, 0.159, 0.221 },  // measured 0.1875
        { "036_wood_block",      "box",      false, 0.175, 0.243 },  // measured 0.2060
        { "040_large_marker",    "thin",     false, 0.103, 0.143 },  // measured 0.1210
        { "048_hammer",          "tool",     false, 0.283, 0.393 },  // measured 0.3327
        { "056_tennis_ball",     "sphere",   false, 0.060, 0.075 },  // published ~0.067 m diameter (ITF)
        { "061_foam_brick",      "box",      false, 0.066, 0.092 },  // measured 0.0779
        { "065-b_cups",          "concave",  true,  0.054, 0.076 },  // measured 0.0640
        { "077_rubiks_cube",     "box",      false, 0.065, 0.090 },  // measured 0.0761 (tilt-inflated)
    };
    return kCat;
}

bool ycbAssetsAvailable() {
    const auto& cat = ycbCatalog();
    if (cat.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(cat.front().meshPath(), ec);
}

bool ycbSkipIfAbsent(const char* gateName) {
    if (ycbAssetsAvailable()) return false;
    std::printf("\n[%s] SKIP: YCB asset pack absent under '%s' "
                "(optional ~117 MB; run scripts/download_ycb.sh or set KRS_YCB_DIR)\n",
                gateName, ycbRoot().c_str());
    std::fflush(stdout);
    krs::gate::skip();
    return true;
}

bool ycbCoacdAvailable() {
    const auto& cat = ycbCatalog();
    if (cat.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(cat.front().coacdPath(), ec);
}

bool coacdSkipIfAbsent(const char* gateName) {
    if (ycbCoacdAvailable()) return false;
    std::printf("\n[%s] SKIP: CoACD colliders absent under '%s' "
                "(offline-cooked; run scripts/gen_coacd.py -- needs `pip install coacd`)\n",
                gateName, ycbRoot().c_str());
    std::fflush(stdout);
    krs::gate::skip();
    return true;
}

} // namespace krs::grasp
