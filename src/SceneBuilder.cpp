#include "SceneBuilder.hpp"
#include "RobotDescription.hpp"
#include "Scene.hpp"
#include "components.hpp" // We need this to add components to entities

#include <QDebug>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

void SceneBuilder::spawnRobot(Scene& scene, const RobotDescription& description)
{
    qInfo() << "Spawning robot:" << QString::fromStdString(description.name);

    auto& registry = scene.getRegistry(); // Get a reference to the ECS registry.

    // This map is crucial. It will let us look up the entity ID of a link by its name.
    // We need this to correctly connect the joints to the correct parent and child link entities.
    std::unordered_map<std::string, entt::entity> linkNameToEntityMap;

    // --- First Pass: Create Entities for all Links ---
    // We create all the links first so that when we create the joints, we can be
    // sure that the parent and child link entities already exist.
    for (const auto& linkDesc : description.links)
    {
        auto linkEntity = registry.create(); // Create a new, empty entity in the scene.
        linkNameToEntityMap[linkDesc.name] = linkEntity; // Store the new entity ID in our map, keyed by its name.

        registry.emplace<TagComponent>(linkEntity, linkDesc.name); // Add a name tag to the entity.

        // --- Create and Emplace TransformComponent ---
        // Convert the RPY (Roll, Pitch, Yaw) Euler angles to a quaternion for robust rotation.
        glm::quat rotation = glm::quat(glm::vec3(linkDesc.visual_origin_rpy.x, linkDesc.visual_origin_rpy.y, linkDesc.visual_origin_rpy.z));
        // We create the TransformComponent and initialize it with the position and new rotation.
        registry.emplace<TransformComponent>(linkEntity, linkDesc.visual_origin_xyz, rotation);

        // --- Emplace Other Components ---
        registry.emplace<RenderableMeshComponent>(linkEntity); // Add a tag to mark this entity for rendering.
        // We will create and add a MaterialComponent in a future step.
    }

    // --- Second Pass: Create Entities for all Joints ---
    // Now we create the joints and connect them to the links we just made.
    for (const auto& jointDesc : description.joints)
    {
        auto jointEntity = registry.create(); // Create an entity for the joint itself.
        registry.emplace<TagComponent>(jointEntity, jointDesc.name); // Give the joint a name.

        // TODO: Create the JointComponent. This component holds all the rich data from the description.
        // auto& jointComponent = registry.emplace<JointComponent>(jointEntity, jointDesc);

        // TODO: Find and Link Parent/Child Entities
        // if (linkNameToEntityMap.count(jointDesc.parent_link_name)) {
        //     jointComponent.parentLink = linkNameToEntityMap.at(jointDesc.parent_link_name);
        // }
        // if (linkNameToEntityMap.count(jointDesc.child_link_name)) {
        //     jointComponent.childLink = linkNameToEntityMap.at(jointDesc.child_link_name);
        // }
    }

    qInfo() << "Finished spawning" << description.links.size() << "links and" << description.joints.size() << "joints.";
}
