#pragma once

#include <string>
#include <vector>

class Scene;
namespace krs::rbuild { struct ParsedPart; }   // assembly import returns these (RobotBuilder.hpp)

/**
 * @brief OpenCASCADE STEP ingestion (Phase 4). Loads a .step assembly, spawns
 * one ECS entity per solid (triangulated RenderableMeshComponent), and attaches
 * auto-detected cylindrical mounting frames (AttachmentComponent) plus the
 * B-Rep volume on the MaterialComponent. Real implementation is compiled only
 * when the engine is built with OpenCASCADE (KR_WITH_OCCT); otherwise the
 * functions report unavailability so the UI degrades gracefully.
 */
namespace krs::cad {

struct ImportResult {
    bool ok = false;
    int solids = 0;          // entities spawned
    int faces = 0;           // total B-Rep faces visited
    int attachments = 0;     // cylindrical features -> AttachmentFrame
    double totalVolume = 0.0;// m^3 (GProp_GProps, summed)
    std::string message;
};

/// True if built with OpenCASCADE.
bool available();

/// Import a STEP file into the scene. `metersPerUnit` scales STEP units (often
/// millimetres) to engine metres (default 0.001 = mm).
ImportResult importStep(Scene& scene, const std::string& path, float metersPerUnit = 0.001f);

/// Assembly-aware STEP import: walks the STEPCAF part tree and spawns one entity per NAMED
/// leaf part, baking each part's accumulated assembly placement into world-metre mesh coords
/// (reusing importStep's exact mesh pipeline). Returns the parts (name + world placement +
/// part-local analytic faces + spawned entity, all in metres) for building a named kinematic
/// chain (krs::rbuild::buildNamedSerialChain). Empty in the no-OCCT build.
std::vector<krs::rbuild::ParsedPart> importStepAssembly(Scene& scene, const std::string& path,
                                                        float metersPerUnit = 0.001f);

/// Phase A topology recon (gated by KRS_STEP_INSPECT=<path>): loads a STEP
/// assembly and prints, per solid, its bounding box / centroid / exact volume /
/// face-type tally and every cylindrical face axis (in assembly coordinates),
/// then clusters those axes across solids to reveal SHARED hinge lines — i.e.
/// the revolute joints and any closed kinematic loops (the FANUC parallelogram).
/// Pure stdout, no scene mutation, no GL. No-op in the no-OCCT build.
void inspectStep(const std::string& path);

/// Headless verification of the OCCT pipeline (gated by KRS_CAD_SELFTEST): builds
/// a box-minus-cylinder solid, round-trips it through a temp STEP file, then
/// re-reads + meshes + recognizes the cylindrical feature + computes the GProp
/// volume, asserting solid count / triangle count / cylinder detection / volume.
/// Returns true on PASS (always true in the no-OCCT stub). Requires no GL context.
bool runSelfTest();

/// Phase A GATE U (gated by KRS_UV_SELFTEST): validates the world-scale B-Rep UV
/// generation. U1 world-scale on a controlled box + cylinder (UV area/circumference
/// match the metre geometry to <1%); U4 coverage on the real FANUC (every vertex a
/// finite UV). Returns true on PASS (vacuous pass in the no-OCCT stub). No GL needed.
bool runUvGateU();

/// Phase 3 GATE F (gated by KRS_BREPSEL_SELFTEST): B-Rep feature selector. A ray-picked triangle
/// resolves to its exact OCCT face's ANALYTIC parameters (cylinder axis/radius straight from the
/// B-Rep, not a mesh fit) to <1e-9 vs OCCT. Returns true on PASS (vacuous in the no-OCCT stub).
bool runBRepSelectorGateF();

/// OMPL sprint Phase 3 GATE SUBFEAT (gated by KRS_SUBFEAT_SELFTEST): the sub-feature SELECTION
/// BACKEND (krs::sel) -- a ray resolves to a specific B-Rep face + exact analytic params (<1e-9 vs
/// OCCT) + a selection-indicator GEOMETRY computed as data (rendering deferred); plus small-bore
/// disambiguation (a 5mm bore on a 100mm part resolved as the small cylinder). Vacuous no-OCCT stub.
bool runSubFeatSelectionGate();

/// Phase 3 GATE J (gated by KRS_JOINT_SELFTEST): joint/mate tooling. Derives a revolute joint frame
/// from two selected cylindrical bore features (krs::joint::deriveRevoluteFromBores), checks the
/// frame matches the analytic oracle axis/origin to <1e-6, writes it into the canonical
/// krs::dyn::RobotArticSpec, and verifies two NON-coaxial bores are rejected (no valid revolute).
/// Returns true on PASS (vacuous in the no-OCCT stub).
bool runJointGateJ();

/// Phase 3 GATE F3 (gated by KRS_DISAMBIG_SELFTEST): hard-feature disambiguation -- a small bore (r=3mm)
/// on a large part (200mm), shared-edge adjacency, and edge/corner robustness for the ray-pick selector.
bool runBRepDisambiguationGateF3();

/// Phase 3 GATE J4 (gated by KRS_JOINTFUZZ_SELFTEST): validation fuzz -- 20k random feature x type x
/// extreme-value combinations through the joint derivation + canonical write -> 0 corrupt graphs, 0 bogus
/// accepts.
bool runJointFuzzGateJ4();

} // namespace krs::cad
