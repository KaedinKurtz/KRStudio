#include "YcbCatalog.hpp"
#include <cstdlib>

namespace krs::grasp {

static std::string ycbRoot() {
    if (const char* env = std::getenv("KRS_YCB_DIR")) return std::string(env);
    return "C:/Users/kurtz/KRStudio/KRStudio/assets/ycb";
}

std::string YcbObject::meshPath() const {
    return ycbRoot() + "/" + id + "/google_16k/nontextured.ply";
}

const std::vector<YcbObject>& ycbCatalog() {
    // anchorLongest{Min,Max}: tight bands for objects whose published real size + orientation-robust AABB I can
    // pin confidently (sphere = diameter; upright can = height; bowl = rim diameter). Verified by measuring the
    // downloaded meshes against published YCB dimensions (Calli et al. 2015).
    static const std::vector<YcbObject> kCat = {
        { "003_cracker_box",     "box",      false, 0.0,   0.0   },
        { "004_sugar_box",       "box",      false, 0.0,   0.0   },
        { "005_tomato_soup_can", "can",      false, 0.095, 0.108 },  // ~0.102 m tall
        { "006_mustard_bottle",  "bottle",   false, 0.0,   0.0   },
        { "007_tuna_fish_can",   "can",      false, 0.0,   0.0   },
        { "009_gelatin_box",     "box",      false, 0.0,   0.0   },
        { "010_potted_meat_can", "can",      false, 0.0,   0.0   },
        { "011_banana",          "thin",     false, 0.0,   0.0   },
        { "019_pitcher_base",    "concave",  true,  0.0,   0.0   },  // large handle/cavity
        { "021_bleach_cleanser", "bottle",   false, 0.0,   0.0   },
        { "024_bowl",            "concave",  true,  0.145, 0.175 },  // ~0.159 m rim diameter
        { "025_mug",             "concave",  true,  0.0,   0.0   },  // handle skews AABB -> universal band only
        { "035_power_drill",     "tool",     false, 0.0,   0.0   },
        { "036_wood_block",      "box",      false, 0.0,   0.0   },
        { "040_large_marker",    "thin",     false, 0.0,   0.0   },
        { "048_hammer",          "tool",     false, 0.0,   0.0   },
        { "056_tennis_ball",     "sphere",   false, 0.060, 0.075 },  // ~0.067 m diameter (ITF 0.0654-0.0686)
        { "061_foam_brick",      "box",      false, 0.0,   0.0   },
        { "065-b_cups",          "concave",  true,  0.0,   0.0   },  // stacking cup cavity
        { "077_rubiks_cube",     "box",      false, 0.0,   0.0   },  // ~0.057 m side (AABB tilt-inflated)
    };
    return kCat;
}

} // namespace krs::grasp
