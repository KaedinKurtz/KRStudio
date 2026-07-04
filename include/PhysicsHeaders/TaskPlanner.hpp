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

// Headless gate (KRS_TASKPLAN_SELFTEST): from goal at(cup, drop_pose) the planner emits
// [pick, place] IN ORDER by matching effects to preconditions, and the plan executes to Success
// under the SkillRuntime against a live demo robot; with the gripper JAMMED SHUT at start the
// planner REPLANS AROUND the fault -- [open_gripper, pick, place] -- and that too executes (the
// exact task P4's neg-ctrl honestly failed, now solved by planning); an already-satisfied goal
// yields an EMPTY plan. NEG-CTRLs: an unreachable goal fails with a reason (never hangs, node cap
// honored); an empty library fails loud.
bool runTaskPlanGate();

} // namespace krs::skill
