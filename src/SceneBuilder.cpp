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
    registry.emplace<ParentComponent>(gizE, camE);          // child-of cam
    registry.emplace<CameraGizmoTag>(gizE);

    auto& gxf = registry.emplace<TransformComponent>(gizE);
    gxf.translation.z = -0.35f;                             // sit in front
    gxf.scale = glm::vec3(0.12f);                   // shrink
    gxf.rotation =                                      // fix STL axes
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)) *
        glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));

    auto& gmesh = registry.emplace<RenderableMeshComponent>(gizE);
    loadStlIntoRenderable(
        "D:/RoboticsSoftware/external/miniViewportCamera.stl", gmesh);
    gmesh.colour = glm::vec4(colour, 1.0f);

    // 3 ── BLINKING “REC” LED ---------------------------------------------
    entt::entity ledE = registry.create();
    registry.emplace<ParentComponent>(ledE, gizE);          // child-of gizmo
    registry.emplace<RecordLedTag>(ledE);                   // marker

    auto& lxf = registry.emplace<TransformComponent>(ledE);
    lxf.translation = { 0.1f, -0.115f, 0.275f };              // tweak location
    lxf.scale = glm::vec3(0.1f);

    auto& lmesh = registry.emplace<RenderableMeshComponent>(ledE);
    buildIcoSphere(lmesh.vertices, lmesh.indices);           // or buildIcoSphere
    lmesh.colour = glm::vec4(1, 0, 0, 1);                   // starts bright red

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
    const glm::vec4& colour)
{
    auto e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::CatmullRom;
    sp.catmullRom = cps;          // NEW field name
    sp.colour = colour;

    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}

entt::entity SceneBuilder::makeParam(entt::registry& r,
    std::function<glm::vec3(float)> f,
    const glm::vec4& colour)
{
    entt::entity e = r.create();
    r.emplace<TransformComponent>(e);

    SplineComponent sp;
    sp.type = SplineType::Parametric;
    sp.parametric.func = std::move(f);
    sp.colour = colour;

    r.emplace<SplineComponent>(e, std::move(sp));
    return e;
}