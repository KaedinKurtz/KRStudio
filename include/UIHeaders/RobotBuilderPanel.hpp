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
#include "IMenu.hpp"

class Scene;
class QListWidget;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;

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

signals:
    void graphChanged();   // emitted after every edit so MainWindow re-syncs viewport/sim

private slots:
    void onLoadDemo();
    void onDeleteJoint();
    void onDefineFromFeatures();
    void onJointSelected(int row);
    void onApplyLimit();

private:
    void initializeUI();
    void setupConnections();
    krs::rbuild::RobotGraph* graph() const;   // discover from Scene registry ctx (nullptr if none)
    void setStatus(const QString& msg);

    Scene* m_scene = nullptr;
    bool   m_isUpdatingUI = false;

    // controls (objectName set on each for the inventory/gate)
    QPushButton*    m_loadDemoBtn   = nullptr;
    QLabel*         m_dofLabel      = nullptr;
    QListWidget*    m_jointsList    = nullptr;
    QPushButton*    m_deleteBtn     = nullptr;
    QLabel*         m_defineHint    = nullptr;
    QPushButton*    m_defineBtn     = nullptr;
    QSpinBox*       m_dofIndex      = nullptr;
    QDoubleSpinBox* m_limitLo       = nullptr;
    QDoubleSpinBox* m_limitHi       = nullptr;
    QPushButton*    m_applyLimitBtn = nullptr;
    QLabel*         m_status        = nullptr;

    // Proven property hot-swap object (krs::rcfg). Rebuilt from graph->toRobot() on
    // refresh; onApplyLimit() invokes setPositionLimit() and reads back toJointLimits()
    // (the gate-proven "edit reflected live, no stale cache" path).
    std::unique_ptr<krs::rcfg::RobotConfig> m_cfg;
};
