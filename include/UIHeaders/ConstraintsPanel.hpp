#pragma once
// ===========================================================================
// CONSTRAINTS PANEL -- the kinematic-constraint authoring dock (Fusion-style).
//
// Workflow (one glance, no manual): (1) latch Pick A, click a face/edge in the
// viewport; (2) latch Pick B, click the second feature; (3) choose the
// constraint type (geometric relations + kinematic joints for passive
// linkages); (4) Apply -- body B snaps so the relation holds, and the
// constraint lands in the scene graph (icons hover at its anchors in the
// viewport; kinematic types become PhysX joints on Play so undriven linkages
// articulate when the robot pushes them).
//
// The list below shows every constraint with suppress/delete; selecting one
// highlights it. "Show icons" drives the in-scene overlay globally.
// ===========================================================================
#include <QWidget>
#include <entt/entt.hpp>
#include "SelectionService.hpp"   // krs::sel::Selection -- the two collected picks

class Scene;
class QComboBox;
class QListWidget;
class QPushButton;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QTimer;

class ConstraintsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ConstraintsPanel(Scene* scene, QWidget* parent = nullptr);

    // The in-scene icon overlay calls this to focus a constraint from a click.
    void focusConstraint(std::uint64_t id);

private slots:
    void onPickA(bool on);
    void onPickB(bool on);
    void onApply();
    void onDeleteSelected();
    void onSuppressToggled(bool on);
    void onTypeChanged(int idx);
    void onTick();               // poll picks + refresh the list on graph changes

private:
    void refreshList();
    void updatePickLabels();

    Scene* m_scene = nullptr;
    QComboBox*      m_type = nullptr;
    QPushButton*    m_pickABtn = nullptr;
    QPushButton*    m_pickBBtn = nullptr;
    QLabel*         m_pickALabel = nullptr;
    QLabel*         m_pickBLabel = nullptr;
    QDoubleSpinBox* m_offset = nullptr;   // Distance / axial offset (shown per type)
    QDoubleSpinBox* m_angle = nullptr;    // Angle (shown per type)
    QWidget*        m_offsetRow = nullptr;
    QWidget*        m_angleRow = nullptr;
    QPushButton*    m_applyBtn = nullptr;
    QListWidget*    m_list = nullptr;
    QCheckBox*      m_suppress = nullptr;
    QCheckBox*      m_showIcons = nullptr;
    QLabel*         m_status = nullptr;
    QTimer*         m_tick = nullptr;

    // the two collected picks (world Selections; anchors derive at Apply time)
    krs::sel::Selection m_selA, m_selB;
    bool m_haveA = false, m_haveB = false;
    int  m_armWhich = 0;         // 0 = none, 1 = collecting A, 2 = collecting B
    QString m_lastListSig;
};
