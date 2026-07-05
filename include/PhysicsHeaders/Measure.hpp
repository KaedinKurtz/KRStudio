#pragma once
// ===========================================================================
// krs::measure -- the measurement MATH the Measure tool displays.
//
// Resolves a selected B-Rep feature -- the same (entity, faceId) identity the
// selection backend (krs::sel, SelectionService.hpp) commits -- into the numbers
// an operator reads off a caliper: diameter, length, surface area, and the
// distance between two features. Everything is computed in WORLD space (the
// entity TransformComponent is applied to vertices and analytic params) and
// reported in SI METERS; unit conversion is the UI's job, never this module's.
//
// Approximations (documented, honest):
//   - AREA is the sum of the face's WORLD-space triangle areas (the mesh that
//     tessellated the B-Rep face). Exact for planar facets, a slight UNDER-
//     estimate for curved faces (inscribed tessellation). A face no triangle
//     references reports area 0 -- never a fabricated number.
//   - CYLINDER diameter under a non-uniform transform scale: the cross-section
//     becomes an ellipse; we report 2*radius scaled by the MEAN of the two
//     linear-map scale factors PERPENDICULAR to the axis.
//   - SPHERE diameter under non-uniform scale (ellipsoid): 2*radius scaled by
//     the mean of the three axis scale factors.
//   - CONE reports the BRepFace reference radius as the BASE diameter (the
//     B-Rep carries a single reference radius, not the full apex/half-angle).
//   - LENGTH (cylinder/cone) is the world distance between the trimmed rim
//     centres axisEnd0..axisEnd1; both-zero rims (untrimmed/synthetic face,
//     see components.hpp BRepFace) yield length 0 -- no invented extent.
//
// Gate: runMeasureGate() (env hook KRS_MEASURE_SELFTEST, wired in the main
// loop) -- headless, pure CPU, synthetic entities, real NEG-CTRLs.
// ===========================================================================
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>

#include "SelectionService.hpp"   // krs::sel::Selection -- the measure-mode buffer entries

namespace krs::measure {

struct Measurement {
    bool ok = false;             // the feature (or both features) resolved
    std::string kind;            // "cylinder" | "plane" | "sphere" | "cone" | "distance" | "feature"
    double diameterM = 0.0;      // cylinder/sphere/cone-base diameter, meters (0 = n/a)
    double lengthM   = 0.0;      // cylinder rim-to-rim axis length, or point distance (0 = n/a)
    double areaM2    = 0.0;      // face surface area, m^2 (0 = n/a / no triangles)
    glm::vec3 a{ 0.0f };         // measured anchor points, WORLD (for a future on-screen
    glm::vec3 b{ 0.0f };         //   annotation): rim centres / centroid / centre / endpoints
    // Human summary in SI meters (the UI converts units): kind + ONLY the fields
    // that apply, 4 significant figures. e.g. "cylinder  D=0.024 m  L=0.06 m".
    std::string text() const;
};

// Measure ONE selected feature: resolve the BRepFace via (entity, faceId) and
// compute diameter/length/area in WORLD space (see header comment for the
// per-kind rules and approximations). ok=false (never a crash) when the entity
// is invalid, has no BRepFaceComponent, or faceId is out of range.
Measurement measureFeature(entt::registry& reg, entt::entity e, int faceId);

// Distance between two selected features' anchor points: cylinder/cone -> the
// midpoint of the two rim centres (axisPos when the rims are unset), plane ->
// the area-weighted WORLD centroid of the face, sphere -> the centre.
// kind="distance", lengthM = |b-a|. ok=false if either feature fails to resolve.
Measurement measureDistance(entt::registry& reg, entt::entity ea, int faceIdA,
                            entt::entity eb, int faceIdB);

// ===========================================================================
// MEASURE MODE (Onshape-style) -- the pair/single readout over the committed
// viewport selections (krs::sel), grouped by Selection.groupId: one GROUP is
// one measurand (a shift-extended face set is ONE plane item; areas sum).
//
// Semantics (all SI -- meters / m^2 / degrees; the HUD converts units):
//   line·line   : angle via acos(|d1·d2|) (0..90, reads a truthful ~0 when
//                 parallel). Parallel -> "Parallel distance" (perpendicular
//                 axis-to-axis). Otherwise -> min distance between the two
//                 SEGMENTS (closest points; rim-to-rim extents). An untrimmed
//                 bore falls back to infinite-line math (noted).
//   plane·plane : angle between normals; parallel -> gap along the normal +
//                 min face-to-face distance; else angle + min distance.
//                 A multi-face group is treated as ONE plane (areas summed;
//                 non-coplanar groups measure but say so).
//   plane·line  : angle (line to plane) + min distance (cylinder surface
//                 sampled against the face triangles -- noted as sampled).
//   sphere·X    : centre distance + surface min distance.
//   vertex·X    : point distance (vertex = a measure-mode point pick).
// Single item  : line -> diameter + length; plane group -> total area;
//                 sphere -> diameter; vertex -> its coordinates.
// ===========================================================================
struct ReadoutLine {
    enum class Unit { Length, Area, Angle, Count };
    std::string label;
    double value = 0.0;
    Unit unit = Unit::Length;
};
struct Readout {
    bool ok = false;                      // something measurable resolved
    std::vector<ReadoutLine> lines;       // pair lines first, then per-item lines (A ·/B · prefixed)
    std::vector<std::string> notes;       // honest caveats (sampled min-distance, non-coplanar sum, ...)
};
Readout measureSelections(entt::registry& reg, const std::vector<krs::sel::Selection>& sel);

// HUD unit preferences (registry ctx). MainWindow mirrors the ribbon's units combo here, so the
// HUD converts SI meters without any widget coupling. display = meters * lengthFactor.
struct MeasureUiPrefs {
    double lengthFactor = 1.0;
    std::string lengthUnit = "m";
};

// Headless self-test (env KRS_MEASURE_SELFTEST): world-space-exact quad area,
// cylinder D/L under identity + uniform scale, tessellated-disk 1% honesty,
// exact sphere distance, text() applicable-fields-only, NEG-CTRLs
// (out-of-range faceId, missing BRepFaceComponent, zero-triangle face), and
// the measure-mode pair math (skew/parallel lines, parallel plane gap,
// multi-face area sum, vertex distance).
bool runMeasureGate();

} // namespace krs::measure
