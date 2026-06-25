#pragma once
// ===========================================================================
// ROBOT-ONLY VIEWPORT -- a focused authoring view that shows ONLY the robot,
// slowly spinning around its base axis, so features can be selected from all
// angles. Built on the PreviewViewport model (a self-owned Scene + RenderingSystem)
// because the shared RenderingSystem renders one shared scene and cannot isolate
// "robot only". It BINDS the live krs::rbuild::RobotGraph (the same graph the
// editing panel mutates) and mirrors its bodies into its own scene; the spin is a
// real base-axis camera transform (turntableCameraPos). On-screen rendering is
// OPERATOR-VISUAL-CONFIRM; the data binding + spin transform are gated.
// ===========================================================================
#include "ViewportWidget.hpp"
#include <memory>

class Scene;
class RenderingSystem;
class QTimer;

namespace krs::rbuild { struct RobotGraph; }

class RobotViewport : public ViewportWidget
{
    Q_OBJECT
public:
    explicit RobotViewport(Scene* mainScene, QWidget* parent = nullptr);
    ~RobotViewport() override;

    // The live graph this view is bound to (the main scene's ctx graph), or nullptr.
    krs::rbuild::RobotGraph* liveGraph() const { return m_liveGraph; }

public slots:
    void refreshFromLive();   // re-bind to the live graph + re-mirror its bodies

protected:
    void initializeGL() override;

private slots:
    void onSpinTick();

private:
    void rebuildView();

    Scene*                            m_mainScene = nullptr;   // the editing scene (owns the live graph in ctx)
    krs::rbuild::RobotGraph*          m_liveGraph = nullptr;   // bound pointer (NOT a copy)
    std::unique_ptr<Scene>            m_viewScene;             // this view's own isolated scene
    std::unique_ptr<RenderingSystem>  m_viewRender;            // this view's own renderer

    QTimer*       m_spinTimer = nullptr;
    QElapsedTimer m_spinClock;
    float         m_spinAngle = 0.0f;
    glm::vec3     m_base{ 0.0f, 0.0f, 0.0f };
    float         m_dist = 2.5f;
    float         m_elev = 1.0f;
};
