#include "CollisionDebugPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "CollisionCookingService.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

constexpr glm::vec3 kStaticColor{ 0.20f, 0.90f, 0.30f };
constexpr glm::vec3 kDynamicColor{ 1.00f, 0.55f, 0.10f };
constexpr glm::vec3 kKinematicColor{ 0.10f, 0.80f, 0.90f };
constexpr glm::vec3 kFallbackColor{ 1.00f, 0.15f, 0.15f };

void appendCircle(std::vector<glm::vec3>& out, const glm::vec3& center,
                  const glm::vec3& axisU, const glm::vec3& axisV, float radius, int segments)
{
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(i) / segments * 6.2831853f;
        const float a1 = float(i + 1) / segments * 6.2831853f;
        out.push_back(center + radius * (std::cos(a0) * axisU + std::sin(a0) * axisV));
        out.push_back(center + radius * (std::cos(a1) * axisU + std::sin(a1) * axisV));
    }
}

std::vector<glm::vec3> unitBoxLines()
{
    const glm::vec3 c[8] = {
        { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f },
        { -0.5f, -0.5f, 0.5f },  { 0.5f, -0.5f, 0.5f },  { 0.5f, 0.5f, 0.5f },  { -0.5f, 0.5f, 0.5f },
    };
    const int e[24] = { 0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7 };
    std::vector<glm::vec3> lines;
    lines.reserve(24);
    for (int i : e) lines.push_back(c[i]);
    return lines;
}

