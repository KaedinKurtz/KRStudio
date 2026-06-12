#pragma once

#include "IRenderPass.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief Wireframe overlay of the collision geometry the solver ACTUALLY
 * uses — cooked trimesh/hull edges from the CollisionCookingService, plus
 * analytic primitives. Toggled by SceneProperties::showCollisionShapes
 * (View > Show Collision Shapes).
 *
 * Colors: green = static / scenery, orange = dynamic, cyan = kinematic,
 * red = legacy AABB fallback (no exact shape).
 */
class CollisionDebugPass : public IRenderPass
{
public:
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;

private:
    struct LineBatch {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
    };

    // Per-entity resolved edge data (revalidated when the mesh changes).
    struct EntityEntry {
        const void* meshData = nullptr; // vertices.data() identity check
        size_t vertexCount = 0;
        int shapeMode = -1; // CollisionCookingService::DebugShape
        std::shared_ptr<const std::vector<glm::vec3>> edges;
    };

    LineBatch& batchFor(QOpenGLFunctions_4_3_Core* gl, const void* key,
                        const std::vector<glm::vec3>& lines);
    void drawBatch(const RenderFrameContext& ctx, const LineBatch& batch,
                   const glm::mat4& model, const glm::vec3& color);

    // Cooked-mesh batches keyed by edge-list pointer (shared across instances)
    std::unordered_map<const void*, LineBatch> m_meshBatches;
    std::unordered_map<entt::entity, EntityEntry> m_entityCache;

    // Analytic primitives (unit-sized, scaled per draw)
    LineBatch m_unitBox;      // corners at ±0.5
    LineBatch m_unitSphere;   // 3 unit-radius circles
    LineBatch m_unitCapsule;  // X-axis stadium: r=1 circles at x=±0.5 + bars
};
