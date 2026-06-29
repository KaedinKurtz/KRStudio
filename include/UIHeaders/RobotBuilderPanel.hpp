#pragma once
// ===========================================================================
// ROBOT BUILDER PANEL -- the docked editing panel whose controls INVOKE the proven
// krs::rbuild edit ops (delete-joint, define-from-features, property hot-swap) on
// the live RobotGraph. It does NOT reimplement any op: every control routes to a
// krs::rbuild::EditController bound to the graph held in the Scene's registry ctx.
//
// Conforms to the established docked-panel pattern (see ObjectPropertiesWidget /
// PhysicsPropertiesWidget): QWidget + IMenu, (Scene*, parent) ctor, hand-coded
// widgets, all connections in setupConnections(), m_isUpdatingUI feedback guard,
// dark QSS. Joints are NOT entities, so instead of setEntity(entt::entity) the
// panel binds to the live graph + the feature SelectionState and exposes refresh().
// ===========================================================================
#include <QWidget>
#include <memory>
#include <cstdint>
#include "IMenu.hpp"

class Scene;
class QListWidget;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;

namespace krs::rbuild { struct RobotGraph; }
namespace krs::rcfg  { struct RobotConfig; }

class RobotBuilderPanel : public QWidget, public IMenu
{
    Q_OBJECT
public:
    explicit RobotBuilderPanel(Scene* scene, QWidget* parent = nullptr);
    ~RobotBuilderPanel() override;   // defined in .cpp (unique_ptr to incomplete RobotConfig)

    // --- IMenu ---
    void initializeFresh() override;
    void initializeFromDatabase() override;
    void shutdownAndSave() override;
    QWidget* widget() override { return this; }

public slots:
    void refresh();   // re-read the live graph into the controls (guarded)
    // Bind the panel to a first-class robot for editing. If that robot IS the one the
    // authoring graph represents (the demo), edits go through the full graph path
    // (define/delete/snap). Otherwise (the boot FANUC, which has no authoring graph) the
    // panel binds to its LiveRobot for joint-LIMIT editing -- limits don't move FK frames,
    // so they can never displace the arm. Wired from OutlinerWidget::robotSelected.
    void editRobot(int robotId);

signals:
    void graphChanged();   // emitted after every edit so MainWindow re-syncs viewport/sim

private slots:
    void onLoadDemo();
    void onDeleteJoint();
    void onDefineFromFeatures();
    void onClearSelection();   // clear the accumulated bore/feature selection set
    void onJointSelected(int row);
    void onApplyLimit();
    void onApplyAxisOrigin();   // move the selected joint's axis origin (where it snaps to)
    void onApplyAxisDir();      // set the selected joint's axis DIRECTION (the rotation/translation axis)
    void onSnapAxisToBore();    // snap the selected joint's axis to the selected bore feature
    void onJointTypeChanged(int comboIndex);   // re-type the selected joint (Revolute/Continuous/Prismatic/Fixed)

private:
    void initializeUI();
    void setupConnections();
    krs::rbuild::RobotGraph* graph() const;   // discover from Scene registry ctx (nullptr if none)
    void setStatus(const QString& msg);
    void showSelectedJointAxis(int row);      // spawn/refresh the selected joint's glowing axis bar (main viewport)
    bool refreshFromLiveRobot();   // when bound to a LiveRobot (m_editRobotId>=0): fill controls from its model; true if it handled refresh
    void onApplyLimitLive();       // write the edited limit straight into the bound LiveRobot's model + rebuild

    Scene* m_scene = nullptr;
    bool   m_isUpdatingUI = false;
    // A single glowing axis bar for the currently-selected joint, shown in the MAIN viewport
    // (JointAxisPass renders JointAxisComponent always-on-top). Editing the axis re-orients it ->
    // direct visible feedback even at the home pose. entt::entity stored as uint32 to keep the
    // header light (no entt include); resolved against m_scene's registry.
    std::uint32_t m_selAxisBar = 0xFFFFFFFFu;   // entt::null sentinel
    // >=0 when the panel is bound to a LiveRobot (e.g. the FANUC) for live limit editing;
    // -1 when authoring the ctx RobotGraph (the demo). Set by editRobot().
    int    m_editRobotId = -1;

    // controls (objectName set on each for the inventory/gate)
    QPushButton*    m_loadDemoBtn   = nullptr;
    QLabel*         m_dofLabel      = nullptr;
    QListWidget*    m_jointsList    = nullptr;
    QComboBox*      m_jointType     = nullptr;
    QPushButton*    m_deleteBtn     = nullptr;
    QLabel*         m_defineHint    = nullptr;
    QPushButton*    m_defineBtn     = nullptr;
    QPushButton*    m_clearSelBtn   = nullptr;
    QSpinBox*       m_dofIndex      = nullptr;
    QDoubleSpinBox* m_limitLo       = nullptr;
    QDoubleSpinBox* m_limitHi       = nullptr;
    QPushButton*    m_applyLimitBtn = nullptr;
    // Joint axis origin (where the joint snaps to) -- adjusts RBJoint.axisPos.
    QDoubleSpinBox* m_axisX         = nullptr;
    QDoubleSpinBox* m_axisY         = nullptr;
    QDoubleSpinBox* m_axisZ         = nullptr;
    QPushButton*    m_applyAxisBtn  = nullptr;
    QPushButton*    m_snapAxisBtn   = nullptr;
    // Joint axis DIRECTION (the actual rotation/translation axis) -- adjusts RBJoint.axisDir.
    QDoubleSpinBox* m_dirX          = nullptr;
    QDoubleSpinBox* m_dirY          = nullptr;
    QDoubleSpinBox* m_dirZ          = nullptr;
    QPushButton*    m_applyDirBtn   = nullptr;
    QLabel*         m_status        = nullptr;

    // Proven property hot-swap object (krs::rcfg). Rebuilt from graph->toRobot() on
    // refresh; onApplyLimit() invokes setPositionLimit() and reads back toJointLimits()
    // (the gate-proven "edit reflected live, no stale cache" path).
    std::unique_ptr<krs::rcfg::RobotConfig> m_cfg;
};
