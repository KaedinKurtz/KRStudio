#include "FieldSolver.hpp"
#include "components.hpp"
#include <entt/entt.hpp>
#include <glm/gtx/norm.hpp>

// --- Helper Functions for Geometric Calculations ---

// Finds the closest point on a line segment to a given point.
static glm::vec3 closestPointOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 ab = b - a;
    float t = glm::dot(p - a, ab) / glm::dot(ab, ab);
    return a + glm::clamp(t, 0.0f, 1.0f) * ab;
}

// Finds the closest point on a triangle to a given point.
// This is a simplified version; more robust solutions exist (e.g., Christer Ericson's "Real-Time Collision Detection").
static glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    // Check if P is in vertex region outside A
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;
    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a; // Barycentric coordinates (1,0,0)

    // Check if P is in vertex region outside B
    glm::vec3 bp = p - b;
    glm::vec3 bc = c - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b; // Barycentric coordinates (0,1,0)

    // Check if P is in edge region of AB
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab; // Barycentric coordinates (1-v,v,0)
    }

    // Check if P is in vertex region outside C
    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c; // Barycentric coordinates (0,0,1)

    // Check if P is in edge region of AC
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac; // Barycentric coordinates (1-w,0,w)
    }

    // Check if P is in edge region of BC
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * bc; // Barycentric coordinates (0,1-w,w)
    }

    // P is inside face region
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w; // = u*a + v*b + w*c, u = 1-v-w
}


// Calculates the total vector at a point by summing all effector influences.
glm::vec3 FieldSolver::getVectorAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources)
{
    glm::vec3 totalField(0.0f);

    // Create a view of all potential field sources.
    auto sourceView = registry.view<const FieldSourceTag, const TransformComponent>();

    for (auto entity : sourceView) {
        // If the visualizer has a specific list of sources, ignore any entities not in that list.
        if (!sources.empty() && std::find(sources.begin(), sources.end(), entity) == sources.end()) {
            continue;
        }

        const auto& transform = sourceView.get<const TransformComponent>(entity);

        // --- 1. Directional Effector ---
        // A constant force, like wind or gravity.
        if (auto* directional = registry.try_get<DirectionalEffectorComponent>(entity)) {
            totalField += glm::normalize(directional->direction) * directional->strength;
        }

        // --- 2. Point Effector ---
        // Repels or attracts from a single point.
        if (auto* point = registry.try_get<PointEffectorComponent>(entity)) {
            glm::vec3 entityPos = transform.translation;
            glm::vec3 vectorToPoint = worldPos - entityPos;
            float distanceSq = glm::length2(vectorToPoint);

            if (distanceSq < (point->radius * point->radius) && distanceSq > 1e-6f) {
                float distance = sqrt(distanceSq);
                glm::vec3 direction = vectorToPoint / distance;

                float strength = point->strength;
                if (point->falloff == PointEffectorComponent::FalloffType::Linear) {
                    strength *= (1.0f - (distance / point->radius));
                }
                else if (point->falloff == PointEffectorComponent::FalloffType::InverseSquare) {
                    strength /= distance; // Using 1/d for a more visually intuitive falloff
                }
                totalField += direction * strength;
            }
        }

        // --- 3. Spline Effector ---
        // Attracts to or repels from the nearest point on a spline.
        if (auto* splineEffector = registry.try_get<SplineEffectorComponent>(entity)) {
            if (auto* spline = registry.try_get<SplineComponent>(entity)) {
                if (spline->controlPoints.size() >= 2) {
                    glm::vec3 closestPointOnSpline;
                    float minDistanceSq = std::numeric_limits<float>::max();

                    // Iterate through the line segments of the spline
                    for (size_t i = 0; i < spline->controlPoints.size() - 1; ++i) {
                        glm::vec3 p_on_segment = closestPointOnSegment(worldPos, spline->controlPoints[i], spline->controlPoints[i + 1]);
                        float distSq = glm::length2(worldPos - p_on_segment);
                        if (distSq < minDistanceSq) {
                            minDistanceSq = distSq;
                            closestPointOnSpline = p_on_segment;
                        }
                    }

                    if (minDistanceSq < (splineEffector->radius * splineEffector->radius)) {
                        glm::vec3 vectorToSpline = closestPointOnSpline - worldPos;
                        totalField += glm::normalize(vectorToSpline) * splineEffector->strength;
                    }
                }
            }
        }

        // --- 4. Mesh Effector ---
        // Repels from the surface of a mesh.
        if (auto* meshEffector = registry.try_get<MeshEffectorComponent>(entity)) {
            if (auto* renderable = registry.try_get<RenderableMeshComponent>(entity)) {
                glm::vec3 closestPointOnMesh;
                float minDistanceSq = std::numeric_limits<float>::max();
                glm::mat4 modelMatrix = transform.getTransform();

                // Iterate through all triangles in the mesh
                for (size_t i = 0; i < renderable->indices.size(); i += 3) {
                    // Get vertices of the triangle and transform them to world space
                    glm::vec3 v0 = modelMatrix * glm::vec4(renderable->vertices[renderable->indices[i]].position, 1.0f);
                    glm::vec3 v1 = modelMatrix * glm::vec4(renderable->vertices[renderable->indices[i + 1]].position, 1.0f);
                    glm::vec3 v2 = modelMatrix * glm::vec4(renderable->vertices[renderable->indices[i + 2]].position, 1.0f);

                    glm::vec3 p_on_triangle = closestPointOnTriangle(worldPos, v0, v1, v2);
                    float distSq = glm::length2(worldPos - p_on_triangle);

                    if (distSq < minDistanceSq) {
                        minDistanceSq = distSq;
                        closestPointOnMesh = p_on_triangle;
                    }
                }

                if (minDistanceSq < (meshEffector->distance * meshEffector->distance)) {
                    glm::vec3 vectorFromMesh = worldPos - closestPointOnMesh;
                    float distance = glm::length(vectorFromMesh);
                    if (distance > 1e-6f) {
                        float strength = meshEffector->strength * (1.0f - (distance / meshEffector->distance));
                        totalField += (vectorFromMesh / distance) * strength;
                    }
                }
            }
        }
    }
    return totalField;
}


