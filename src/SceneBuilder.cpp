#include "SceneBuilder.hpp"
#include "RobotDescription.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <QDebug>
#include <unordered_map>
#include <glm/gtc/quaternion.hpp>

void SceneBuilder::spawnRobot(Scene& scene, const RobotDescription& description)
{
    qInfo() << "Spawning robot:" << QString::fromStdString(description.name);
    auto& registry = scene.getRegistry();
    std::unordered_map<std::string, entt::entity> linkNameToEntityMap;

    // --- First Pass: Create Link Entities ---
    for (const auto& linkDesc : description.links) {
        auto linkEntity = registry.create();
        linkNameToEntityMap[linkDesc.name] = linkEntity;
        registry.emplace<TagComponent>(linkEntity, linkDesc.name);
        glm::quat rotation = glm::quat(glm::vec3(linkDesc.visual_origin_rpy.x, linkDesc.visual_origin_rpy.y, linkDesc.visual_origin_rpy.z));
        registry.emplace<TransformComponent>(linkEntity, linkDesc.visual_origin_xyz, rotation);
        registry.emplace<RenderableMeshComponent>(linkEntity);
        // We will add MaterialComponent in the next step
    }

    // --- Second Pass: Create Joint Entities and Build the Hierarchy ---
    for (const auto& jointDesc : description.joints) {
        if (linkNameToEntityMap.count(jointDesc.parent_link_name) && linkNameToEntityMap.count(jointDesc.child_link_name)) {
            entt::entity parentEntity = linkNameToEntityMap.at(jointDesc.parent_link_name);
            entt::entity childEntity = linkNameToEntityMap.at(jointDesc.child_link_name);

            // --- THIS IS THE KEY ---
            // The child link entity now has a component that explicitly states who its parent is.
            registry.emplace<ParentComponent>(childEntity, parentEntity);

            // The joint's transform is relative to the PARENT link.
            auto jointEntity = registry.create();
            registry.emplace<TagComponent>(jointEntity, jointDesc.name);
            auto& jointComponent = registry.emplace<JointComponent>(jointEntity, jointDesc);
            jointComponent.parentLink = parentEntity;
            jointComponent.childLink = childEntity;

            // Add a transform to the joint as well, defining its position relative to the parent link.
            glm::quat jointRotation = glm::quat(glm::vec3(jointDesc.origin_rpy.x, jointDesc.origin_rpy.y, jointDesc.origin_rpy.z));
            registry.emplace<TransformComponent>(jointEntity, jointDesc.origin_xyz, jointRotation);
            registry.emplace<ParentComponent>(jointEntity, parentEntity);
        }
    }
}
