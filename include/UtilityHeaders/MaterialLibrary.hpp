#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

/**
 * @brief Physical material ground-truth lookup (Phase 4). Resolves a material
 * name or Materials Project id to SI density / bulk / shear modulus.
 *
 * Source priority:
 *  1. A live Materials Project query via `mp_api` (Python subprocess) — used
 *     only when MP_API_KEY is set and mp-api is importable.
 *  2. A built-in canonical-material table (offline ground truth) so the tool
 *     works with no network / key on this machine.
 */
namespace krs::materials {

struct MatProps {
    std::string name;
    double density = 0.0;             // kg/m^3
    double bulkModulus = 0.0;         // Pa
    double shearModulus = 0.0;        // Pa
    double specificHeat = 0.0;        // c_p, J/(kg.K)  (thermal — Phase 4.5)
    double thermalConductivity = 0.0; // k,  W/(m.K)    (thermal — Phase 4.5)
    bool valid = false;
    std::string source;         // "Materials Project (...)" or "offline DB"
};

/// Resolve by canonical name ("steel", "aluminium", "titanium", ...) or mp-id
/// ("mp-13"). Tries the live MP query first, then the offline table.
MatProps query(const std::string& idOrName);

/// Names available in the offline table (for UI hints).
std::vector<std::string> offlineNames();

/// Isotropic elastic constants from bulk K and shear G:
///   E = 9KG/(3K+G),  nu = (3K-2G)/(2(3K+G)).
void deriveElastic(double K, double G, double& youngsE, double& poisson);

/// Signed-tetrahedra volume of a closed triangle mesh (m^3). Robust for the
/// watertight solids OCCT emits; matches GProp_GProps for closed B-Reps.
double meshVolume(const std::vector<glm::vec3>& positions,
                  const std::vector<unsigned int>& indices);

} // namespace krs::materials
