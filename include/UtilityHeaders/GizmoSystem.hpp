#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <functional>
#include "components.hpp" // For MaterialComponent, etc.

// Forward declarations for classes used in the header
class Scene;
class Camera;



enum class Frame {
    World, // Aligns with the world axes
    Body,  // Aligns with the selected object's local axes
    Parent // Aligns with the parent object's axes
};

/**
 * @enum GizmoMode
 * @brief Defines the current transformation operation of the gizmo.
 */
enum class GizmoMode {
    None,
    Translate,
    Rotate,
    Scale
};

/**
 * @enum GizmoAxis
 * @brief Defines the specific axis or plane a gizmo handle controls.
 */
enum class GizmoAxis {
    None,
    X, Y, Z,        // Single axes
    XY, YZ, XZ,     // Planes
    XYZ,            // Uniform (for scaling)
};

/**
 * @struct GizmoHandleComponent
 * @brief Attached to each part of the gizmo to identify its function.
 */
struct GizmoHandleComponent {
    GizmoMode mode;
    GizmoAxis axis;
};


struct TransformRecord {
    entt::entity       e{ entt::null };
    TransformComponent before{};
    TransformComponent after{};
};

struct TransformCommand {
    std::vector<TransformRecord> records;
};

/**
 * @class GizmoSystem
 * @brief Manages the creation, rendering, and interaction of the 3D transformation gizmo.
 *
 * This system creates and owns all the entities that make up the gizmo handles.
 * It's responsible for positioning the gizmo on the selected object, handling
 * mouse intersections, and translating user input into object transformations.
 */
class GizmoSystem
{
public:
    /**
     * @brief Constructs the GizmoSystem and creates all handle geometries.
     * @param scene A reference to the main scene.
     */
    GizmoSystem(Scene& scene);

    /**
     * @brief Updates the gizmo's state, primarily its position and visibility.
     * @param selectedEntity The currently selected entity in the scene.
     */
    void update(const QVector<entt::entity>& selectedEntities, const Camera& camera);

    /**
     * @brief Sets the active transformation mode (Translate, Rotate, Scale).
     * @param mode The new mode to activate.
     */
    void setMode(GizmoMode mode);

    /**
     * @brief Checks for an intersection between a mouse ray and the active gizmo handles.
     * @param ray The ray cast from the camera through the mouse cursor.
     * @return The entity of the handle that was hit, or entt::null if none.
     */
    entt::entity intersect(const CpuRay& ray);

    /**
     * @brief Begins a drag operation on a gizmo handle.
     * @param handleEntity The entity of the handle that was clicked.
     * @param selectedEntity The scene object being transformed.
     * @param hitPoint The world-space point where the ray intersected the handle.
     */
    void startDrag(entt::entity handleEntity,
        entt::entity selectedEntity,
        const glm::vec3& hitPoint,
        const glm::vec3& viewDir);

    /**
     * @brief Updates the transformation of the selected object based on mouse movement.
     * @param ray The current ray from the camera through the mouse cursor.
     * @param camera The active viewport camera.
     */
    void updateDrag(const CpuRay& ray, const Camera& camera);

    /**
     * @brief Ends the current drag operation and resets the gizmo's state.
     */
    void endDrag();

    // --- Getters ---
    bool isDragging() const { return m_isDragging; }
    GizmoMode getMode() const { return m_activeMode; }

    void onTranslateDoubleClick(GizmoAxis axis);
    void onScaleDoubleClick(GizmoAxis axis);

    entt::entity pickHandle(const CpuRay& ray);

    struct SnapSettings {
        int   rotateSegments = 0;     // 0 = off
        float translationStep = 0.0f;  // 0 = off
        float scaleStep = 0.0f;  // 0 = off
        bool  didStep = false; // for scale immediate jump
    };
    SnapSettings m_snap;

    void setSnapRotateSegments(int n) { m_snap.rotateSegments = std::max(0, n); }
    void setSnapTranslateStep(float d) { m_snap.translationStep = std::max(0.0f, d); }
    void setSnapScaleStep(float k) { m_snap.scaleStep = std::max(0.0f, k); m_snap.didStep = false; }

    entt::entity getRootEntity() const { return m_gizmoRoot; }
    void setHoveredHandle(entt::entity handle);

    std::function<void(entt::entity)> onTransformEdited;



    void setCenterScaleSensitivity(float g) { m_centerScaleSensitivity = g; }
    void setCenterScaleDistanceNormalize(bool e) { m_centerScaleDistanceNormalize = e; }

