#pragma once
// RevoluteFK.hpp -- SINGLE SOURCE OF TRUTH for one-revolute-joint forward kinematics. A point rigidly
// attached to the moving link, at world rest position `restPoint`, rotates about axis `axis` through
// `origin` by joint angle q (radians). Shared engine code (krs::kin), NOT a per-test formula -- it is
// consumed by the MQTT joint round-trip (GATE M), the node-graph link driver (RevoluteLinkFkNode /
// GATE ND), and their gates, so "the robot moves" is one definition used everywhere.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace krs::kin {

inline glm::vec3 revoluteApply(const glm::vec3& origin, const glm::vec3& axis,
                               const glm::vec3& restPoint, float q)
{
    const glm::mat4 R = glm::rotate(glm::mat4(1.0f), q, glm::normalize(axis));
    return origin + glm::vec3(R * glm::vec4(restPoint - origin, 0.0f));
}

} // namespace krs::kin
