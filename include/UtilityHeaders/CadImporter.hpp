#pragma once

#include <string>

class Scene;

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

} // namespace krs::cad
