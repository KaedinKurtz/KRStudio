#pragma once

#include <entt/entt.hpp>

class Scene
{
public:
    Scene();
    ~Scene();

    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

private:
    entt::registry m_registry;
};