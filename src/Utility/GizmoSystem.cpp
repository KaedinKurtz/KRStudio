#include "GizmoSystem.hpp"
#include "Scene.hpp"
#include "SceneBuilder.hpp" // Assuming you have this for creating primitives
#include "IntersectionSystem.hpp" // For ray definition and math
#include "Camera.hpp"
#include "PrimitiveBuilders.hpp"
#include <glm/glm.hpp>
#include "components.hpp"

#include <QGuiApplication>         // for QGuiApplication::keyboardModifiers()
#include <glm/gtc/constants.hpp>   // for glm::two_pi<float>()

#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <array>

// ---- Math helpers -------------------------------------------------

static inline glm::vec3 projectOntoPlane(const glm::vec3& v, const glm::vec3& n) {
    return v - glm::dot(v, n) * n;
}

static inline float signedAnglePlanar(const glm::vec3& a, const glm::vec3& b, const glm::vec3& n) {
    // n is the plane normal (rotation axis). Right-hand rule.
    float d = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
    float ang = std::acos(d);
    float s = (glm::dot(glm::cross(a, b), n) < 0.0f) ? -1.0f : 1.0f;
    return ang * s;
}

static inline float rotationSnapStepRad(const GizmoSystem::SnapSettings& s) {
    if (s.rotateSegments <= 0) return 0.0f;
    return glm::two_pi<float>() / float(s.rotateSegments);
}

// Signed angle between v0->v1 around axis N (all in world space)
static float signedAngleAround(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& N)
{
    glm::vec3 a = glm::normalize(v0);
    glm::vec3 b = glm::normalize(v1);
    float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    float ang = std::acos(d);
    float s = glm::dot(N, glm::cross(a, b)) >= 0.0f ? 1.0f : -1.0f;
    return ang * s;
}

// Parameter of point P on infinite line (O + t*A), with unit A recommended
static float projectPointOnAxis(const glm::vec3& P, const glm::vec3& O, const glm::vec3& A)
{
    glm::vec3 Ad = glm::normalize(A);
    return glm::dot(P - O, Ad);
}

// Closest points between a ray R = ro + u*rd (u>=0) and an infinite line L = lo + v*ld
// Returns via out params the u (tRayOut) on the ray and v (tLineOut) on the line.
// We clamp u>=0 (ray), we do NOT clamp v (infinite line).
static void closestRayLine(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& lo, const glm::vec3& ld,
    float& tRayOut, float& tLineOut)
{
    glm::vec3 r = rd; glm::vec3 l = glm::normalize(ld);
    glm::vec3 w0 = ro - lo;
    float a = glm::dot(r, r);
    float b = glm::dot(r, l);
    float c = glm::dot(l, l); // =1
    float d = glm::dot(r, w0);
    float e = glm::dot(l, w0);

    float denom = a * c - b * b;
    float u = 0.0f, v = 0.0f;

    if (std::abs(denom) > 1e-8f) {
        u = (b * e - c * d) / denom;
        v = (a * e - b * d) / denom;
    }
    else {
        // Nearly parallel: slide along ray
        u = 0.0f;
        v = e;
    }
    if (u < 0.0f) { // clamp ray to start
        u = 0.0f;
        v = e;
    }
    tRayOut = u;
    tLineOut = v;
}

// Distance between a ray and a finite segment; returns Euclidean distance,
// and outputs parameters along the ray and segment.
static float closestRaySegment(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& s0, const glm::vec3& s1,
    float& tRayOut, float& tSegOut)
{
    glm::vec3 u = rd;
    glm::vec3 v = s1 - s0;
    glm::vec3 w0 = ro - s0;

    float a = glm::dot(u, u);
    float b = glm::dot(u, v);
    float c = glm::dot(v, v);
    float d = glm::dot(u, w0);
    float e = glm::dot(v, w0);

    float denom = a * c - b * b;
    float tRay = 0.0f, tSeg = 0.0f;

    if (denom > 1e-8f) {
        tRay = (b * e - c * d) / denom;
        tSeg = (a * e - b * d) / denom;
    }
    else {
        // Parallel: choose tRay=0
        tRay = 0.0f;
        tSeg = e / c;
    }

    // Clamp segment param to [0,1]
    tSeg = glm::clamp(tSeg, 0.0f, 1.0f);

    // If ray param is negative, clamp and recompute segment param for that plane
    if (tRay < 0.0f) {
        tRay = 0.0f;
        tSeg = glm::clamp(glm::dot(v, -w0) / c, 0.0f, 1.0f);
    }

    glm::vec3 Pc = ro + tRay * u;
    glm::vec3 Qc = s0 + tSeg * v;
    tRayOut = tRay;
    tSegOut = tSeg;
    return glm::length(Pc - Qc);
}

// Ray-plane intersection: plane point O, normal N (need not be unit).
// Returns true if hit, with t along ray and the hit point H.
static bool rayPlane(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& O, const glm::vec3& N,
    float& tOut, glm::vec3& H)
{
    float denom = glm::dot(rd, N);
    if (std::abs(denom) < 1e-8f) return false; // parallel
    float t = glm::dot(O - ro, N) / denom;
    if (t < 0.0f) return false;                // behind ray
    tOut = t;
    H = ro + rd * t;
    return true;
}

// Ray-AABB in world space: returns true if hit, and tEnter as tOut.
static bool rayAABB(const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& bmin, const glm::vec3& bmax,
    float& tOut)
{
    glm::vec3 invD(1.0f / rd.x, 1.0f / rd.y, 1.0f / rd.z);
    glm::vec3 t0 = (bmin - ro) * invD;
    glm::vec3 t1 = (bmax - ro) * invD;

    glm::vec3 tminVec(glm::min(t0.x, t1.x),
        glm::min(t0.y, t1.y),
        glm::min(t0.z, t1.z));
    glm::vec3 tmaxVec(glm::max(t0.x, t1.x),
        glm::max(t0.y, t1.y),
        glm::max(t0.z, t1.z));

    float tEnter = std::max(std::max(tminVec.x, tminVec.y), tminVec.z);
    float tExit = std::min(std::min(tmaxVec.x, tmaxVec.y), tmaxVec.z);

    if (tExit >= tEnter && tExit >= 0.0f) { tOut = (tEnter >= 0.0f ? tEnter : tExit); return true; }
    return false;
}

// Transform a local AABB by a matrix, output world AABB
static void computeWorldAABB(const glm::vec3& bmin, const glm::vec3& bmax,
    const glm::mat4& M, glm::vec3& outMin, glm::vec3& outMax)
{
    std::array<glm::vec3, 8> corners{
        glm::vec3(bmin.x,bmin.y,bmin.z), glm::vec3(bmax.x,bmin.y,bmin.z),
        glm::vec3(bmin.x,bmax.y,bmin.z), glm::vec3(bmax.x,bmax.y,bmin.z),
        glm::vec3(bmin.x,bmin.y,bmax.z), glm::vec3(bmax.x,bmin.y,bmax.z),
        glm::vec3(bmin.x,bmax.y,bmax.z), glm::vec3(bmax.x,bmax.y,bmax.z)
    };
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (auto& c : corners) {
        glm::vec3 w = glm::vec3(M * glm::vec4(c, 1.0f));
        mn = glm::min(mn, w);
        mx = glm::max(mx, w);
    }
    outMin = mn; outMax = mx;
}

