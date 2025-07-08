template<class C>
static C& firstComponent(entt::registry& reg)
{
    auto v = reg.view<C>();            // build once
    return reg.get<C>(v.front());      // fetch the first component
}
