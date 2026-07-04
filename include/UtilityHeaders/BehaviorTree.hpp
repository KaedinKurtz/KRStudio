#pragma once
// ===========================================================================
// krs::policy -- a headless, tick-based BEHAVIOR TREE: the reactive policy
// substrate for robot control ("move to A, wait for the sensor, if force>X
// then retract"). Pure C++ (no Qt/entt/GL) so it composes into either the
// control thread or a test harness and gates without a window.
//
// WHY a behavior tree (vs an FSM): the RUNNING status is first-class. A leaf
// that has not finished (an in-flight motion, a Wait) returns Running and is
// RE-ENTERED next tick from where it left off; a Sequence remembers which
// child is running so it does not restart already-succeeded prefix steps every
// tick. That resumability is the whole point -- long-running robot ops span
// many control ticks and must not be re-issued each frame.
//
// Ownership: composites/decorators own their children by unique_ptr; the Tree
// owns the root. Ticking is single-threaded and allocation-free after build
// (leaves may of course call into whatever the caller bound). dt is the
// wall-clock seconds elapsed since the previous tick -- timing decorators/
// leaves integrate it, so the tree is frame-rate independent.
// ===========================================================================
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace krs::policy {

// Tri-state tick result. Running == "not done, call me again next tick".
enum class Status { Running, Success, Failure };

// ---------------------------------------------------------------------------
// Blackboard: a string-keyed scratchpad shared across the whole tree so leaves
// can pass sensor readings + targets without hard-wiring to each other (a
// Condition reads "force", an Action writes "target.x", etc.). Three typed
// stores rather than a variant -- callers know the type at the key and it keeps
// the read/write API branch-free. Missing keys read as a caller-supplied
// default so a policy never faults on an unpublished sensor.
// ---------------------------------------------------------------------------
struct Vec3 { double x = 0.0, y = 0.0, z = 0.0; };

class Blackboard {
public:
    void  setNumber(const std::string& k, double v) { nums_[k] = v; }
    void  setBool  (const std::string& k, bool   v) { bools_[k] = v; }
    void  setVec3  (const std::string& k, const Vec3& v) { vecs_[k] = v; }

    double getNumber(const std::string& k, double def = 0.0) const {
        auto it = nums_.find(k); return it == nums_.end() ? def : it->second;
    }
    bool getBool(const std::string& k, bool def = false) const {
        auto it = bools_.find(k); return it == bools_.end() ? def : it->second;
    }
    Vec3 getVec3(const std::string& k, const Vec3& def = {}) const {
        auto it = vecs_.find(k); return it == vecs_.end() ? def : it->second;
    }

    bool hasNumber(const std::string& k) const { return nums_.count(k) != 0; }
    bool hasBool  (const std::string& k) const { return bools_.count(k) != 0; }
    bool hasVec3  (const std::string& k) const { return vecs_.count(k) != 0; }

private:
    std::unordered_map<std::string, double> nums_;
    std::unordered_map<std::string, bool>   bools_;
    std::unordered_map<std::string, Vec3>   vecs_;
};

// ---------------------------------------------------------------------------
// Node: the polymorphic base. tick() advances the node by one control step and
// returns its Status. reset() clears transient run-state (running child index,
// elapsed timers, retry counters) so a re-used subtree starts clean -- it is
// invoked by the framework when a node that had been Running/Success finishes,
// so the NEXT activation re-enters fresh rather than resuming stale state.
// ---------------------------------------------------------------------------
class Node {
public:
    virtual ~Node() = default;
    virtual Status tick(double dt, Blackboard& bb) = 0;
    virtual void   reset() {}
};
using NodePtr = std::unique_ptr<Node>;

// ===========================================================================
// Composites
// ===========================================================================

// Sequence: AND. Ticks children left-to-right; fail-fast (a child's Failure
// fails the whole Sequence immediately). Remembers the running child so on the
// next tick it resumes THERE, not from child 0 -- succeeded prefix steps are
// not re-ticked while a later child is Running. Returns Success only when every
// child has succeeded in order.
class Sequence : public Node {
public:
    void add(NodePtr c) { children_.push_back(std::move(c)); }
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    std::vector<NodePtr> children_;
    std::size_t cur_ = 0;   // index of the child currently in progress
};

// Selector (a.k.a. Fallback): OR. Ticks children left-to-right and returns the
// first non-Failure (Success or Running); only a child's Failure advances to
// the next. Remembers the running child (same resumption reason as Sequence).
// Returns Failure only when every child has failed.
class Selector : public Node {
public:
    void add(NodePtr c) { children_.push_back(std::move(c)); }
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    std::vector<NodePtr> children_;
    std::size_t cur_ = 0;
};

// Parallel: tick ALL children every step; succeed once >= successThreshold have
// succeeded (N-of-M), fail once so many have failed that the threshold can no
// longer be met, else Running. A child that has resolved is not re-ticked until
// the Parallel itself resolves and resets. Robots use this to run a motion and
// a guard concurrently ("drive to A while watching a force limit").
class Parallel : public Node {
public:
    explicit Parallel(int successThreshold) : need_(successThreshold) {}
    void add(NodePtr c) { children_.push_back(std::move(c)); done_.push_back(Status::Running); }
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    std::vector<NodePtr> children_;
    std::vector<Status>  done_;   // latched per-child result (Running == still live)
    int need_;                    // N in "N of M"
};

// ===========================================================================
// Decorators -- exactly one child, transform its result/timing.
// ===========================================================================

// Inverter: Success<->Failure, Running passes through unchanged.
class Inverter : public Node {
public:
    explicit Inverter(NodePtr c) : child_(std::move(c)) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override { child_->reset(); }
private:
    NodePtr child_;
};

// Repeat(n): re-run the child n times, succeeding after the n-th Success. Any
// child Failure fails immediately (it is a bounded loop, not a retry). n<=0
// means repeat forever (never returns Success on its own -- Running until the
// child fails). Between iterations the child is reset so each pass is fresh.
class Repeat : public Node {
public:
    explicit Repeat(NodePtr c, int n) : child_(std::move(c)), n_(n) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    NodePtr child_;
    int n_;
    int count_ = 0;   // completed successful iterations so far
};

// RetryUntilSuccess: swallow the child's Failure (reset + try again next tick)
// until it Succeeds. Optional attempt cap: <=0 means retry forever; a positive
// cap converts "exhausted all attempts" into Failure. The classic "keep trying
// to grasp until it sticks".
class RetryUntilSuccess : public Node {
public:
    explicit RetryUntilSuccess(NodePtr c, int maxAttempts = 0)
        : child_(std::move(c)), max_(maxAttempts) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    NodePtr child_;
    int max_;
    int attempts_ = 0;   // failed attempts consumed
};

// Timeout(seconds): give the child a deadline. While it is Running and the
// budget is not spent, pass Running through; if the child resolves in time,
// pass that result; if the wall-clock budget elapses first, force Failure
// (a stuck motion must not hang the policy forever).
class Timeout : public Node {
public:
    explicit Timeout(NodePtr c, double seconds) : child_(std::move(c)), budget_(seconds) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    NodePtr child_;
    double budget_;
    double elapsed_ = 0.0;
};

// Cooldown(seconds): rate-limit re-triggering. On the child's Success, start a
// lockout; subsequent ticks return Failure until the cooldown expires, then the
// child is allowed to run again. Prevents a Selector from re-firing an
// expensive/oscillating action every frame.
class Cooldown : public Node {
public:
    explicit Cooldown(NodePtr c, double seconds) : child_(std::move(c)), cd_(seconds) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override;
private:
    NodePtr child_;
    double cd_;
    double remaining_ = 0.0;   // >0 == locked out
};

// ===========================================================================
// Leaves -- where the policy touches the world (all caller-bound).
// ===========================================================================

// Condition: instantaneous predicate over the blackboard -> Success/Failure
// (never Running). Gates actions ("is the sensor ready?", "force < limit?").
class Condition : public Node {
public:
    using Pred = std::function<bool(Blackboard&)>;
    explicit Condition(Pred p) : pred_(std::move(p)) {}
    Status tick(double dt, Blackboard& bb) override;
private:
    Pred pred_;
};

// Action: the caller binds a real robot op. The functor gets (dt, blackboard)
// and returns Status directly, so a multi-tick motion returns Running until it
// completes -- this is how "move to A" spans control frames.
class Action : public Node {
public:
    using Fn = std::function<Status(double, Blackboard&)>;
    explicit Action(Fn f) : fn_(std::move(f)) {}
    Status tick(double dt, Blackboard& bb) override;
private:
    Fn fn_;
};

// Wait(seconds): return Running until `seconds` of accumulated dt have passed,
// then Success. A pure timed delay ("settle for 0.1s before reading force").
class Wait : public Node {
public:
    explicit Wait(double seconds) : dur_(seconds) {}
    Status tick(double dt, Blackboard& bb) override;
    void   reset() override { elapsed_ = 0.0; }
private:
    double dur_;
    double elapsed_ = 0.0;
};

// SetBlackboard: write a number into the blackboard and Succeed (side-effect
// leaf -- publish a target, latch a flag, seed a counter). One-shot per tick.
class SetBlackboard : public Node {
public:
    SetBlackboard(std::string key, double value)
        : key_(std::move(key)), value_(value) {}
    Status tick(double dt, Blackboard& bb) override;
private:
    std::string key_;
    double value_;
};

// ===========================================================================
// Tree: owns the root + the shared blackboard. tick(dt) advances the policy one
// control step. On a completed (Success/Failure) root it resets the root so the
// next tick re-evaluates from scratch (a reactive policy re-runs continuously).
// ===========================================================================
class Tree {
public:
    Tree() = default;
    explicit Tree(NodePtr root) : root_(std::move(root)) {}
    void setRoot(NodePtr root) { root_ = std::move(root); }

    Blackboard&       blackboard()       { return bb_; }
    const Blackboard& blackboard() const { return bb_; }

    Status tick(double dt);
    void   reset() { if (root_) root_->reset(); }

private:
    NodePtr    root_;
    Blackboard bb_;
};

// GATE BEHAVIORTREE -- exercises Sequence/Selector ordering + RUNNING semantics,
// timed Wait, Condition-gated Action, Inverter, a realistic settle-then-check
// composite, and the always-Failure NEG-CTRLs. Declared here; defined in the
// .cpp. The main loop wires KRS_BEHAVIORTREE_SELFTEST -> this -> _Exit.
bool runBehaviorTreeGate();

} // namespace krs::policy
