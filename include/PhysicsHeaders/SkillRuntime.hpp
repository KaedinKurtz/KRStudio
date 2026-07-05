#pragma once
// ===========================================================================
// SKILL RUNTIME -- the live closed-loop executor (krs::skill), Process & Skills P3.
//
// Owns the ACTIVE behavior trees (tasks built from skill leaves) and ticks them
// on the app's eval pass -- the first time the policy tower runs LIVE instead of
// only inside _Exit self-test gates. The per-pass contract does the safety work:
// each tick a running skill re-asserts its command-bus entries; a finished,
// cancelled, or deleted task simply STOPS re-asserting, so its DOFs release to
// manual control on the very next pass (no latched commands).
//
// CLOSED LOOP: monitoring is the skill's postcondition (evaluated against the
// live robot + WorldState every tick); retry/replan is the BT vocabulary the
// task is built from (Timeout -> honest Failure, RetryUntilSuccess -> re-attempt,
// Selector -> fallback). The runtime adds lifecycle: start/cancel/status + the
// tick pump, and keeps each task's Blackboard alive across ticks.
// ===========================================================================
#include "BehaviorTree.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace krs::skill {

class SkillRuntime {
public:
    // Start a task: the tree ticks every pump until it resolves (Success/Failure). Returns a handle.
    int start(std::string name, krs::policy::NodePtr root);
    // Convenience: one skill leaf under a Timeout (the canonical single-skill task).
    int startSkill(std::string name, krs::policy::Action::Fn leaf, double timeoutSec);

    // Pump every active task by dt. Resolved tasks retire (their last status is kept queryable);
    // they stop being ticked, so their skills stop re-asserting and the DOFs release next pass.
    void tick(double dt);

    // Running / Success / Failure; an unknown handle reports Failure (honest: it will never succeed).
    krs::policy::Status status(int id) const;
    bool anyRunning() const;
    int  runningCount() const;
    // Cancel: drop a running task NOW. Its skills stop re-asserting -> DOFs release next pass.
    void cancel(int id);
    // E-STOP path: cancel EVERY running task (the ribbon emergency stop). Same release contract.
    void cancelAll();

private:
    struct Task {
        int id = 0;
        std::string name;
        krs::policy::NodePtr root;
        krs::policy::Blackboard bb;
        krs::policy::Status last = krs::policy::Status::Running;
        bool active = true;
    };
    std::vector<std::unique_ptr<Task>> tasks_;   // unique_ptr: Task holds a NodePtr (non-copyable)
    int nextId_ = 1;
};

// ctx singleton accessor (get-or-emplace on the scene registry) -- the instance MainWindow pumps.
SkillRuntime& skillRuntime(entt::registry& reg);

// Headless gate (KRS_SKILLRUNTIME_SELFTEST): the runtime pumps a task under the REAL per-pass
// lifecycle (clear -> tick -> drain) against a live demo robot; a WorldState-gated postcondition
// makes the first attempt(s) time out (the world is not ready), RetryUntilSuccess re-attempts, and
// the task converges to Success once the world catches up -- retries observably > 0, robot at
// target (true closed-loop: monitor -> honest Failure -> retry -> Success). cancel() releases a
// never-succeeding task's DOFs on the next pass. NEG-CTRL: with retry capped at 1 attempt the same
// task ends Failure (the recovery genuinely came from the retry loop, not from slack elsewhere).
bool runSkillRuntimeGate();

} // namespace krs::skill
