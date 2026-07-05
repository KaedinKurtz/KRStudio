#pragma once
// ===========================================================================
// GOAL WORKSPACE -- the goal-definition dock (Goal Workspace G5, UX pass 2).
//
// A GUIDED, numbered flow instead of the first-pass three-pane debug surface:
//   (1) Scene objects -- the live WorldState browser (SimOnly/R2S tag, traits,
//       learned parameters value +- sigma colored by provenance) with plain-
//       English explainers;
//   (2) Target locations -- NAME a task frame (the missing piece that made
//       at()-goals impossible to author): at the selected object's pose or at
//       typed XYZ;
//   (3) The goal -- a sentence-style condition composer (only the fields the
//       chosen condition uses are shown) + the live ✓/✗ checklist, saved and
//       loaded as .kgoal (the same document the GoalBuilder API writes);
//   (4) Plan & execute -- [Plan] regresses the goal into ordered steps with
//       stamped envelopes + the KNOWLEDGE TO-DO of gaps (auto-inserted
//       excitations), [Execute] starts it on the ctx SkillRuntime that
//       MainWindow's eval loop pumps, so the robot moves LIVE.
//
// The machinery underneath is gated (KGOAL/GOALPLAN/HONE/GOALLOOP); this
// widget is the eyes-on surface.
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
class QLineEdit;
class QTimer;

class GoalWorkspacePanel : public QWidget
{
    Q_OBJECT
public:
    explicit GoalWorkspacePanel(Scene* scene, QWidget* parent = nullptr);

private slots:
    void onRefreshWorld();     // rebuild the world browser from WorldState + knowledge
    void onAddPredicate();     // append the composed condition to the goal checklist
    void onRemovePredicate();  // remove the selected condition
    void onToggleTag();        // flip the selected object SimOnly <-> R2S
    void onAddTrait();         // add the selected trait to the selected object
    void onSaveGoal();         // Save Goal... (.kgoal)
    void onLoadGoal();         // Load Goal... (.kgoal)
    void onPlan();             // planBackward -> steps + knowledge to-do
    void onExecute();          // start the planned task on the ctx SkillRuntime
    void onCancel();           // cancel the running task (DOFs release next pass)
    void onLiveTick();         // ~5 Hz: live truth ticks on goal cards + runtime status
    void onKindChanged(int);   // show ONLY the fields the chosen condition kind uses
    void onSetFrameAtObject(); // name a task frame at the selected world object's pose
    void onSetFrameAtXyz();    // name a task frame at typed coordinates

private:
    void rebuildGoalList();    // repaint the goal checklist (rows + live truth + empty hint)
    void refreshFrames();      // repaint the frame list + the target combo
    std::vector<krs::skill::SkillStep> buildLibraryFor(const std::string& object);

    Scene* m_scene = nullptr;
    krs::goal::GoalDoc m_goal;                       // the document being edited
    krs::skill::RegressionResult m_plan;             // the last [Plan] result
    // the specs the current plan references (kept alive while a plan/execution exists)
    std::vector<std::unique_ptr<krs::skill::SkillSpec>> m_specs;
    int m_taskId = -1;                               // the running SkillRuntime task ("-1" = none)

    // step 1: world
    QTreeWidget* m_world = nullptr;
    QComboBox*   m_traitPick = nullptr;
    // step 2: frames
    QListWidget* m_frameList = nullptr;
    QLineEdit*   m_frameName = nullptr;
    QDoubleSpinBox *m_fx = nullptr, *m_fy = nullptr, *m_fz = nullptr;
    // step 3: goal
    QListWidget* m_goalList = nullptr;
    QLabel*      m_goalEmpty = nullptr;              // empty-state hint (visible when no conditions)
    QComboBox *m_kind = nullptr, *m_objA = nullptr, *m_objB = nullptr;
    QWidget *m_rowA = nullptr, *m_rowB = nullptr, *m_rowTol = nullptr;   // contextual composer rows
    QDoubleSpinBox* m_tol = nullptr;
    // step 4: plan & execute
    QListWidget* m_planList = nullptr;
    QListWidget* m_todoList = nullptr;
    QPushButton *m_planBtn = nullptr, *m_execBtn = nullptr, *m_cancelBtn = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_liveTimer = nullptr;
};