static inline glm::quat ringOrientationForAxis(GizmoAxis axis) {
    switch (axis) {
    case GizmoAxis::X: return glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)); // ring -> YZ
    case GizmoAxis::Y: return glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0)); // ring -> XZ
    case GizmoAxis::Z: default: return glm::quat(1, 0, 0, 0);                           // ring -> XY
    }
}

static inline glm::vec3 axisColor(GizmoAxis axis) {
    switch (axis) {
    case GizmoAxis::X: return { 1.00f, 0.13f, 0.13f }; // red
    case GizmoAxis::Y: return { 0.15f, 1.00f, 0.20f }; // green
    case GizmoAxis::Z: return { 0.20f, 0.35f, 1.00f }; // blue
    default:           return { 1.0f, 1.0f, 1.0f };
    }
}

GizmoSystem::GizmoSystem(Scene& scene) : m_scene(scene)
{
    // --- 1. Initialize Material Components ---
    M_RED = MaterialComponent{ {1.0f, 0.1f, 0.1f} };
    M_GREEN = MaterialComponent{ {0.1f, 1.0f, 0.1f} };
    M_BLUE = MaterialComponent{ {0.1f, 0.1f, 1.0f} };
    M_RED_GLOW = MaterialComponent{ {1.0f, 0.5f, 0.5f} };
    M_GREEN_GLOW = MaterialComponent{ {0.5f, 1.0f, 0.5f} };
    M_BLUE_GLOW = MaterialComponent{ {0.5f, 0.5f, 1.0f} };
    M_TEAL = MaterialComponent{ {0.1f, 0.8f, 0.8f} };
    M_PURPLE = MaterialComponent{ {0.8f, 0.1f, 0.8f} };
    M_ORANGE = MaterialComponent{ {1.0f, 0.5f, 0.1f} };
    M_TEAL_GLOW = MaterialComponent{ {0.5f, 1.0f, 1.0f} };
    M_PURPLE_GLOW = MaterialComponent{ {1.0f, 0.5f, 1.0f} };
    M_ORANGE_GLOW = MaterialComponent{ {1.0f, 0.8f, 0.5f} };
    M_GREY = MaterialComponent{ {0.5f, 0.5f, 0.5f} };

    auto& registry = m_scene.getRegistry();
    m_gizmoRoot = registry.create();
    registry.emplace<TransformComponent>(m_gizmoRoot);
    registry.emplace<TagComponent>(m_gizmoRoot, "GizmoRoot");

    // --- 2. Create All Handle Geometries ---
    createAxisHandles(GizmoMode::Translate, GizmoAxis::X, M_RED);
    createAxisHandles(GizmoMode::Translate, GizmoAxis::Y, M_GREEN);
    createAxisHandles(GizmoMode::Translate, GizmoAxis::Z, M_BLUE);

    auto xyPlane = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Quad, m_gizmoRoot);
    registry.get<TransformComponent>(xyPlane).translation = { 0.3f, 0.3f, 0.0f };
    registry.get<TransformComponent>(xyPlane).scale = glm::vec3(0.3f);
    registry.emplace<GizmoHandleComponent>(xyPlane, GizmoMode::Translate, GizmoAxis::XY);
    registry.emplace<MaterialComponent>(xyPlane, M_ORANGE);
    m_translateHandles.push_back(xyPlane);

    auto yzPlane = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Quad, m_gizmoRoot);
    registry.get<TransformComponent>(yzPlane).translation = { 0.0f, 0.3f, 0.3f };
    registry.get<TransformComponent>(yzPlane).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    registry.get<TransformComponent>(yzPlane).scale = glm::vec3(0.3f);
    registry.emplace<GizmoHandleComponent>(yzPlane, GizmoMode::Translate, GizmoAxis::YZ);
    registry.emplace<MaterialComponent>(yzPlane, M_TEAL);
    m_translateHandles.push_back(yzPlane);

    auto xzPlane = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Quad, m_gizmoRoot);
    registry.get<TransformComponent>(xzPlane).translation = { 0.3f, 0.0f, 0.3f };
    registry.get<TransformComponent>(xzPlane).rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
    registry.get<TransformComponent>(xzPlane).scale = glm::vec3(0.3f);
    registry.emplace<GizmoHandleComponent>(xzPlane, GizmoMode::Translate, GizmoAxis::XZ);
    registry.emplace<MaterialComponent>(xzPlane, M_PURPLE);
    m_translateHandles.push_back(xzPlane);

    createAxisHandles(GizmoMode::Rotate, GizmoAxis::X, M_RED);
    createAxisHandles(GizmoMode::Rotate, GizmoAxis::Y, M_GREEN);
    createAxisHandles(GizmoMode::Rotate, GizmoAxis::Z, M_BLUE);

    createAxisHandles(GizmoMode::Scale, GizmoAxis::X, M_RED);
    createAxisHandles(GizmoMode::Scale, GizmoAxis::Y, M_GREEN);
    createAxisHandles(GizmoMode::Scale, GizmoAxis::Z, M_BLUE);

    auto uniformScale = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Cube, m_gizmoRoot);
    registry.get<TransformComponent>(uniformScale).scale = glm::vec3(0.15f);
    registry.emplace<GizmoHandleComponent>(uniformScale, GizmoMode::Scale, GizmoAxis::XYZ);
    registry.emplace<MaterialComponent>(uniformScale, M_GREY);
    m_scaleHandles.push_back(uniformScale);

    // --- 3. Set Initial Mode ---
    setMode(GizmoMode::Translate);
}

