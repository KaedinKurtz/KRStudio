#pragma once
// ===========================================================================
// GOAL WORKSPACE -- the goal-definition dock (Goal Workspace G5, first pass).
//
// Three zones: LEFT the live world browser (WorldState objects with pose,
// SimOnly/R2S tag, traits, and knowledge badges -- parameter value +- sigma
// colored by provenance); CENTER the goal stack (predicate cards built from
// combos, each showing LIVE truth against the current world, saved/loaded as a
// .kgoal -- the same document the GoalBuilder API writes); RIGHT plan &
// knowledge ([Plan] -> the regressed steps with stamped envelopes + the
// KNOWLEDGE TO-DO of gaps with suggested excitations, [Execute] -> the ctx
// SkillRuntime that MainWindow's eval loop pumps, so the robot moves LIVE).
//
// First pass: the built-in skill library binds grasp/place (bodies from
// assets/skills/*.knode) + the lift_weigh excitation to the goal's object;
// grasp/place target configs are placeholder joint poses until grasp-pose
// synthesis lands. Eyes-on surface -- the machinery underneath is gated
// (KGOAL/GOALPLAN/HONE/GOALLOOP).
// ===========================================================================
#include <QWidget>
#include "GoalDoc.hpp"
#include "TaskPlanner.hpp"

class Scene;
class QTreeWidget;
class QListWidget;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QTimer;

class GoalWorkspacePanel : public QWidget
{
    Q_OBJECT
public:
    explicit GoalWorkspacePanel(Scene* scene, QWidget* parent = nullptr);

private slots:
    void onRefreshWorld();     // rebuild the world browser from WorldState + knowledge
    void onAddPredicate();     // append the combo-built predicate to the goal stack
    void onRemovePredicate();  // remove the selected predicate card
    void onToggleTag();        // flip the selected object SimOnly <-> R2S
    void onAddTrait();         // add the selected trait to the selected object
    void onSaveGoal();         // Save Goal... (.kgoal)
    void onLoadGoal();         // Load Goal... (.kgoal)
    void onPlan();             // planBackward -> steps + knowledge to-do
    void onExecute();          // start the planned task on the ctx SkillRuntime
    void onCancel();           // cancel the running task (DOFs release next pass)
    void onLiveTick();         // ~5 Hz: live truth ticks on goal cards + runtime status

private:
    void rebuildGoalList();    // repaint the goal stack (rows + live truth)
    std::vector<krs::skill::SkillStep> buildLibraryFor(const std::string& object);

    Scene* m_scene = nullptr;
    krs::goal::GoalDoc m_goal;                       // the document being edited
    krs::skill::RegressionResult m_plan;             // the last [Plan] result
    // the specs the current plan references (kept alive while a plan/execution exists)
    std::vector<std::unique_ptr<krs::skill::SkillSpec>> m_specs;
    int m_taskId = -1;                               // the running SkillRuntime task ("-1" = none)

    QTreeWidget* m_world = nullptr;
    QListWidget* m_goalList = nullptr;
    QListWidget* m_planList = nullptr;
    QListWidget* m_todoList = nullptr;
    QComboBox *m_kind = nullptr, *m_objA = nullptr, *m_objB = nullptr, *m_traitPick = nullptr;
    QDoubleSpinBox* m_tol = nullptr;
    QPushButton *m_planBtn = nullptr, *m_execBtn = nullptr, *m_cancelBtn = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_liveTimer = nullptr;
};
