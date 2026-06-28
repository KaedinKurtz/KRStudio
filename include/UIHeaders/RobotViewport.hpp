#pragma once
// ===========================================================================
// ROBOT-ONLY VIEWPORT -- a focused authoring view that shows ONLY the robot in a
// flat grey "room" (no skybox/horizon), gently orbiting so features can be
// selected from all angles. Built on the PreviewViewport model (a self-owned
// Scene + RenderingSystem) because the shared RenderingSystem renders one shared
// scene and cannot isolate "robot only". It BINDS the live krs::rbuild::RobotGraph
// (the same graph the editing panel mutates) and mirrors its bodies into its own
// scene; the spin is a real base-axis camera transform (turntableCameraPos).
//
// Orbit control: a bottom-right slider sets the orbit speed (0 = stopped, which
// is acceptable). Manual navigation (orbit/pan/zoom drag) LOCKS the auto-orbit to
// 0 for the duration of the drag -- you cannot auto-orbit while transforming --
// and on release the orbit re-seeds from the current camera and resumes at the
// slider speed. On-screen rendering is OPERATOR-VISUAL-CONFIRM; the data binding +
// spin transform are gated.
// ===========================================================================
#include "ViewportWidget.hpp"
#include <memory>
#include <vector>
#include <utility>

class Scene;
class RenderingSystem;
class QTimer;
class QSlider;
class QLabel;
class QPushButton;

namespace krs::rbuild { struct RobotGraph; }

class RobotViewport : public ViewportWidget
{
    Q_OBJECT
public:
    explicit RobotViewport(Scene* mainScene, RenderingSystem* mainRender, QWidget* parent = nullptr);
    ~RobotViewport() override;

    // The live graph this view is bound to (the main scene's ctx graph), or nullptr.
    krs::rbuild::RobotGraph* liveGraph() const { return m_liveGraph; }

    // Auto-orbit angular speed (rad/s). 0 stops the orbit. Drives the slider too.
    float orbitSpeed() const { return m_orbitSpeed; }
    void  setOrbitSpeed(float radPerSec);

    // # of mirrored view bodies currently carrying the selection (cross-viewport check).
    int viewSelectedCount() const;

public slots:
    void refreshFromLive();   // re-bind to the live graph + re-mirror its bodies

protected:
    void initializeGL() override;
    void resizeEvent(QResizeEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private slots:
    void onSpinTick();
    void onOrbitSliderChanged(int value);
    void onModeToggled(bool builder);

private:
    void rebuildView();
    void buildOverlayControls();        // bottom-right orbit-speed slider
    void layoutOverlayControls();       // reposition on resize
    void reseedOrbitFromCamera();       // continue the orbit smoothly from the current view

    Scene*                            m_mainScene = nullptr;   // the editing scene (owns the live graph in ctx)
    RenderingSystem*                  m_mainRender = nullptr;  // main renderer to borrow baked IBL from
    krs::rbuild::RobotGraph*          m_liveGraph = nullptr;   // bound pointer (NOT a copy)
    std::unique_ptr<Scene>            m_viewScene;             // this view's own isolated scene
    std::unique_ptr<RenderingSystem>  m_viewRender;            // this view's own renderer

    // When a first-class Robot (e.g. the FANUC) exists, the view mirrors ITS real body
    // meshes (not the placeholder demo cubes). m_bodyMap pairs {mainEntity, viewEntity}
    // so the view bodies track the live pose (Mirror) + cross-viewport selection works.
    int   m_robotId = -1;            // mirrored robot's id (-1 = demo-graph fallback)
    std::vector<std::pair<entt::entity, entt::entity>> m_bodyMap;
    std::vector<TransformComponent> m_restXf;   // each view body's HOME (q0) transform (Builder mode)

    // Builder mode = robot frozen at the 0-degree HOME pose (for building/fixing joints).
    // Mirror mode  = view bodies track the live scene robot's joint angles.
    bool          m_builderMode = true;
    QPushButton*  m_modeToggle  = nullptr;

    QTimer*       m_spinTimer = nullptr;
    QElapsedTimer m_spinClock;
    float         m_spinAngle = 0.0f;
    glm::vec3     m_base{ 0.0f, 0.0f, 0.0f };
    float         m_dist = 2.5f;
    float         m_elev = 1.0f;

    // Orbit / manual-nav lock
    float         m_orbitSpeed = 0.5f;   // rad/s (slider-driven); the resume speed
    bool          m_manualNav  = false;  // true while a mouse button is held (drag)

    // Bottom-right overlay controls
    QSlider*      m_orbitSlider = nullptr;
    QLabel*       m_orbitLabel  = nullptr;
    static constexpr float kMaxOrbitSpeed = 1.5f;   // rad/s at slider max
};
