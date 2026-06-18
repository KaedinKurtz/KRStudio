#include "OrbProbe.hpp"
#include "components.hpp"   // TransformComponent, GlassComponent, OrbBindingComponent, AutoCollisionComponent, TagComponent

#include <glm/gtc/quaternion.hpp>

namespace krs::orb {

OrbVelocity averageVelocityInSphere(const std::vector<glm::vec3>& positions,
                                    const std::vector<glm::vec3>& velocities,
                                    const glm::vec3& center, float radius)
{
    const float r2 = radius * radius;
    glm::vec3 sum(0.0f);
    int n = 0;
    const size_t count = std::min(positions.size(), velocities.size());
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3 d = positions[i] - center;
        if (glm::dot(d, d) <= r2) { sum += velocities[i]; ++n; }   // strictly a VOLUME containment test
    }
    return { n > 0 ? sum / float(n) : glm::vec3(0.0f), n };
}

void decorateProbeOrb(entt::registry& reg, entt::entity e, std::uint64_t nodeId,
                      const glm::vec3& color, const glm::vec3& center, float radius)
{
    // a measurement VOLUME, never a rigid body: strip any auto-collider the spawn path added.
    if (reg.all_of<AutoCollisionComponent>(e)) reg.remove<AutoCollisionComponent>(e);

    auto& xf = reg.get_or_emplace<TransformComponent>(e);
    xf.translation = center;
    xf.scale = glm::vec3(radius);

    auto& glass = reg.get_or_emplace<GlassComponent>(e);
    glass.tint = color;                  // the probe's glass takes the node's colour as its transmission tint

    auto& ob = reg.get_or_emplace<OrbBindingComponent>(e);
    ob.nodeId = nodeId;
    ob.color = color;
    ob.radius = radius;
}

entt::entity findOrbForNode(entt::registry& reg, std::uint64_t nodeId)
{
    auto view = reg.view<OrbBindingComponent>();
    for (auto e : view) if (view.get<OrbBindingComponent>(e).nodeId == nodeId) return e;
    return entt::null;
}

bool removeOrbForNode(entt::registry& reg, std::uint64_t nodeId)
{
    const entt::entity e = findOrbForNode(reg, nodeId);
    if (e == entt::null || !reg.valid(e)) return false;
    reg.destroy(e);
    return true;
}

int orbCount(entt::registry& reg)
{
    int n = 0;
    auto view = reg.view<OrbBindingComponent>();
    for (auto e : view) { (void)e; ++n; }
    return n;
}

} // namespace krs::orb
