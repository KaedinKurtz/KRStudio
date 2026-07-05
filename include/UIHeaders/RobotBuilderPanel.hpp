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
#include <vector>
#include <cstdint>
#include "IMenu.hpp"

class Scene;
class QListWidget;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QSlider;
class QTimer;

namespace krs::rbuild { struct RobotGraph; }
namespace krs::rcfg  { struct RobotConfig; }

struct EditSnapshot;   // cpp-only: graph + live-robot state captured before a mutating edit

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

public:
    int selectedJointRow() const;   // current joints-list row (-1 = none); for drop-to-apply actuator

public slots:
    void refresh();   // re-read the live graph into the controls (guarded)
    // Bind the panel to a first-class robot for editing. If that robot IS the one the
    // authoring graph represents, edit it directly; otherwise synthesize an editable
    // graph mirroring its LiveRobot (buildGraphFromLiveRobot) so define/delete/re-type/
    // limits all work on ANY robot. Wired from OutlinerWidget::robotSelected.
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
    void onJogMoved(int sliderValue);          // jog the selected joint's DOF through LiveRobot::q
    void onUndoEdit();                         // restore the last pre-edit snapshot (graph + pose + transforms)
    void onGenerateClearanceLimits();          // self-intersection -> joint limits for a surface clearance

private:
    void initializeUI();
    void setupConnections();
    krs::rbuild::RobotGraph* graph() const;   // discover from Scene registry ctx (nullptr if none)
    void setStatus(const QString& msg);
    void showSelectedJointAxis(int row);      // spawn/refresh the selected joint's glowing axis bar (main viewport)
    void refreshBoreSlots();                  // Bore A/B readout + Define/Snap enable gating (selection poll)
    int  dofIndexOfRow(int row) const;        // joints-list row -> chain DOF index (-1 = no DOF: Fixed/ambiguous)
    void syncJogToSelected(int row);          // point the jog slider at the selected joint's DOF + current q
    void pushUndo(const QString& label);      // snapshot graph + robot state BEFORE a mutating edit

    Scene* m_scene = nullptr;
    bool   m_isUpdatingUI = false;
    // A single glowing axis bar for the currently-selected joint, shown in the MAIN viewport
    // (JointAxisPass renders JointAxisComponent always-on-top). Editing the axis re-orients it ->
    // direct visible feedback even at the home pose. entt::entity stored as uint32 to keep the
    // header light (no entt include); resolved against m_scene's registry.
    std::uint32_t m_selAxisBar = 0xFFFFFFFFu;   // entt::null sentinel

    // controls (objectName set on each for the inventory/gate)
    QPushButton*    m_loadDemoBtn   = nullptr;
    QLabel*         m_dofLabel      = nullptr;
    QListWidget*    m_jointsList    = nullptr;
    QComboBox*      m_jointType     = nullptr;
    QPushButton*    m_deleteBtn     = nullptr;
    QPushButton*    m_undoBtn       = nullptr;   // Undo Last Edit (Ctrl+Z inside the panel)
    // Pre-edit snapshots (graph + q + rest captures + entity transforms), newest last. Cut/split is
    // NOT snapshotted (it creates robots; confirm-guarded instead). unique_ptr keeps the header
    // light -- EditSnapshot is defined in the .cpp (needs Eigen + components).
    std::vector<std::unique_ptr<EditSnapshot>> m_undoStack;
    QLabel*         m_defineHint    = nullptr;
    QLabel*         m_boreA         = nullptr;   // Bore A slot readout (body/radius of the older pick)
    QLabel*         m_boreB         = nullptr;   // Bore B slot readout (the newer pick)
    QComboBox*      m_alignCombo    = nullptr;   // Define alignment: Auto / Align axes / Oppose axes
    QPushButton*    m_chooseBoresBtn = nullptr;  // checkable: arms bore picking (quota 2, auto-disarms)
    QPushButton*    m_defineBtn     = nullptr;
    QPushButton*    m_clearSelBtn   = nullptr;
    QTimer*         m_selPoll       = nullptr;   // polls SelectionState -> bore slots + enable gating
    QSlider*        m_jogSlider     = nullptr;   // jog the selected joint through LiveRobot::q
    QLabel*         m_jogLabel      = nullptr;   // live q readout for the jogged joint
    QSpinBox*       m_dofIndex      = nullptr;
    QDoubleSpinBox* m_limitLo       = nullptr;
    QDoubleSpinBox* m_limitHi       = nullptr;
    QPushButton*    m_applyLimitBtn = nullptr;
    QDoubleSpinBox* m_clearanceSpin = nullptr;   // surface clearance (m) for limit discovery
    QPushButton*    m_genLimitsBtn  = nullptr;   // Generate Joint Limits from self-collision
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
