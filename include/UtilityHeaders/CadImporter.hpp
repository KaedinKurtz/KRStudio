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

} // namespace krs::cad
