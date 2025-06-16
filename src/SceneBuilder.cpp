#include "SceneBuilder.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "Mesh.hpp"
#include <QDebug>

entt::entity SceneBuilder::createCamera(entt::registry& registry, const glm::vec3& position)
{
    auto entity = registry.create();
    registry.emplace<CameraComponent>(entity);
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.translation = position;

    auto& camera = registry.get<CameraComponent>(entity).camera;
    camera.forceRecalculateView(position, glm::vec3(0.0f, 0.0f, 0.0f), 0.0f);

    return entity;
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