void GizmoSystem::createAxisHandles(GizmoMode mode, GizmoAxis axis, const MaterialComponent& color)
{
    auto& registry = m_scene.getRegistry();
    glm::vec3 dir(0.0f);
    glm::quat rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (axis == GizmoAxis::X) {
        dir = { 1, 0, 0 };
        rot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, -1));
    }
    else if (axis == GizmoAxis::Y) {
        dir = { 0, 1, 0 };
        // No rotation needed for Y-axis primitives as they are built along Y
    }
    else { // Z
        dir = { 0, 0, 1 };
        rot = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
    }

    switch (mode) {
    case GizmoMode::Translate: {
        auto shaft = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Cylinder, m_gizmoRoot);
        registry.get<TransformComponent>(shaft).rotation = rot;
        registry.get<TransformComponent>(shaft).scale = { 0.02f, 0.5f, 0.02f };
        registry.get<TransformComponent>(shaft).translation = dir * 0.5f;
        registry.emplace<GizmoHandleComponent>(shaft, mode, axis);
        registry.emplace<MaterialComponent>(shaft, color);
        m_translateHandles.push_back(shaft);

        auto cone = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Cone, m_gizmoRoot);
        registry.get<TransformComponent>(cone).rotation = rot;
        registry.get<TransformComponent>(cone).scale = { 0.08f, 0.2f, 0.08f };
        registry.get<TransformComponent>(cone).translation = dir * 1.0f;
        registry.emplace<GizmoHandleComponent>(cone, mode, axis);
        registry.emplace<MaterialComponent>(cone, color);
        m_translateHandles.push_back(cone);
        break;
    }
    case GizmoMode::Rotate: {
        auto torus = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Torus, m_gizmoRoot);

        auto& tx = registry.get<TransformComponent>(torus);
        tx.rotation = ringOrientationForAxis(axis);

        // UNIFORM scale only, to preserve the donut cross-section
        // Adjust this if your torus primitive is not unit-sized.
        tx.scale = glm::vec3(1.0f);

        registry.emplace<GizmoHandleComponent>(torus, mode, axis);
        registry.emplace<MaterialComponent>(torus, color);
        m_rotateHandles.push_back(torus);
        break;
    }
    case GizmoMode::Scale: {
        auto line = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Cylinder, m_gizmoRoot);
        registry.get<TransformComponent>(line).rotation = rot;
        registry.get<TransformComponent>(line).scale = { 0.02f, 0.5f, 0.02f };
        registry.get<TransformComponent>(line).translation = dir * 0.5f;
        registry.emplace<GizmoHandleComponent>(line, mode, axis);
        registry.emplace<MaterialComponent>(line, color);
        m_scaleHandles.push_back(line);

        auto cube = SceneBuilder::spawnPrimitiveAsChild(m_scene, Primitive::Cube, m_gizmoRoot);
        registry.get<TransformComponent>(cube).translation = dir * 1.0f;
        registry.get<TransformComponent>(cube).scale = glm::vec3(0.1f);
        registry.emplace<GizmoHandleComponent>(cube, mode, axis);
        registry.emplace<MaterialComponent>(cube, color);
        m_scaleHandles.push_back(cube);
        break;
    }
    default: break;
    }
}

void GizmoSystem::setMode(GizmoMode mode)
{
    if (m_activeMode == mode) return;
    m_activeMode = mode;
    auto& registry = m_scene.getRegistry();

    auto setVisible = [&](const std::vector<entt::entity>& handles, bool visible) {
        for (entt::entity handle : handles) {
            if (visible) {
                if (registry.all_of<HiddenComponent>(handle)) {
                    registry.remove<HiddenComponent>(handle);
                }
            }
            else {
                registry.emplace_or_replace<HiddenComponent>(handle);
            }
        }
        };

    setVisible(m_translateHandles, m_activeMode == GizmoMode::Translate);
    setVisible(m_rotateHandles, m_activeMode == GizmoMode::Rotate);
    setVisible(m_scaleHandles, m_activeMode == GizmoMode::Scale);
}

void GizmoSystem::update(const QVector<entt::entity>& selectedEntities, const Camera& camera)
{
    auto& registry = m_scene.getRegistry();
    bool hasSelection = !selectedEntities.isEmpty();

    // Toggle root
    if (hasSelection && !m_isVisible) {
        if (registry.all_of<HiddenComponent>(m_gizmoRoot)) {
            registry.remove<HiddenComponent>(m_gizmoRoot);
        }
        m_isVisible = true;
    }
    else if (!hasSelection && m_isVisible) {
        registry.emplace_or_replace<HiddenComponent>(m_gizmoRoot);
        m_isVisible = false;
    }

    // Also explicitly toggle the handles (in case your renderer ignores parent HiddenComponent)
    auto setVisible = [&](const std::vector<entt::entity>& handles, bool visible) {
        for (entt::entity h : handles) {
            if (visible) {
                if (registry.all_of<HiddenComponent>(h)) registry.remove<HiddenComponent>(h);
            }
            else {
                registry.emplace_or_replace<HiddenComponent>(h);
            }
        }
        };

    // If nothing is selected, hide everything; else show only the active mode’s set
    if (!hasSelection) {
        setVisible(m_translateHandles, false);
        setVisible(m_rotateHandles, false);
        setVisible(m_scaleHandles, false);
    }
    else {
        setVisible(m_translateHandles, m_activeMode == GizmoMode::Translate);
        setVisible(m_rotateHandles, m_activeMode == GizmoMode::Rotate);
        setVisible(m_scaleHandles, m_activeMode == GizmoMode::Scale);
    }

    if (hasSelection && !m_isDragging) {
        glm::vec3 averagePosition(0.0f);
        TransformComponent firstObjectTransform;
        bool first = true;

        for (entt::entity entity : selectedEntities) {
            if (!registry.valid(entity)) continue;
            auto* transform = registry.try_get<TransformComponent>(entity);
            auto* mesh = registry.try_get<RenderableMeshComponent>(entity);
            if (!transform || !mesh) continue;

            glm::vec3 aabbCenter = (mesh->aabbMin + mesh->aabbMax) * 0.5f;
            averagePosition += glm::vec3(transform->getTransform() * glm::vec4(aabbCenter, 1.0f));

            if (first) {
                firstObjectTransform = *transform;
                first = false;
            }
        }
        averagePosition /= selectedEntities.size();

        auto& gizmoTransform = registry.get<TransformComponent>(m_gizmoRoot);
        gizmoTransform.translation = averagePosition;

        constexpr float kBaseGizmoDiameter = 2.0f;   // your handles are built ~±1.0
        constexpr float kTargetGizmoDiameter = 1.5f;   // 20 cm across
        const float scaleFactor = kTargetGizmoDiameter / kBaseGizmoDiameter; // = 0.1
        gizmoTransform.scale = glm::vec3(scaleFactor);

        switch (m_currentFrame) {
        case Frame::Body:
            gizmoTransform.rotation = firstObjectTransform.rotation;
            break;
        case Frame::Parent:
        case Frame::World:
        default:
            gizmoTransform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            break;
        }
    }
}


entt::entity GizmoSystem::intersect(const CpuRay& ray)
{
    // This is a placeholder. You'll need a proper ray-mesh intersection test.
    // For now, we'll use a simple ray-AABB test.
    entt::entity bestHit = entt::null;
    float minT = std::numeric_limits<float>::max();

    const auto* activeHandles = &m_translateHandles;
    if (m_activeMode == GizmoMode::Rotate) activeHandles = &m_rotateHandles;
    if (m_activeMode == GizmoMode::Scale) activeHandles = &m_scaleHandles;

    for (entt::entity handle : *activeHandles) {
        // Perform ray-AABB intersection test here...
        // If (hit and distance < minT), update bestHit and minT
    }
    return bestHit;
}