// Calculates the total scalar potential at a point from all sources.
// This function remains unchanged but is included for completeness.
float FieldSolver::getPotentialAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources) {
    float totalPotential = 0.0f;
    auto view = registry.view<const FieldSourceTag, const TransformComponent, const PointEffectorComponent>();

    for (auto entity : view) {
        if (!sources.empty() && std::find(sources.begin(), sources.end(), entity) == sources.end()) {
            continue;
        }
        const auto& transform = view.get<const TransformComponent>(entity);
        const auto& source = view.get<const PointEffectorComponent>(entity);

        // We only calculate potential for point effectors for now
        float distSq = glm::distance2(transform.translation, worldPos);
        if (distSq > 1e-6) {
            // Strengh < 0 is attractive, but potential should be negative
            // Strengh > 0 is repulsive, but potential should be positive
            totalPotential += -source.strength / distSq;
        }
    }
    return totalPotential;
}

// Calculates the gradient of the potential field at a point using finite differences.
// This function remains unchanged but is included for completeness.
glm::vec3 FieldSolver::getPotentialGradientAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources) {
    float epsilon = 0.01f;

    float pot_x1 = getPotentialAt(registry, worldPos + glm::vec3(epsilon, 0, 0), sources);
    float pot_x0 = getPotentialAt(registry, worldPos - glm::vec3(epsilon, 0, 0), sources);

    float pot_y1 = getPotentialAt(registry, worldPos + glm::vec3(0, epsilon, 0), sources);
    float pot_y0 = getPotentialAt(registry, worldPos - glm::vec3(0, epsilon, 0), sources);

    float pot_z1 = getPotentialAt(registry, worldPos + glm::vec3(0, 0, epsilon), sources);
    float pot_z0 = getPotentialAt(registry, worldPos - glm::vec3(0, 0, epsilon), sources);

    float dx = (pot_x1 - pot_x0) / (2.0f * epsilon);
    float dy = (pot_y1 - pot_y0) / (2.0f * epsilon);
    float dz = (pot_z1 - pot_z0) / (2.0f * epsilon);

    // The vector field is the negative gradient of the potential field
    return -glm::vec3(dx, dy, dz);
}
