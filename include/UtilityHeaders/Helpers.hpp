#include <stdexcept> // Required for std::runtime_error

template <typename Component>
Component& firstComponent(entt::registry& registry)
{
    auto view = registry.view<Component>();
    if (view.empty())
    {
        // Throw an exception if no component of this type exists.
        // This prevents a crash and is easier to debug.
        throw std::runtime_error("Attempted to get the first component from an empty view.");
    }
    return view.get<Component>(view.front());
}