void GizmoSystem::startDrag(entt::entity handleEntity,
    entt::entity selectedEntity,
    const glm::vec3& hitPoint,
    const glm::vec3& viewDir)
{
    beginTransformEdit();
    m_isDragging = true;
    m_draggedHandle = handleEntity;
    m_selectedObject = selectedEntity;
    m_initialObjectTransform = m_scene.getRegistry().get<TransformComponent>(selectedEntity);
    m_dragStartPoint = hitPoint;
    m_hasScreenStart = false;

    auto& R = m_scene.getRegistry();
    const auto& gh = R.get<GizmoHandleComponent>(handleEntity);
    m_dragMode = gh.mode;
    m_dragAxisOrPlane = gh.axis;

    // Cache world axis/plane at mouse-down
    const auto& rootXf = R.get<TransformComponent>(m_gizmoRoot);
    switch (gh.axis) {
    case GizmoAxis::X:  m_dragAxisWorld = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(1, 0, 0)); break;
    case GizmoAxis::Y:  m_dragAxisWorld = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(0, 1, 0)); break;
    case GizmoAxis::Z:  m_dragAxisWorld = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(0, 0, 1)); break;
    case GizmoAxis::XY: m_dragPlaneNormal = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(0, 0, 1)); break;
    case GizmoAxis::YZ: m_dragPlaneNormal = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(1, 0, 0)); break;
    case GizmoAxis::XZ: m_dragPlaneNormal = glm::normalize(glm::mat3_cast(rootXf.rotation) * glm::vec3(0, 1, 0)); break;
    case GizmoAxis::XYZ: default: break;
    }
    m_gizmoOriginWorld = rootXf.translation;

    // Seed rotation / axis param
    if (m_dragMode == GizmoMode::Rotate) {
        // Use the HANDLE’s own plane normal (+Z in child-local mapped to world)
        const auto& rootXf = R.get<TransformComponent>(m_gizmoRoot);
        const glm::mat4 Mroot = rootXf.getTransform();
        const auto& hXf = R.get<TransformComponent>(handleEntity);
        const glm::mat4 Mchild = Mroot * hXf.getTransform();

        m_rotPlaneNormal = glm::normalize(glm::vec3(Mchild * glm::vec4(0, 0, 1, 0))); // ring normal
        m_v0 = glm::normalize(projectOntoPlane(m_dragStartPoint - m_gizmoOriginWorld, m_rotPlaneNormal));
    }
    else if (m_dragMode == GizmoMode::Translate || m_dragMode == GizmoMode::Scale) {
        m_t0 = projectPointOnAxis(m_dragStartPoint, m_gizmoOriginWorld, m_dragAxisWorld);
    }
    else {
        // Param along axis at start (for axis translate/scale).
        // For plane translate we’ll work directly from plane hits.
        m_t0 = projectPointOnAxis(m_dragStartPoint, m_gizmoOriginWorld, m_dragAxisWorld);
    }

    // Center (uniform) scale uses camera pull/push
    if (m_dragMode == GizmoMode::Scale && m_dragAxisOrPlane == GizmoAxis::XYZ) {
        glm::vec3 camFwd = glm::normalize(viewDir);
        m_dragAxisWorld = camFwd;
        m_t0 = projectPointOnAxis(m_dragStartPoint, m_gizmoOriginWorld, m_dragAxisWorld);
    }

    // Snapshot full selection for group behavior
    m_group.clear();
    for (auto eSel : R.view<SelectedComponent, TransformComponent>()) {
        DragItem di;
        di.e = eSel;
        di.t0 = R.get<TransformComponent>(eSel);
        di.pivotOffsetW = di.t0.translation - m_gizmoOriginWorld;
        m_group.push_back(di);
    }
    if (m_group.empty()) {
        // Fallback to single if somehow none were tagged
        DragItem di; di.e = selectedEntity; di.t0 = m_initialObjectTransform; di.pivotOffsetW = di.t0.translation - m_gizmoOriginWorld;
        m_group.push_back(di);
    }

    m_initialGizmoOrigin = m_gizmoOriginWorld;
    m_initialPivotOffsetWorld = m_initialObjectTransform.translation - m_gizmoOriginWorld;

    updateHighlights(entt::null);
}