    void setAxisTranslateSensitivity(float g) { m_axisTranslateSensitivity = g; }
    void setAxisTranslateDistanceNormalize(bool e) { m_axisTranslateDistanceNormalize = e; }

    void beginTransformEdit();           // call at mouse-down
    void endTransformEdit();             // call at mouse-up (commit if changed)
    void undo();
    void redo();
    std::function<void()> onAfterCommandApplied;

private:
        // Uniform (center cube) scale sensitivity
    float m_centerScaleSensitivity = 1.0f;    // lower = less sensitive (try 0.1..0.4)
    bool  m_centerScaleDistanceNormalize = true; // normalize by camera distance
    /**
     * @brief Creates a single axis for a gizmo (e.g., the X-axis translation arrow).
     * @param mode The mode this handle belongs to (Translate, Rotate, Scale).
     * @param axis The axis (X, Y, Z).
     * @param color The base color for this axis.
     */
    void createAxisHandles(GizmoMode mode, GizmoAxis axis, const MaterialComponent& color);

    /**
     * @brief Updates the visual state of all handles based on the active one.
     * @param activeHandle The handle currently being hovered over or dragged.
     */
    void updateHighlights(entt::entity activeHandle);

    Scene& m_scene;

    // --- Gizmo State ---
    GizmoMode m_activeMode = static_cast<GizmoMode>(-1);
    Frame m_currentFrame = Frame::World;
    entt::entity m_gizmoRoot = entt::null;
    bool m_isVisible = false;

    // --- Drag State ---
    bool m_isDragging = false;
    entt::entity m_draggedHandle = entt::null;
    entt::entity m_selectedObject = entt::null;
    TransformComponent m_initialObjectTransform;
    glm::vec3 m_dragStartPoint;
    glm::vec3 m_dragAxis;

    glm::vec3 m_initialGizmoOrigin{ 0.0f };     // gizmo root position at mouse-down
    glm::vec3 m_initialPivotOffsetWorld{ 0.0f }; // object origin - pivot at mouse-down (for rotate about pivot)

    GizmoMode m_dragMode = GizmoMode::Translate; // current drag mode when dragging
    GizmoAxis m_dragAxisOrPlane = GizmoAxis::X;  // which axis/plane is active

    glm::vec3 m_dragAxisWorld{ 0.0f };    // active axis direction (world)
    glm::vec3 m_dragPlaneNormal{ 0.0f };  // active plane normal (world)
    glm::vec3 m_rotPlaneNormal{ 0.0f };   // same as axis for rotate

    glm::vec3 m_gizmoOriginWorld{ 0.0f }; // gizmo root position
    glm::vec3 m_v0{ 0.0f };               // rotate: start vector on rotation plane
    float     m_t0 = 0.0f;              // translate/scale: start param along axis

    // Screen-space axis-translate tuning
    float m_axisTranslateSensitivity = 10.0f;     // try 0.6..1.5
    bool  m_axisTranslateDistanceNormalize = true;

    // Screen-plane reference point captured at drag start (projected)
    glm::vec3 m_screenStart = glm::vec3(0.0f);
    bool      m_hasScreenStart = false;

    struct DragItem {
        entt::entity       e{ entt::null };
        TransformComponent t0{};            // initial local transform (copy at mouse-down)
        glm::vec3          pivotOffsetW{ 0 }; // (t0.translation - m_gizmoOriginWorld)
    };

    std::vector<DragItem> m_group;          // selection snapshot at mouse-down

	// --- UNDO/REDO ---

    std::vector<TransformCommand> m_undoStack;
    std::vector<TransformCommand> m_redoStack;
    TransformCommand              m_activeCmd;
    bool                          m_editOpen = false;

    void applyCommand(const TransformCommand& cmd, bool toBefore);

    // --- Handle Entity Storage ---
    std::vector<entt::entity> m_translateHandles;
    std::vector<entt::entity> m_rotateHandles;
    std::vector<entt::entity> m_scaleHandles;

    // --- Material Storage ---
    // You will initialize these with specific colors in the .cpp file
    MaterialComponent M_RED, M_GREEN, M_BLUE;
    MaterialComponent M_RED_GLOW, M_GREEN_GLOW, M_BLUE_GLOW;
    MaterialComponent M_TEAL, M_PURPLE, M_ORANGE; // For planes (Cyan, Magenta, Yellow)
    MaterialComponent M_TEAL_GLOW, M_PURPLE_GLOW, M_ORANGE_GLOW;
    MaterialComponent M_GREY;

public slots:
    void setFrameOfReference(Frame newFrame);

};
