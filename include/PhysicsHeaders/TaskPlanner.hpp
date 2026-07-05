#pragma once
// ===========================================================================
// TASK PLANNER -- thin PDDL-lite forward search over skills (krs::skill), P5.
//
// Given a LIBRARY of grounded SkillSteps (a SkillSpec + bound params), a start
// WorldState, and a GOAL (a set of typed Predicates), forward-chain: expand the
// steps whose preconditions hold in the simulated state, assert their effects,
// breadth-first until every goal predicate holds. The result is an ordered
// std::vector<SkillStep> ready for validateSequence + composeSequence + the
// SkillRuntime -- i.e. a goal in, an executable closed-loop task out.
//
// SYMBOLIC OVERLAY: effects are asserted as STRIPS-style facts layered OVER the
// observed WorldState (assertedTrue/assertedFalse by canonical predicate text),
// so a simulated "place" makes at(cup, drop_pose) hold downstream without
// mutating observed poses. Deliberately thin: grounded steps only (no variable
// binding -- the library enumerates its bindings), BFS with a node cap, first
// (shallowest) plan wins. HTN/cost search stays future work.
// ===========================================================================
#include "Skill.hpp"
#include <string>
#include <vector>

namespace krs::world { struct WorldState; }

namespace krs::skill {

struct PlanResult {
    bool ok = false;
    std::vector<SkillStep> steps;   // ordered; feed to validateSequence/composeSequence
    std::string why;                // when !ok: unmet goal / cap hit / empty library
    int nodesExpanded = 0;
};

// Forward-search a plan reaching `goal` from `start` using the grounded `library`.
// maxDepth caps plan length; maxNodes caps the search (honest failure, never a hang).
PlanResult planTask(const std::vector<SkillStep>& library, const krs::world::WorldState& start,
                    const std::vector<Predicate>& goal, int maxDepth = 8, int maxNodes = 20000);

// ---- Goal Workspace G2/G3: goal REGRESSION with epistemic knowledge gaps -----------------------
// A parameter the plan needs but the knowledge ledger cannot yet supply at the demanded confidence.
struct KnowledgeGap {
    std::string object, param;
    double sigmaNow = 1e9, sigmaNeeded = 0.0;
    std::string suggestedSkill;      // an excitation routine that hones this parameter ("" = none known)
    bool resolvedByPlan = false;     // true when an excitation step was auto-inserted for it
};
struct RegressionResult {
    bool ok = false;
    std::vector<SkillStep> steps;    // ordered, envelopes STAMPED per step (auditable bounds)
    std::vector<KnowledgeGap> gaps;  // the knowledge to-do (empty = fully executable now)
    std::string why;
    int iterations = 0;
};
// BACKWARD-chain from the goal: pick an unmet predicate, find the skill whose EFFECTS establish it,
// prepend it, adopt its PRECONDITIONS as new subgoals; repeat until everything grounds in `start`.
// Regression yields the RELEVANCE chain: only objects the plan actually touches get knowledge
// demands. For each chosen step, unsatisfied SkillSpec::needs[] (per the object's R2S/SimOnly tag)
// emit KnowledgeGaps; when `autoInsertExcitation` an epistemic skill honing that parameter is
// inserted before the needing step (gap marked resolvedByPlan) -- otherwise gaps go to the to-do.
// Envelopes are composed (traits x intrinsic, intersection) and stamped on every step.
RegressionResult planBackward(const std::vector<SkillStep>& library, const krs::world::WorldState& start,
                              const std::vector<Predicate>& goal, bool autoInsertExcitation = false,
                              int maxIterations = 64);

// Headless STORY gate (KRS_GOALLOOP_SELFTEST) -- the whole Goal Workspace loop end to end, with the
// primitive bodies loaded FROM the shipped assets/skills/*.knode files: a .kgoal goal on an R2S
// liquid glass with UNKNOWN mass -> regression plans [grasp, place] + auto-splices lift_weigh ->
// execution under the SkillRuntime runs the honing excitation (population converges, mass becomes
// Measured), then the guarded motion steps drive the live robot -> the goal is satisfied. The scene
// SAVES and a fresh load re-plans with ZERO gaps (yesterday's glass stays known); a residual-spike
// invalidation makes the gap honestly reappear. NEG-CTRL included via the invalidation re-check.
bool runGoalLoopGate();

// Headless gate (KRS_GOALPLAN_SELFTEST): regression reproduces [pick, place] backward from
// at(cup, drop_pose); an R2S glass with unknown mass emits gap(mass) while the SAME plan on a
// SimOnly object emits none (the tag gates extraction); autoInsertExcitation splices lift_weigh
// before the needing step; the liquid-container trait stamps maxTiltDeg=15 onto the carry step's
// envelope (intersection beats the primitive's looser bound) and the runtime guard FAILS the step
// when the glass tilts past it mid-run; NEG-CTRLs: an unestablishable goal fails bounded with a
// reason; an irrelevant object's parameters are NEVER demanded (relevance chain).
bool runGoalPlanGate();

// Headless gate (KRS_TASKPLAN_SELFTEST): from goal at(cup, drop_pose) the planner emits
// [pick, place] IN ORDER by matching effects to preconditions, and the plan executes to Success
// under the SkillRuntime against a live demo robot; with the gripper JAMMED SHUT at start the
// planner REPLANS AROUND the fault -- [open_gripper, pick, place] -- and that too executes (the
// exact task P4's neg-ctrl honestly failed, now solved by planning); an already-satisfied goal
// yields an EMPTY plan. NEG-CTRLs: an unreachable goal fails with a reason (never hangs, node cap
// honored); an empty library fails loud.
bool runTaskPlanGate();

} // namespace krs::skill
