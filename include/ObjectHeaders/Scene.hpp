// include/Scene.hpp
#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

class Scene
{
public:
    Scene();
    ~Scene();

    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

    // REFACTOR: Removed the concept of a single "main camera" from the scene
    // to support multiple independent viewports and cameras.
    void setPrimaryCamera(entt::entity camera);
    entt::entity getPrimaryCamera() const;
    entt::sigh<void(entt::entity)> onSelectionChanged;

private:
    entt::registry m_registry;

    entt::entity m_primaryCamera = entt::null;
};