void GizmoSystem::updateDrag(const CpuRay& ray, const Camera& camera)
{
    if (!m_isDragging || m_group.empty()) return;

    auto& R = m_scene.getRegistry();

    const glm::vec3 P = m_gizmoOriginWorld;

    // Shift => Local/Body frame, else World frame
    const bool useLocal = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);

    auto emitEdited = [&](entt::entity e) {
        if (onTransformEdited) onTransformEdited(e);
        };

    // Helpers that write back transforms for all group items
    auto moveGroupByDeltaW = [&](const glm::vec3& deltaW) {
        for (auto& di : m_group) {
            auto& t = R.get<TransformComponent>(di.e);
            t.translation = di.t0.translation + deltaW;
            emitEdited(di.e);
        }
        // Make gizmo follow for smoothness
        auto& rootXf = R.get<TransformComponent>(m_gizmoRoot);
        rootXf.translation = m_initialGizmoOrigin + deltaW;
        m_gizmoOriginWorld = rootXf.translation;
        };

    auto rotateGroupAsRigidAboutP = [&](const glm::quat& dq) {
        for (auto& di : m_group) {
            auto& t = R.get<TransformComponent>(di.e);
            // rotate position about P
            glm::vec3 r0 = di.pivotOffsetW;
            glm::vec3 r1 = dq * r0;
            t.translation = P + r1;
            // also rotate orientation
            t.rotation = dq * di.t0.rotation;
            emitEdited(di.e);
        }
        // gizmo stays at P for rotation
        };

    auto translateEachAlongAxis = [&](const glm::vec3& axisWorld, float delta) {
        if (!useLocal) {
            // world-frame: same axis for all
            glm::vec3 dW = delta * axisWorld;
            moveGroupByDeltaW(dW);
        }
        else {
            // local-frame: each uses its own local axis
            for (auto& di : m_group) {
                auto& t = R.get<TransformComponent>(di.e);
                glm::vec3 ai = glm::mat3_cast(di.t0.rotation) * axisWorld; // treat axisWorld as local basis X/Y/Z
                ai = glm::normalize(ai);
                t.translation = di.t0.translation + delta * ai;
                emitEdited(di.e);
            }
            auto& rootXf = R.get<TransformComponent>(m_gizmoRoot);
            rootXf.translation = m_initialGizmoOrigin + delta * axisWorld; // central follow (optional)
            m_gizmoOriginWorld = rootXf.translation;
        }
        };

    auto translateEachInPlane = [&](const glm::vec3& planeN, const glm::vec3& dragStart) {
        float t; glm::vec3 H;
        if (!rayPlane(ray.origin, ray.dir, P, planeN, t, H)) return;
        glm::vec3 delta = H - dragStart;
        // drop any drift normal to plane
        delta -= glm::dot(delta, planeN) * planeN;

        if (!useLocal) {
            // world plane: move everyone by same world delta
            moveGroupByDeltaW(delta);
        }
        else {
            // local plane: per-object 2D basis
            for (auto& di : m_group) {
                auto& t = R.get<TransformComponent>(di.e);
                glm::mat3 R0 = glm::mat3_cast(di.t0.rotation);
                // Build two basis vectors from planeN projected into object local frame
                // Choose axes that lie in the plane (e.g., for XY plane, use local X/Y)
                // We infer basis from planeN vs local axes:
                glm::vec3 Xl = R0 * glm::vec3(1, 0, 0);
                glm::vec3 Yl = R0 * glm::vec3(0, 1, 0);
                glm::vec3 Zl = R0 * glm::vec3(0, 0, 1);

                // Keep the two axes with smallest |dot(N, axis)| (most in-plane)
                struct A { glm::vec3 v; float d; };
                A a[3] = { {Xl, std::abs(glm::dot(planeN,Xl))},
                           {Yl, std::abs(glm::dot(planeN,Yl))},
                           {Zl, std::abs(glm::dot(planeN,Zl))} };
                std::sort(std::begin(a), std::end(a), [](const A& p, const A& q) {return p.d < q.d; });
                glm::vec3 U = glm::normalize(a[0].v);
                glm::vec3 V = glm::normalize(a[1].v);

                // Project world delta into this per-object 2D span
                glm::vec3 diW = glm::dot(delta, U) * U + glm::dot(delta, V) * V;
                t.translation = di.t0.translation + diW;
                emitEdited(di.e);
            }
            // Gizmo stays near plane intersection at H, but we can keep origin stable here
        }
        };

    auto worldAxisVec = [&](GizmoAxis ax)->glm::vec3 {
        switch (ax) {
        case GizmoAxis::X: return glm::vec3(1, 0, 0);
        case GizmoAxis::Y: return glm::vec3(0, 1, 0);
        case GizmoAxis::Z: return glm::vec3(0, 0, 1);
        default: return glm::vec3(0, 0, 0);
        }
        };

    // === TRANSLATE ===
    if (m_dragMode == GizmoMode::Translate) {
        if (m_dragAxisOrPlane == GizmoAxis::X || m_dragAxisOrPlane == GizmoAxis::Y || m_dragAxisOrPlane == GizmoAxis::Z) {
            // --- Screen-space axis translate ---
            // Camera basis (world-space)
            const glm::mat4 V = camera.getViewMatrix();
            const glm::mat4 invV = glm::inverse(V);
            const glm::vec3 camRight = glm::normalize(glm::vec3(invV[0])); // column 0
            const glm::vec3 camUp = glm::normalize(glm::vec3(invV[1])); // column 1
            const glm::vec3 camForward = glm::normalize(glm::vec3(invV[2])); // column 2 (toward scene)
            const glm::vec3 camPos = glm::vec3(invV[3]);

            // Screen-aligned plane through the gizmo origin
            auto intersectScreenPlane = [&](glm::vec3& H)->bool {
                float t;
                if (!rayPlane(ray.origin, ray.dir, P, camForward, t, H)) return false;
                return t >= 0.0f;
                };

            // Initialize the reference point at drag start (project start onto screen plane)
            if (!m_hasScreenStart) {
                // m_dragStartPoint is the world-space point we grabbed on the handle
                glm::vec3 s = m_dragStartPoint;
                glm::vec3 toS = s - P;
                m_screenStart = s - glm::dot(toS, camForward) * camForward; // projection onto screen plane through P
                m_hasScreenStart = true;
            }

            // Current intersection of cursor ray with the same screen plane
            glm::vec3 H;
            if (!intersectScreenPlane(H)) {
                // Fallback: use previous axis/parametric method if degenerate (rare)
                float tRay = 0, tAxis = 0; closestRayLine(ray.origin, ray.dir, P, m_dragAxisWorld, tRay, tAxis);
                float delta = (tAxis - m_t0);
                if (m_snap.translationStep > 0.0f) delta = m_snap.translationStep * std::round(delta / m_snap.translationStep);
                translateEachAlongAxis(m_dragAxisWorld, delta);
                return;
            }

            // Motion on the screen plane since drag start
            glm::vec3 d = H - m_screenStart;

            // Project the selected axis into the screen plane to set the "drag direction" on screen
            glm::vec3 A = glm::normalize(m_dragAxisWorld);
            glm::vec3 As = A - glm::dot(A, camForward) * camForward;  // axis projection to screen plane
            float AsLen = glm::length(As);

            // Robust fallback if axis is almost parallel to camera forward (projection collapses)
            if (AsLen < 1e-4f) {
                float rx = std::abs(glm::dot(A, camRight));
                float ry = std::abs(glm::dot(A, camUp));
                As = (rx >= ry ? glm::sign(glm::dot(A, camRight)) * camRight
                    : glm::sign(glm::dot(A, camUp)) * camUp);
                AsLen = 1.0f;
            }
            else {
                As /= AsLen;
            }

            // Scalar delta along the on-screen axis direction.
            // Moving along As increases translation in +axis; opposite decreases.
            float delta = glm::dot(d, As);

            // Normalize by camera distance so sensitivity feels consistent
            if (m_axisTranslateDistanceNormalize) {
                float dist = glm::length(P - camPos);
                delta /= glm::max(dist, 1e-3f);
            }

            // Apply sensitivity
            delta *= m_axisTranslateSensitivity;

            // Snap (world units)
            if (m_snap.translationStep > 0.0f) {
                delta = m_snap.translationStep * std::round(delta / m_snap.translationStep);
            }

            // Apply along the actual world axis (group-aware, respects Shift for local/world)
            translateEachAlongAxis(A, delta);
            return;
        }

        // (leave your plane-translate branch as-is)
        translateEachInPlane(m_dragPlaneNormal, m_dragStartPoint);
        return;
    }

    // === ROTATE ===
    if (m_dragMode == GizmoMode::Rotate) {
        float t; glm::vec3 H;
        if (!rayPlane(ray.origin, ray.dir, P, m_rotPlaneNormal, t, H)) return;

        // Work strictly in the plane of the selected ring
        glm::vec3 n = glm::normalize(m_rotPlaneNormal);

        auto projPlaneNorm = [&](const glm::vec3& v) -> glm::vec3 {
            glm::vec3 vp = v - glm::dot(v, n) * n;
            float len = glm::length(vp);
            return (len > 1e-8f) ? (vp / len) : glm::vec3(1, 0, 0); // safe fallback
            };

        // v0 captured at mouse-down; project both onto the plane
        glm::vec3 v0p = projPlaneNorm(m_v0);
        glm::vec3 v1p = projPlaneNorm(H - P);

        // Signed angle around the ring normal (right-hand rule)
        float c = glm::clamp(glm::dot(v0p, v1p), -1.0f, 1.0f);
        float ang = std::acos(c);
        float sgn = (glm::dot(glm::cross(v0p, v1p), n) < 0.0f) ? -1.0f : 1.0f;
        ang *= sgn;

        // Snap by rotateSegments, if enabled
        float step = rotationSnapStepRad(m_snap); // 2pi / segments, or 0
        if (step > 0.0f) ang = step * std::round(ang / step);

        glm::quat dq = glm::angleAxis(ang, n);

        if (!useLocal) {
            // World-frame: rotate the whole group about gizmo origin P
            rotateGroupAsRigidAboutP(dq);
        }
        else {
            // Local-frame: each object rotates in place around the same world axis
            for (auto& di : m_group) {
                auto& t = R.get<TransformComponent>(di.e);
                t.translation = di.t0.translation;      // no orbiting in local mode
                t.rotation = dq * di.t0.rotation;    // rotate orientation only
                emitEdited(di.e);
            }
        }
        return;
    }

    // === SCALE ===
    if (m_dragMode == GizmoMode::Scale) {
        if (m_dragAxisOrPlane == GizmoAxis::X || m_dragAxisOrPlane == GizmoAxis::Y || m_dragAxisOrPlane == GizmoAxis::Z) {
            float tRay = 0, tAxis = 0; closestRayLine(ray.origin, ray.dir, P, m_dragAxisWorld, tRay, tAxis);
            float delta = (tAxis - m_t0);
            float s = 1.0f + delta;
            if (m_snap.scaleStep > 0.0f) { s = m_snap.scaleStep * std::round(s / m_snap.scaleStep); if (s <= 0.0f) s = m_snap.scaleStep; }

            if (!useLocal) {
                // WORLD frame: move objects farther/closer along that world axis (don’t resize objects)
                glm::vec3 A = glm::normalize(m_dragAxisWorld);
                for (auto& di : m_group) {
                    auto& t = R.get<TransformComponent>(di.e);
                    glm::vec3 off = di.pivotOffsetW;
                    glm::vec3 offPar = glm::dot(off, A) * A;
                    glm::vec3 offPer = off - offPar;
                    glm::vec3 newOff = offPer + s * offPar;
                    t.translation = P + newOff;
                    t.scale = di.t0.scale; // keep size unchanged in world-frame scale
                    emitEdited(di.e);
                }
            }
            else {
                // LOCAL frame: change each object's local scale on that component
                for (auto& di : m_group) {
                    auto& t = R.get<TransformComponent>(di.e);
                    glm::vec3 sc = di.t0.scale;
                    if (m_dragAxisOrPlane == GizmoAxis::X) sc.x = di.t0.scale.x * s;
                    if (m_dragAxisOrPlane == GizmoAxis::Y) sc.y = di.t0.scale.y * s;
                    if (m_dragAxisOrPlane == GizmoAxis::Z) sc.z = di.t0.scale.z * s;
                    t.scale = sc;
                    emitEdited(di.e);
                }
            }
            return;
        }

        // Uniform (center cube) scale: push/pull along view axis set in startDrag
        if (m_dragAxisOrPlane == GizmoAxis::XYZ) {
            // Camera basis (world-space)
            const glm::mat4 V = camera.getViewMatrix();
            const glm::mat4 invV = glm::inverse(V);
            const glm::vec3 camRight = glm::normalize(glm::vec3(invV[0])); // column 0
            const glm::vec3 camUp = glm::normalize(glm::vec3(invV[1])); // column 1
            const glm::vec3 camForward = glm::normalize(glm::vec3(invV[2])); // column 2 (toward scene)

            // Intersect the cursor ray with a screen-aligned plane through the gizmo origin.
            // Using +camForward or -camForward gives the same plane; choose + for consistency.
            float t; glm::vec3 H;
            if (!rayPlane(ray.origin, ray.dir, P, camForward, t, H)) {
                // Fallback to old behavior if perfectly edge-on (rare)
                float tRay = 0, tAxis = 0;
                closestRayLine(ray.origin, ray.dir, P, m_dragAxisWorld, tRay, tAxis);
                float delta = (tAxis - m_t0);
                if (m_centerScaleDistanceNormalize) {
                    const glm::vec3 camPos = glm::vec3(invV[3]);
                    float dist = glm::length(P - camPos);
                    delta /= glm::max(dist, 1e-3f);
                }
                delta *= m_centerScaleSensitivity;
                float s = 1.0f + delta;
                if (m_snap.scaleStep > 0.0f) { s = m_snap.scaleStep * std::round(s / m_snap.scaleStep); if (s <= 0.0f) s = m_snap.scaleStep; }
                else if (s < 1e-3f) s = 1e-3f;

                if (!useLocal) {
                    for (auto& di : m_group) {
                        auto& tOut = R.get<TransformComponent>(di.e);
                        tOut.translation = P + s * di.pivotOffsetW; // spread/contract group
                        tOut.scale = di.t0.scale;             // keep object sizes
                        emitEdited(di.e);
                    }
                }
                else {
                    for (auto& di : m_group) {
                        auto& tOut = R.get<TransformComponent>(di.e);
                        tOut.translation = di.t0.translation;
                        tOut.scale = di.t0.scale * s;         // resize each object
                        emitEdited(di.e);
                    }
                }
                return;
            }

            // Reference point at drag start: use the gizmo origin (clean & stable)
            const glm::vec3 H0 = P;

            // Screen-plane motion vector
            glm::vec3 d = H - H0;

            // Right/up increase, left/down decrease
            float deltaRU = glm::dot(d, camRight) + glm::dot(d, camUp);

            // Normalize by camera distance (optional) + sensitivity gain
            float delta = deltaRU;
            if (m_centerScaleDistanceNormalize) {
                const glm::vec3 camPos = glm::vec3(invV[3]);          // camera world position
                float dist = glm::length(P - camPos);
                delta /= glm::max(dist, 1e-3f);
            }
            delta *= m_centerScaleSensitivity;

            // Map to scale factor
            float s = 1.0f + delta;

            // Snap/clamp
            if (m_snap.scaleStep > 0.0f) {
                s = m_snap.scaleStep * std::round(s / m_snap.scaleStep);
                if (s <= 0.0f) s = m_snap.scaleStep;                  // avoid non-positive scales
            }
            else if (s < 1e-3f) {
                s = 1e-3f;
            }

            if (!useLocal) {
                // WORLD frame: spread/contract the group relative to the gizmo origin
                for (auto& di : m_group) {
                    auto& tOut = R.get<TransformComponent>(di.e);
                    tOut.translation = P + s * di.pivotOffsetW;
                    tOut.scale = di.t0.scale;                   // size unchanged
                    emitEdited(di.e);
                }
            }
            else {
                // LOCAL frame: uniformly resize each object (keep positions)
                for (auto& di : m_group) {
                    auto& tOut = R.get<TransformComponent>(di.e);
                    tOut.translation = di.t0.translation;
                    tOut.scale = di.t0.scale * s;
                    emitEdited(di.e);
                }
            }
            return;
        }
    }
}


