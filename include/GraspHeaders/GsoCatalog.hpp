#pragma once
// GsoCatalog.hpp -- the LARGE object library for grasp generalization: Google Scanned Objects (GSO), 1033 real
// scanned household items, Creative Commons Attribution 4.0 International (CC-BY 4.0). We download a few-hundred
// subset (scripts/download_gso.py) in REAL METRIC scale (meters). This catalog just enumerates whatever GSO
// model.obj files are present under assets/gso (or $KRS_GSO_DIR) -- it does NOT decide validity; the GATE FILTER
// classifies each object as graspable-valid or rejected (with a reason), and only the survivors get colliders.
#include <string>
#include <vector>

namespace krs::grasp {

struct GsoObject {
    std::string name;              // the GSO model directory name
    std::string meshPath() const;  // assets/gso/<name>/model.obj
    std::string coacdPath() const; // assets/gso/<name>/coacd.bin  (generated for FILTER survivors only)
};

// Enumerate every GSO object that has a model.obj on disk (scans the directory; sorted by name for determinism).
const std::vector<GsoObject>& gsoCatalog();

} // namespace krs::grasp
