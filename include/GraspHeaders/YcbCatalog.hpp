#pragma once
// YcbCatalog.hpp -- the curated YCB object library (Yale-CMU-Berkeley object set; free research use). 20
// objects spanning boxes / cylinders / cans / bottles / concavities (bowl,mug,cup,pitcher) / tools / spheres
// / thin items. anchorLongest{Min,Max} are published-dimension anchors on the AABB's LONGEST axis for objects
// whose real size I can pin confidently (orientation-robust ones); 0 means "use only the universal band".
#include <string>
#include <vector>

namespace krs::grasp {

struct YcbObject {
    std::string id;          // e.g. "025_mug"
    std::string category;    // box / cylinder / can / bottle / concave / tool / sphere / thin
    bool        concavity;   // has a grasp-relevant cavity (bowl/mug/cup/pitcher) -> used by GATE COACD
    double      anchorLongestMin;  // tight published-dimension anchor on the AABB longest axis (m); 0 = none
    double      anchorLongestMax;
    std::string meshPath() const;  // resolved absolute path to the geometry (.ply)
};

// The library. Mesh dir defaults to the repo assets/ycb, overridable via KRS_YCB_DIR.
const std::vector<YcbObject>& ycbCatalog();

// universal manipulation-object scale band (the AABB longest axis must lie inside this, in meters).
constexpr double kScaleBandMin = 0.02;   // 2 cm
constexpr double kScaleBandMax = 0.40;   // 40 cm

} // namespace krs::grasp