void GizmoSystem::setHoveredHandle(entt::entity handle)
{
    auto& R = m_scene.getRegistry();

    // clear all prior hovers
    for (auto e : R.view<HoveredGizmoTag>()) R.remove<HoveredGizmoTag>(e);
    if (handle == entt::null) return;

    const auto& gh = R.get<GizmoHandleComponent>(handle);
    const auto tagIt = (gh.mode == GizmoMode::Translate) ? m_translateHandles
        : (gh.mode == GizmoMode::Rotate) ? m_rotateHandles
        : m_scaleHandles;

    for (auto e : tagIt) {
        if (!R.valid(e) || !R.all_of<GizmoHandleComponent>(e)) continue;
        const auto& other = R.get<GizmoHandleComponent>(e);
        if (other.mode == gh.mode && other.axis == gh.axis) {
            R.emplace_or_replace<HoveredGizmoTag>(e);
        }
    }
}

void GizmoSystem::endDrag()
{
    endTransformEdit();
    m_isDragging = false;
    m_snap.didStep = false;
    updateHighlights(entt::null); // back to hover-only
    m_draggedHandle = entt::null;
    m_selectedObject = entt::null;

}

void GizmoSystem::updateHighlights(entt::entity activeHandle)
{
    auto& R = m_scene.getRegistry();

    // Clear all active tags
    for (auto e : R.view<ActiveGizmoTag>()) R.remove<ActiveGizmoTag>(e);

    if (activeHandle == entt::null) return;

    const auto& gh = R.get<GizmoHandleComponent>(activeHandle);
    const auto tagIt = (gh.mode == GizmoMode::Translate) ? m_translateHandles
        : (gh.mode == GizmoMode::Rotate) ? m_rotateHandles
        : m_scaleHandles;

    for (auto e : tagIt) {
        if (!R.valid(e) || !R.all_of<GizmoHandleComponent>(e)) continue;
        const auto& other = R.get<GizmoHandleComponent>(e);
        if (other.mode == gh.mode && other.axis == gh.axis) {
            R.emplace_or_replace<ActiveGizmoTag>(e);
        }
    }
}


