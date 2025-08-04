#include "SceneBuilder.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "Mesh.hpp"
#include "MeshUtils.hpp"
#include "PrimitiveBuilders.hpp"
#include <QDebug>

entt::entity SceneBuilder::createCamera(entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& colour)
{
    // 1 – CAMERA ENTITY ----------------------------------------------------
    entt::entity camE = registry.create();

    auto& camComp = registry.emplace<CameraComponent>(camE);
    auto& camXf = registry.emplace<TransformComponent>(camE);
    camComp.tint = colour;
    camXf.translation = position;

    // 2 – MINI CAMERA MESH (gizmo) ----------------------------------------
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
    loadStlIntoRenderable(":/external/miniViewportCamera.stl", gmesh);

    auto& gizmoMaterial = registry.emplace<MaterialComponent>(gizE);
    gizmoMaterial.albedoColor = colour;

    // 3 – BLINKING “REC” LED ---------------------------------------------
    entt::entity ledE = registry.create();
    registry.emplace<ParentComponent>(ledE, gizE);
    registry.emplace<RecordLedTag>(ledE);
    auto& lightPulse = registry.emplace<PulsingLightComponent>(ledE);
    lightPulse.onColor = glm::vec3(1.0f, 0.0f, 0.0f);
    lightPulse.offColor = glm::vec3(0.3f, 0.0f, 0.0f);
    lightPulse.speed = 6.0f;

    auto& lxf = registry.emplace<TransformComponent>(ledE);
    lxf.translation = { 0.1f, -0.115f, 0.275f };
    lxf.scale = glm::vec3(0.1f);

    auto& lmesh = registry.emplace<RenderableMeshComponent>(ledE);
    // This call is now correct because the buildIcoSphere in PrimitiveBuilders.hpp
    // was updated to use the 5-argument Vertex constructor.
    buildIcoSphere(lmesh.vertices, lmesh.indices);

    auto& ledMaterial = registry.emplace<MaterialComponent>(ledE);
    ledMaterial.albedoColor = glm::vec3(1.0f, 0.0f, 0.0f);

    return camE;
}

void SceneBuilder::spawnRobot(Scene& scene, const RobotDescription& description)
{
    auto& registry = scene.getRegistry();

    // Clean up only the entities that are part of the old robot.
    auto view = registry.view<LinkComponent>();
    registry.destroy(view.begin(), view.end());

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

            // Step A: Create the mesh with only position, normal, and UV data.
            const std::vector<float>& raw = Mesh::getLitCubeVertices();
            constexpr std::size_t stride = 8; // Stride is now 8 (pos, norm, uv)

            meshComp.vertices.clear();
            meshComp.vertices.reserve(raw.size() / stride);

            for (std::size_t i = 0; i < raw.size(); i += stride)
            {
                glm::vec3 pos{ raw[i],     raw[i + 1], raw[i + 2] };
                glm::vec3 normal{ raw[i + 3], raw[i + 4], raw[i + 5] };
                glm::vec2 uv{ raw[i + 6], raw[i + 7] };

                // THE FIX: Explicitly construct the Vertex object to resolve compiler ambiguity.
                // The tangent/bitangent will be zero-initialized before calculation.
                Vertex newVertex(pos, normal, uv, glm::vec3(0.0f), glm::vec3(0.0f));
                meshComp.vertices.push_back(newVertex);
            }
            meshComp.indices = Mesh::getLitCubeIndices();

            // Step B: Programmatically calculate the tangents and bitangents.
            MeshUtils::calculateTangentsAndBitangents(meshComp);
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

// --- Spline creation functions (no changes needed) ---

entt::entity SceneBuilder::makeCR(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::CatmullRom;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeParam(entt::registry& r,
    std::function<glm::vec3(float)> f,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    entt::entity e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Parametric;
    sp.parametric.func = std::move(f);
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeLinear(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Linear;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeBezier(entt::registry& r,
    const std::vector<glm::vec3>& cps,
    const glm::vec4& coreColour,
    const glm::vec4& glowColour,
    float glowThickness)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);
    SplineComponent sp;
    sp.type = SplineType::Bezier;
    sp.controlPoints = cps;
    sp.coreColour = coreColour;
    sp.glowColour = glowColour;
    sp.thickness = glowThickness;
    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}
