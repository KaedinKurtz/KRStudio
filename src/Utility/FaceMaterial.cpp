// FaceMaterial.cpp -- see FaceMaterial.hpp. Whole-body vs per-face material ops; per-face render
// via generated overlay sub-meshes (no shader/vertex-format change).
#include "FaceMaterial.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <cstdio>
#include <vector>

namespace krs::facemat {

void applyToBody(entt::registry& reg, entt::entity e, const FaceMaterial& m) {
    if (!reg.valid(e)) return;
    auto& mat = reg.get_or_emplace<MaterialComponent>(e);
    mat.albedoColor = m.albedo;
    mat.metallic    = m.metallic;
    mat.roughness   = m.roughness;
}

bool applyToFace(entt::registry& reg, entt::entity e, int faceId, const FaceMaterial& m) {
    if (!reg.valid(e) || faceId < 0) return false;
    const auto* brep = reg.try_get<BRepFaceComponent>(e);
    if (!brep || faceId >= int(brep->faces.size())) return false;
    reg.get_or_emplace<FaceMaterialComponent>(e).byFace[faceId] = m;
    return true;
}

bool clearFace(entt::registry& reg, entt::entity e, int faceId) {
    if (!reg.valid(e)) return false;
    auto* fm = reg.try_get<FaceMaterialComponent>(e);
    if (!fm) return false;
    return fm->byFace.erase(faceId) > 0;
}

void destroyOverlays(entt::registry& reg, entt::entity e) {
    auto* fm = reg.try_get<FaceMaterialComponent>(e);
    if (!fm) return;
    for (std::uint32_t id : fm->overlayEntities) {
        const entt::entity oe = entt::entity(id);
        if (reg.valid(oe)) reg.destroy(oe);
    }
    fm->overlayEntities.clear();
}

int rebuildFaceOverlays(Scene& scene, entt::entity e) {
    auto& reg = scene.getRegistry();
    if (!reg.valid(e)) return 0;
    destroyOverlays(reg, e);
    auto* fm = reg.try_get<FaceMaterialComponent>(e);
    if (!fm || fm->byFace.empty()) return 0;
    const auto* mesh = reg.try_get<RenderableMeshComponent>(e);
    if (!mesh || mesh->triFace.empty() || mesh->vertices.empty()) return 0;

    int made = 0;
    for (const auto& [faceId, fmat] : fm->byFace) {
        // Gather this face's triangles from triFace; remap to a compact vertex set, offset each
        // vertex a hair along its normal so the overlay sits just proud of the parent (no z-fight).
        std::vector<Vertex> ov;
        std::vector<unsigned int> oi;
        std::unordered_map<unsigned int, unsigned int> remap;   // parent vertex -> overlay vertex
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (size_t t = 0; t < mesh->triFace.size(); ++t) {
            if (mesh->triFace[t] != faceId) continue;
            const size_t i0 = t * 3;
            if (i0 + 2 >= mesh->indices.size()) continue;
            for (int k = 0; k < 3; ++k) {
                const unsigned int vi = mesh->indices[i0 + k];
                if (vi >= mesh->vertices.size()) continue;
                auto it = remap.find(vi);
                if (it == remap.end()) {
                    Vertex v = mesh->vertices[vi];
                    v.position += v.normal * 5e-4f;              // 0.5 mm proud
                    remap[vi] = unsigned(ov.size());
                    it = remap.find(vi);
                    ov.push_back(v);
                    mn = glm::min(mn, v.position); mx = glm::max(mx, v.position);
                }
                oi.push_back(it->second);
            }
        }
        if (ov.empty() || oi.empty()) continue;                 // face id with no triangles -> skip

        const entt::entity oe = reg.create();
        auto& om = reg.emplace<RenderableMeshComponent>(oe);
        om.vertices = std::move(ov);
        om.indices = std::move(oi);
        om.aabbMin = mn; om.aabbMax = mx;
        om.sourcePath = "face_overlay";
        // Parent to the body (identity local so it sits exactly on the offset triangles) so it
        // tracks the body's motion; propagateTransforms composes world = parent * local.
        reg.emplace<TransformComponent>(oe, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        reg.emplace<ParentComponent>(oe, e);
        auto& mat = reg.emplace<MaterialComponent>(oe);
        mat.albedoColor = fmat.albedo; mat.metallic = fmat.metallic; mat.roughness = fmat.roughness;
        reg.emplace<TriPlanarMaterialTag>(oe);                  // no UVs on the overlay slice
        // Overlays are decoration, never robot members / pickable features: no
        // RobotSubcomponentComponent, no BRepFaceComponent (so they can't be re-painted or driven).
        fm->overlayEntities.push_back(std::uint32_t(oe));
        ++made;
    }
    return made;
}

// ================================================================================================
// GATE FACEMAT
// ================================================================================================
bool runFaceMaterialGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[facemat] GATE FACEMAT -- whole-body write; per-face override + overlay generation; clear drops overlay\n");

    Scene scene;
    auto& reg = scene.getRegistry();
    const entt::entity e = reg.create();
    // Two triangles = two faces (faceId 0 and 1), a quad-ish patch.
    auto& mesh = reg.emplace<RenderableMeshComponent>(e);
    auto V = [](float x, float y) { Vertex v; v.position = { x, y, 0 }; v.normal = { 0, 0, 1 }; return v; };
    mesh.vertices = { V(0,0), V(1,0), V(1,1), V(0,1) };
    mesh.indices  = { 0, 1, 2,   0, 2, 3 };
    mesh.triFace  = { 0, 1 };                                    // tri 0 -> face 0, tri 1 -> face 1
    mesh.aabbMin = { 0, 0, 0 }; mesh.aabbMax = { 1, 1, 0 };
    reg.emplace<TransformComponent>(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    reg.emplace<BRepFaceComponent>(e, BRepFaceComponent{ std::vector<BRepFace>(2) });
    reg.emplace<MaterialComponent>(e);

    // WHOLE BODY
    FaceMaterial red; red.albedo = { 1, 0, 0 }; red.metallic = 0.9f; red.roughness = 0.2f;
    applyToBody(reg, e, red);
    const auto& bm = reg.get<MaterialComponent>(e);
    const bool bodyOk = glm::length(bm.albedoColor - glm::vec3(1, 0, 0)) < 1e-6f
                     && std::abs(bm.metallic - 0.9f) < 1e-6f && std::abs(bm.roughness - 0.2f) < 1e-6f;

    // THIS FACE: paint face 0 green, face 1 blue -> two overlays with matching colors + 3 verts each.
    FaceMaterial green; green.albedo = { 0, 1, 0 };
    FaceMaterial blue;  blue.albedo  = { 0, 0, 1 };
    const bool a0 = applyToFace(reg, e, 0, green);
    const bool a1 = applyToFace(reg, e, 1, blue);
    const int made = rebuildFaceOverlays(scene, e);
    const auto& fm = reg.get<FaceMaterialComponent>(e);
    bool overlaysOk = (made == 2) && a0 && a1 && fm.overlayEntities.size() == 2;
    // each overlay is one triangle (3 verts) with the right albedo
    int greenSeen = 0, blueSeen = 0;
    for (std::uint32_t id : fm.overlayEntities) {
        const entt::entity oe = entt::entity(id);
        if (!reg.valid(oe)) { overlaysOk = false; continue; }
        const auto& om = reg.get<RenderableMeshComponent>(oe);
        const auto& omat = reg.get<MaterialComponent>(oe);
        if (om.vertices.size() != 3 || om.indices.size() != 3) overlaysOk = false;
        if (glm::length(omat.albedoColor - glm::vec3(0, 1, 0)) < 1e-6f) ++greenSeen;
        if (glm::length(omat.albedoColor - glm::vec3(0, 0, 1)) < 1e-6f) ++blueSeen;
        // overlay is parented to the body + is NOT a pickable/robot entity
        if (!reg.all_of<ParentComponent>(oe) || reg.get<ParentComponent>(oe).parent != e) overlaysOk = false;
        if (reg.all_of<BRepFaceComponent>(oe)) overlaysOk = false;
    }
    overlaysOk = overlaysOk && greenSeen == 1 && blueSeen == 1;

    // CLEAR face 0 -> one overlay remains (blue).
    const bool cleared = clearFace(reg, e, 0);
    const int made2 = rebuildFaceOverlays(scene, e);
    const bool clearOk = cleared && made2 == 1 && reg.get<FaceMaterialComponent>(e).overlayEntities.size() == 1;

    // NEG-CTRL: a face id with no triangles produces no overlay (and applyToFace rejects out-of-range).
    const bool negBadFace = !applyToFace(reg, e, 99, red);       // faceId >= face count -> rejected
    FaceMaterial x; applyToFace(reg, e, 1, x);                   // valid
    const bool pass = bodyOk && overlaysOk && clearOk && negBadFace;
    printf("[facemat]   whole-body PBR set=%s ; per-face overlays (2, colors match, parented, non-pickable)=%s ; "
           "clear drops one (->1)=%s ; NEG bad-faceId rejected=%s  %s\n",
           bodyOk?"yes":"NO", overlaysOk?"yes":"NO", clearOk?"yes":"NO", negBadFace?"yes":"NO",
           pass ? "PASS" : "FAIL");
    printf("[facemat] %s\n", pass ? "ALL PASS (whole-body material; per-face overlays generated/cleared; data-model sound)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::facemat