void GizmoSystem::onTranslateDoubleClick(GizmoAxis axis) {
    qDebug() << "[GIZMO] Translate double-click on axis" << int(axis);
    // TODO: raycast plane snap / centre move etc.
}
void GizmoSystem::onScaleDoubleClick(GizmoAxis axis) {
    qDebug() << "[GIZMO] Scale double-click on axis" << int(axis);
    // TODO: uniform/axis snap etc.
}

entt::entity GizmoSystem::pickHandle(const CpuRay& ray)
{
    auto& R = m_scene.getRegistry();

    // Choose currently visible set
    const std::vector<entt::entity>* handles =
        (m_activeMode == GizmoMode::Translate) ? &m_translateHandles :
        (m_activeMode == GizmoMode::Rotate) ? &m_rotateHandles :
        &m_scaleHandles;

    if (!handles || handles->empty()) return entt::null;

    // Root transform (children are under the gizmo root)
    const TransformComponent& rootXf = R.get<TransformComponent>(m_gizmoRoot);
    const glm::mat4 Mroot = rootXf.getTransform();
    const glm::vec3 O = rootXf.translation;

    auto axisWorld = [&](GizmoAxis a)->glm::vec3 {
        glm::vec3 aL = (a == GizmoAxis::X) ? glm::vec3(1, 0, 0)
            : (a == GizmoAxis::Y) ? glm::vec3(0, 1, 0)
            : glm::vec3(0, 0, 1);
        return glm::normalize(glm::mat3_cast(rootXf.rotation) * aL);
        };

    // Local AABB slab test helper in child local space
    auto rayAABBLocal = [](const glm::vec3& ro, const glm::vec3& rd,
        const glm::vec3& bmin, const glm::vec3& bmax,
        float& tEnter, float& tExit)->bool {
            glm::vec3 invD(1.0f / rd.x, 1.0f / rd.y, 1.0f / rd.z);
            glm::vec3 t0 = (bmin - ro) * invD;
            glm::vec3 t1 = (bmax - ro) * invD;
            glm::vec3 tminVec(glm::min(t0.x, t1.x),
                glm::min(t0.y, t1.y),
                glm::min(t0.z, t1.z));
            glm::vec3 tmaxVec(glm::max(t0.x, t1.x),
                glm::max(t0.y, t1.y),
                glm::max(t0.z, t1.z));
            tEnter = std::max(std::max(tminVec.x, tminVec.y), tminVec.z);
            tExit = std::min(std::min(tmaxVec.x, tmaxVec.y), tmaxVec.z);
            return (tExit >= tEnter) && (tExit >= 0.0f);
        };

    // Tunables
    const float AXIS_GRAB_RADIUS = 0.10f;  // world units radius around axis
    const float AXIS_TIP_PAD = 0.25f;  // extend half-length to near arrow tip
    const float RING_THICKNESS = 0.10f;  // rotate band half-thickness (local)
    const float PLANE_THICK = 0.05f;  // plane patch local z-thickness
    const float CUBE_PAD = 0.05f;  // center cube aabb pad (local)

    entt::entity best = entt::null;
    float bestMetric = std::numeric_limits<float>::max();

    // Camera-to-gizmo direction (for plane facing test)
    const glm::vec3 camDir = glm::normalize(O - ray.origin);

    for (entt::entity h : *handles) {
        if (!R.valid(h) || !R.all_of<GizmoHandleComponent, TransformComponent>(h))
            continue;
        if (R.all_of<HiddenComponent>(h)) continue; // invisible

        const auto& gh = R.get<GizmoHandleComponent>(h);
        const auto& tx = R.get<TransformComponent>(h);
        const glm::mat4 Mchild = Mroot * tx.getTransform();
        const glm::mat4 invM = glm::inverse(Mchild);

        // Common transforms for child-local tests
        glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1));
        glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0)));

        // Child world position (useful for half-side determination and length)
        const glm::vec3 childPos = glm::vec3(Mchild[3]);

        if (m_activeMode == GizmoMode::Rotate) {
            // Rotate ring pick in CHILD-LOCAL space: torus lies in local XY, normal +Z.
            if (std::abs(rdL.z) < 1e-6f) continue;
            float tL = -roL.z / rdL.z;
            if (tL < 0.0f) continue;

            glm::vec3 hitL = roL + rdL * tL;     // near XY circle
            float Rring = 1.0f;               // ring radius (unit torus)
            float bandDist = std::abs(glm::length(glm::vec2(hitL.x, hitL.y)) - Rring);

            if (bandDist <= RING_THICKNESS) {
                glm::vec3 hitW = glm::vec3(Mchild * glm::vec4(hitL, 1));
                float worldT = glm::length(hitW - ray.origin);
                float metric = bandDist * 10.0f + worldT * 0.001f;
                if (metric < bestMetric) { bestMetric = metric; best = h; }
            }
            continue;
        }

        else if (m_activeMode == GizmoMode::Translate) {
            if (gh.axis == GizmoAxis::X || gh.axis == GizmoAxis::Y || gh.axis == GizmoAxis::Z) {
                // === Finite, one-sided AXIS segment (no backside grabs) ===
                glm::vec3 A = axisWorld(gh.axis);         // world axis dir (unit)
                float sideDot = glm::dot(childPos - O, A);
                bool  isPlus = (sideDot >= 0.0f);

                // Dynamic half-length from gizmo origin to handle (plus a pad to tip)
                float L = glm::length(childPos - O) + AXIS_TIP_PAD;

                // Closest points between cursor ray and infinite axis line
                float tRay = 0.0f, tLine = 0.0f;
                closestRayLine(ray.origin, ray.dir, O, A, tRay, tLine);

                // Clip to our visible half and length
                bool inHalfRange = isPlus ? (tLine >= 0.0f && tLine <= L)
                    : (tLine <= 0.0f && tLine >= -L);
                if (!inHalfRange) continue;

                // Distance-to-line test
                glm::vec3 Pc = ray.origin + tRay * ray.dir;
                glm::vec3 Lc = O + tLine * A;
                float d = glm::length(Pc - Lc);
                if (d <= AXIS_GRAB_RADIUS && tRay < bestMetric) { bestMetric = tRay; best = h; }
            }
            else {
                // === Plane patches: only face the camera; then local AABB ===
                // Plane normal in world: local +Z transformed
                glm::vec3 planeN = glm::normalize(glm::vec3(Mchild * glm::vec4(0, 0, 1, 0)));
                if (glm::dot(planeN, camDir) <= 0.0f) continue; // back-facing, skip

                // Unit quad in local XY, with small thickness in Z
                const glm::vec3 bmin(-0.5f, -0.5f, -PLANE_THICK);
                const glm::vec3 bmax(0.5f, 0.5f, PLANE_THICK);
                float t0, t1;
                if (rayAABBLocal(roL, rdL, bmin, bmax, t0, t1)) {
                    glm::vec3 worldHit = glm::vec3(Mchild * glm::vec4(roL + rdL * t0, 1));
                    float worldT = glm::length(worldHit - ray.origin);
                    if (worldT < bestMetric) { bestMetric = worldT; best = h; }
                }
            }
        }
        else { // Scale
            if (gh.axis == GizmoAxis::X || gh.axis == GizmoAxis::Y || gh.axis == GizmoAxis::Z) {
                // === Finite, one-sided AXIS segment for scale ===
                glm::vec3 A = axisWorld(gh.axis);
                float sideDot = glm::dot(childPos - O, A);
                bool  isPlus = (sideDot >= 0.0f);

                float L = glm::length(childPos - O) + AXIS_TIP_PAD;

                float tRay = 0.0f, tLine = 0.0f;
                closestRayLine(ray.origin, ray.dir, O, A, tRay, tLine);

                bool inHalfRange = isPlus ? (tLine >= 0.0f && tLine <= L)
                    : (tLine <= 0.0f && tLine >= -L);
                if (!inHalfRange) continue;

                glm::vec3 Pc = ray.origin + tRay * ray.dir;
                glm::vec3 Lc = O + tLine * A;
                float d = glm::length(Pc - Lc);
                if (d <= AXIS_GRAB_RADIUS && tRay < bestMetric) { bestMetric = tRay; best = h; }
            }
            else if (gh.axis == GizmoAxis::XYZ) {
                // Center cube: local AABB a little padded (unit cube scaled ~0.1 in ctor)
                const glm::vec3 bmin(-0.5f - CUBE_PAD, -0.5f - CUBE_PAD, -0.5f - CUBE_PAD);
                const glm::vec3 bmax(0.5f + CUBE_PAD, 0.5f + CUBE_PAD, 0.5f + CUBE_PAD);
                float t0, t1;
                if (rayAABBLocal(roL, rdL, bmin, bmax, t0, t1)) {
                    glm::vec3 worldHit = glm::vec3(Mchild * glm::vec4(roL + rdL * t0, 1));
                    float worldT = glm::length(worldHit - ray.origin);
                    if (worldT < bestMetric) { bestMetric = worldT; best = h; }
                }
            }
        }
    }

    return best;
}

