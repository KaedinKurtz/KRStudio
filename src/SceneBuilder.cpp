#include "SceneBuilder.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "Mesh.hpp"
#include "MeshUtils.hpp"
#include "Primitivebuilders.hpp"
#include <QDebug>

entt::entity SceneBuilder::createCamera(entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& colour)
{
    // 1 ── CAMERA ENTITY ----------------------------------------------------
    entt::entity camE = registry.create();

    registry.emplace<CameraComponent>(camE);
    auto& camXf = registry.emplace<TransformComponent>(camE);
    camXf.translation = position;

    // 2 ── MINI CAMERA MESH (gizmo) ----------------------------------------
    entt::entity gizE = registry.create();
    registry.emplace<ParentComponent>(gizE, camE);
    registry.emplace<CameraGizmoTag>(gizE);

    auto& gxf = registry.emplace<TransformComponent>(gizE);
    gxf.translation.z = -0.35f;
    gxf.scale = glm::vec3(0.12f);
    gxf.rotation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)) *
        glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));

    auto& gmesh = registry.emplace<RenderableMeshComponent>(gizE);
    loadStlIntoRenderable(
        "D:/RoboticsSoftware/external/miniViewportCamera.stl", gmesh);

    // FIX: Replaced C++20 designated initializer with C++17-compatible code.
// First, emplace the component with its default values.
    auto& gizmoMaterial = registry.emplace<MaterialComponent>(gizE);
    // Then, modify the specific member we care about.
    gizmoMaterial.albedo = colour;


    // 3 ── BLINKING “REC” LED ---------------------------------------------
    entt::entity ledE = registry.create();
    registry.emplace<ParentComponent>(ledE, gizE);
    registry.emplace<RecordLedTag>(ledE);

    auto& lxf = registry.emplace<TransformComponent>(ledE);
    lxf.translation = { 0.1f, -0.115f, 0.275f };
    lxf.scale = glm::vec3(0.1f);

    auto& lmesh = registry.emplace<RenderableMeshComponent>(ledE);
    buildIcoSphere(lmesh.vertices, lmesh.indices);

    // FIX: Replaced C++20 designated initializer with C++17-compatible code.
    // Emplace the component for the LED, then set its albedo.
    auto& ledMaterial = registry.emplace<MaterialComponent>(ledE);
    ledMaterial.albedo = glm::vec3(1.0f, 0.0f, 0.0f);


    return camE;
}

void SceneBuilder::spawnRobot(Scene& scene, const RobotDescription& description)
{
    // --- DIAGNOSTIC LOGGING ---
    ////qDebug() << "!!!!!!!! SceneBuilder::spawnRobot has been called! Now clearing previous robot... !!!!!!!!!";

    auto& registry = scene.getRegistry();

    // --- SAFER CLEANUP ---
    // Instead of clearing the whole registry, we specifically find and
    // destroy only the entities that are part of the old robot.
    // We identify these by looking for a LinkComponent.
    auto view = registry.view<LinkComponent>();
    registry.destroy(view.begin(), view.end());


    // --- The rest of the function remains the same ---
    std::unordered_map<std::string, entt::entity> linkNameToEntity;

    for (const auto& linkDesc : description.links)
    {
        auto linkEntity = registry.create();
        linkNameToEntity[linkDesc.name] = linkEntity;
        registry.emplace<TagComponent>(linkEntity, linkDesc.name);
        registry.emplace<LinkComponent>(linkEntity, linkDesc);
        registry.emplace<TransformComponent>(linkEntity);

        if (!linkDesc.mesh_filepath.empty())
        {
            auto& meshComp = registry.emplace<RenderableMeshComponent>(linkEntity);

            const std::vector<float>& raw = Mesh::getLitCubeVertices();
            constexpr std::size_t stride = 6;

            meshComp.vertices.clear();
            meshComp.vertices.reserve(raw.size() / stride);

            for (std::size_t i = 0; i < raw.size(); i += stride)
            {
                glm::vec3 pos{ raw[i],   raw[i + 1], raw[i + 2] };
                glm::vec3 normal{ raw[i + 3], raw[i + 4], raw[i + 5] };
                meshComp.vertices.emplace_back(pos, normal);
            }

            meshComp.indices = Mesh::getLitCubeIndices();
        }
    }

    for (const auto& jointDesc : description.joints)
    {
        entt::entity parentEntity = entt::null;
        entt::entity childEntity = entt::null;

        if (linkNameToEntity.count(jointDesc.parent_link_name)) {
            parentEntity = linkNameToEntity.at(jointDesc.parent_link_name);
        }
        if (linkNameToEntity.count(jointDesc.child_link_name)) {
            childEntity = linkNameToEntity.at(jointDesc.child_link_name);
        }

        if (registry.valid(parentEntity) && registry.valid(childEntity))
        {
            registry.emplace<ParentComponent>(childEntity, parentEntity);
            auto& childTransform = registry.get<TransformComponent>(childEntity);
            childTransform.translation = jointDesc.origin_xyz;
        }
    }
}

entt::entity SceneBuilder::makeCR(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,      // Changed parameter name
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::CatmullRom;
    // Use the new consolidated 'controlPoints' vector
    sp.controlPoints = cps;
    sp.coreColour = coreColour;       // Set the new member
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeParam(entt::registry& r,
    std::function<glm::vec3(float)> f,
    const glm::vec4& coreColour,      // Changed parameter name
    const glm::vec4& glowColour, 
    float glowThickness)
{
    entt::entity e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::Parametric;
    sp.parametric.func = std::move(f);
    sp.coreColour = coreColour;       // Set the new member
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeLinear(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,      // Changed parameter name
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::Linear;
    sp.controlPoints = cps; // Use the new consolidated vector
    sp.coreColour = coreColour;       // Set the new member
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeBezier(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,      // Changed parameter name
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::Bezier;
    sp.controlPoints = cps; // Use the new consolidated vector
    sp.coreColour = coreColour;       // Set the new member
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}