std::vector<glm::vec3> unitSphereLines()
{
    std::vector<glm::vec3> lines;
    appendCircle(lines, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 1.0f, 32);
    appendCircle(lines, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 1.0f, 32);
    appendCircle(lines, { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 1.0f, 32);
    return lines;
}

std::vector<glm::vec3> unitCapsuleLines()
{
    // PhysX capsules extend along X: half-height caps at x = ±0.5, radius 1.
    std::vector<glm::vec3> lines;
    appendCircle(lines, { 0.5f, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 1.0f, 24);
    appendCircle(lines, { -0.5f, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 1.0f, 24);
    const glm::vec3 bars[4] = { { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const auto& b : bars) {
        lines.push_back(glm::vec3(-0.5f, 0, 0) + b);
        lines.push_back(glm::vec3(0.5f, 0, 0) + b);
    }
    // End-cap arcs in the XY and XZ planes.
    for (int half = 0; half < 2; ++half) {
        const float sign = half == 0 ? 1.0f : -1.0f;
        for (int i = 0; i < 12; ++i) {
            const float a0 = float(i) / 12 * 3.14159265f - 1.5707963f;
            const float a1 = float(i + 1) / 12 * 3.14159265f - 1.5707963f;
            lines.push_back({ 0.5f * sign + sign * std::cos(a0), std::sin(a0), 0 });
            lines.push_back({ 0.5f * sign + sign * std::cos(a1), std::sin(a1), 0 });
            lines.push_back({ 0.5f * sign + sign * std::cos(a0), 0, std::sin(a0) });
            lines.push_back({ 0.5f * sign + sign * std::cos(a1), 0, std::sin(a1) });
        }
    }
    return lines;
}

} // namespace

void CollisionDebugPass::initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core* gl)
{
    batchFor(gl, &m_unitBox, unitBoxLines());
    batchFor(gl, &m_unitSphere, unitSphereLines());
    batchFor(gl, &m_unitCapsule, unitCapsuleLines());
    m_unitBox = m_meshBatches[&m_unitBox];
    m_unitSphere = m_meshBatches[&m_unitSphere];
    m_unitCapsule = m_meshBatches[&m_unitCapsule];
}

CollisionDebugPass::LineBatch&
CollisionDebugPass::batchFor(QOpenGLFunctions_4_3_Core* gl, const void* key,
                             const std::vector<glm::vec3>& lines)
{
    auto it = m_meshBatches.find(key);
    if (it != m_meshBatches.end()) return it->second;

    LineBatch batch;
    gl->glGenVertexArrays(1, &batch.vao);
    gl->glGenBuffers(1, &batch.vbo);
    gl->glBindVertexArray(batch.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lines.size() * sizeof(glm::vec3)),
                     lines.data(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    gl->glBindVertexArray(0);
    batch.vertexCount = GLsizei(lines.size());
    return m_meshBatches.emplace(key, batch).first->second;
}

void CollisionDebugPass::drawBatch(const RenderFrameContext& ctx, const LineBatch& batch,
                                   const glm::mat4& model, const glm::vec3& color)
{
    if (!batch.vao || batch.vertexCount == 0) return;
    Shader* shader = ctx.renderer.getShader("collision_debug");
    if (!shader) return;
    shader->use(ctx.gl);
    shader->setMat4(ctx.gl, "u_mvp", ctx.projection * ctx.view * model);
    shader->setVec3(ctx.gl, "u_color", color);
    ctx.gl->glBindVertexArray(batch.vao);
    ctx.gl->glDrawArrays(GL_LINES, 0, batch.vertexCount);
    ctx.gl->glBindVertexArray(0);
}

void CollisionDebugPass::execute(const RenderFrameContext& context)
{
    const auto& props = context.registry.ctx().get<SceneProperties>();
    if (!props.showCollisionShapes) return;

    auto* gl = context.gl;
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_FALSE); // lines shouldn't occlude later overlays

    auto& reg = context.registry;
    auto& cooking = CollisionCookingService::instance();

    for (auto e : reg.view<TransformComponent>()) {
        if (reg.any_of<FluidEmitterComponent, FluidVolumeComponent, ParentComponent>(e)) continue;
        const auto& xf = reg.get<TransformComponent>(e);
        const auto* rb = reg.try_get<RigidBodyComponent>(e);
        const auto* mesh = reg.try_get<RenderableMeshComponent>(e);

        glm::vec3 color = kStaticColor;
        if (rb && rb->bodyType == RigidBodyComponent::BodyType::Dynamic) color = kDynamicColor;
        else if (rb && rb->bodyType == RigidBodyComponent::BodyType::Kinematic) color = kKinematicColor;

        const glm::mat4 tr = glm::translate(glm::mat4(1.0f), xf.translation) * glm::mat4_cast(xf.rotation);

        // Explicit primitives first — same priority as the physics backend.
        if (const auto* box = reg.try_get<BoxCollider>(e)) {
            glm::mat4 model = tr;
            model = glm::translate(model, box->offset * xf.scale);
            model = glm::scale(model, 2.0f * box->halfExtents * xf.scale);
            drawBatch(context, m_unitBox, model, color);
            continue;
        }
        if (const auto* sph = reg.try_get<SphereCollider>(e)) {
            const float r = sph->radius * std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            glm::mat4 model = tr;
            model = glm::translate(model, sph->offset * xf.scale);
            model = glm::scale(model, glm::vec3(r));
            drawBatch(context, m_unitSphere, model, color);
            continue;
        }
        if (const auto* cap = reg.try_get<CapsuleCollider>(e)) {
            const float s = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            glm::mat4 model = tr;
            // unit capsule: radius 1, caps at x=±0.5 -> scale X by height, YZ by radius
            model = glm::scale(model, glm::vec3(cap->height * s, cap->radius * s, cap->radius * s));
            drawBatch(context, m_unitCapsule, model, color);
            continue;
        }

        if (!mesh || mesh->vertices.empty()) continue;

        const auto* autoCol = reg.try_get<AutoCollisionComponent>(e);
        const bool legacyHull = reg.any_of<ConvexMeshCollider>(e);
        if (!autoCol && !legacyHull) {
            if (rb) { // backend AABB fallback — flag it loudly
                const glm::vec3 he = glm::max((mesh->aabbMax - mesh->aabbMin) * 0.5f, glm::vec3(0.01f));
                const glm::vec3 center = (mesh->aabbMax + mesh->aabbMin) * 0.5f;
                glm::mat4 model = tr;
                model = glm::translate(model, center * xf.scale);
                model = glm::scale(model, 2.0f * he * xf.scale);
                drawBatch(context, m_unitBox, model, kFallbackColor);
            }
            continue;
        }
        if (autoCol && autoCol->mode == AutoCollisionComponent::Mode::None) continue;

        // Same resolution the backend applies: exact trimesh for statics and
        // kinematics, V-HACD multi-hull when requested, hull otherwise.
        const bool dynamicBody = rb && rb->bodyType == RigidBodyComponent::BodyType::Dynamic;
        using Shape = CollisionCookingService::DebugShape;
        Shape shape = Shape::Hull;
        if (autoCol && !legacyHull) {
            if (autoCol->mode == AutoCollisionComponent::Mode::ConvexDecomposition)
                shape = Shape::Decomposition;
            else if (autoCol->mode == AutoCollisionComponent::Mode::AutoExact
                     && !dynamicBody && !autoCol->isTrigger)
                shape = Shape::Trimesh;
        }

        auto& entry = m_entityCache[e];
        if (entry.meshData != mesh->vertices.data() || entry.vertexCount != mesh->vertices.size()
            || entry.shapeMode != int(shape) || !entry.edges) {
            entry.meshData = mesh->vertices.data();
            entry.vertexCount = mesh->vertices.size();
            entry.shapeMode = int(shape);
            entry.edges = cooking.debugEdges(mesh->vertices, mesh->indices, shape);
        }
        if (!entry.edges) continue; // cook still in flight — retry next frame

        const LineBatch& batch = batchFor(gl, entry.edges.get(), *entry.edges);
        const glm::vec3 s = glm::abs(xf.scale);
        const glm::mat4 model = glm::scale(tr, glm::max(s, glm::vec3(1e-4f)));
        drawBatch(context, batch, model, color);
    }

    gl->glDepthMask(GL_TRUE);
}