void GizmoSystem::beginTransformEdit() {
    m_activeCmd.records.clear();
    m_editOpen = true;

    auto& R = m_scene.getRegistry();
    // Capture "before" for the whole selection
    for (auto eSel : R.view<SelectedComponent, TransformComponent>()) {
        TransformRecord rec;
        rec.e = eSel;
        rec.before = R.get<TransformComponent>(eSel);
        m_activeCmd.records.push_back(rec);
    }
}

void GizmoSystem::endTransformEdit() {
    if (!m_editOpen) return;
    m_editOpen = false;

    auto& R = m_scene.getRegistry();

    // Fill "after" and detect whether anything changed
    bool changed = false;
    for (auto& rec : m_activeCmd.records) {
        if (!R.valid(rec.e) || !R.any_of<TransformComponent>(rec.e)) continue;
        rec.after = R.get<TransformComponent>(rec.e);

        // Compare fields (use a small epsilon if you like)
        if (!changed) {
            changed = (rec.before.translation != rec.after.translation) ||
                (rec.before.rotation != rec.after.rotation) ||
                (rec.before.scale != rec.after.scale);
        }
    }

    if (!changed) {
        m_activeCmd.records.clear();
        return; // nothing to push
    }

    // Push to undo, clear redo
    m_undoStack.push_back(std::move(m_activeCmd));
    m_redoStack.clear();
    m_activeCmd.records.clear();
    if (onAfterCommandApplied) onAfterCommandApplied();
}

void GizmoSystem::applyCommand(const TransformCommand& cmd, bool toBefore)
{
    auto& R = m_scene.getRegistry();
    for (const auto& rec : cmd.records) {
        if (!R.valid(rec.e) || !R.any_of<TransformComponent>(rec.e)) continue;
        auto& t = R.get<TransformComponent>(rec.e);
        const TransformComponent& src = toBefore ? rec.before : rec.after;
        t = src;
        if (onTransformEdited) onTransformEdited(rec.e);
    }
    // >>> Ask UI to refresh gizmo + menus <<<
    if (onAfterCommandApplied) onAfterCommandApplied();
}

void GizmoSystem::undo()
{
    if (m_undoStack.empty()) return;
    TransformCommand cmd = m_undoStack.back(); m_undoStack.pop_back();
    applyCommand(cmd, /*toBefore*/ true);
    m_redoStack.push_back(std::move(cmd));
    // (applyCommand already called onAfterCommandApplied)
}

void GizmoSystem::redo()
{
    if (m_redoStack.empty()) return;
    TransformCommand cmd = m_redoStack.back(); m_redoStack.pop_back();
    applyCommand(cmd, /*toBefore*/ false);
    m_undoStack.push_back(std::move(cmd));
    // (applyCommand already called onAfterCommandApplied)
}