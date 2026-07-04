// ===========================================================================
// krs::policy -- behavior-tree runtime + self-test gate. See BehaviorTree.hpp
// for the design rationale (RUNNING resumability is the load-bearing property).
// ===========================================================================
#include "UtilityHeaders/BehaviorTree.hpp"

#include <cstdio>

namespace krs::policy {

// ---------------------------------------------------------------------------
// Composites
// ---------------------------------------------------------------------------

Status Sequence::tick(double dt, Blackboard& bb) {
    if (children_.empty()) return Status::Success;   // vacuous AND == true
    // Resume at the remembered child; earlier children already succeeded this
    // activation and are NOT re-ticked while a later one is Running.
    for (; cur_ < children_.size(); ++cur_) {
        Status s = children_[cur_]->tick(dt, bb);
        if (s == Status::Running) return Status::Running;      // hold position
        if (s == Status::Failure) { reset(); return Status::Failure; } // fail-fast
        // Success -> fall through to the next child on this same tick.
    }
    reset();                 // all succeeded -> ready for a fresh run next time
    return Status::Success;
}

void Sequence::reset() {
    cur_ = 0;
    for (auto& c : children_) c->reset();
}

Status Selector::tick(double dt, Blackboard& bb) {
    if (children_.empty()) return Status::Failure;   // vacuous OR == false
    for (; cur_ < children_.size(); ++cur_) {
        Status s = children_[cur_]->tick(dt, bb);
        if (s == Status::Running) return Status::Running;      // this branch owns the tick
        if (s == Status::Success) { reset(); return Status::Success; } // first winner; skip rest
        // Failure -> advance to the next candidate on this same tick.
    }
    reset();                 // every branch failed
    return Status::Failure;
}

void Selector::reset() {
    cur_ = 0;
    for (auto& c : children_) c->reset();
}

Status Parallel::tick(double dt, Blackboard& bb) {
    const int m = static_cast<int>(children_.size());
    if (m == 0) return Status::Success;
    int succ = 0, fail = 0;
    for (int i = 0; i < m; ++i) {
        if (done_[i] == Status::Running)          // only re-tick still-live children
            done_[i] = children_[i]->tick(dt, bb);
        if (done_[i] == Status::Success) ++succ;
        else if (done_[i] == Status::Failure) ++fail;
    }
    if (succ >= need_) { reset(); return Status::Success; }
    // If even every remaining live child succeeding cannot reach the threshold,
    // the outcome is already decided -> fail now rather than spin.
    if (m - fail < need_) { reset(); return Status::Failure; }
    return Status::Running;
}

void Parallel::reset() {
    for (auto& c : children_) c->reset();
    for (auto& s : done_) s = Status::Running;
}

// ---------------------------------------------------------------------------
// Decorators
// ---------------------------------------------------------------------------

Status Inverter::tick(double dt, Blackboard& bb) {
    switch (child_->tick(dt, bb)) {
        case Status::Success: return Status::Failure;
        case Status::Failure: return Status::Success;
        default:              return Status::Running;   // Running is not invertible
    }
}

Status Repeat::tick(double dt, Blackboard& bb) {
    for (;;) {
        Status s = child_->tick(dt, bb);
        if (s == Status::Running) return Status::Running;
        if (s == Status::Failure) { reset(); return Status::Failure; }
        // Success -> one iteration done.
        ++count_;
        if (n_ > 0 && count_ >= n_) { reset(); return Status::Success; }
        child_->reset();     // fresh child for the next iteration (this tick, if instantaneous)
    }
}

void Repeat::reset() { count_ = 0; child_->reset(); }

Status RetryUntilSuccess::tick(double dt, Blackboard& bb) {
    for (;;) {
        Status s = child_->tick(dt, bb);
        if (s == Status::Running) return Status::Running;
        if (s == Status::Success) { reset(); return Status::Success; }
        // Failure -> consume an attempt and (maybe) retry.
        ++attempts_;
        if (max_ > 0 && attempts_ >= max_) { reset(); return Status::Failure; }
        child_->reset();     // retry from a clean slate
    }
}

void RetryUntilSuccess::reset() { attempts_ = 0; child_->reset(); }

Status Timeout::tick(double dt, Blackboard& bb) {
    elapsed_ += dt;
    if (elapsed_ >= budget_) { reset(); return Status::Failure; }  // deadline blown
    Status s = child_->tick(dt, bb);
    if (s == Status::Running) return Status::Running;
    reset();                 // child resolved in time -> pass its result through
    return s;
}

void Timeout::reset() { elapsed_ = 0.0; child_->reset(); }

Status Cooldown::tick(double dt, Blackboard& bb) {
    if (remaining_ > 0.0) {                 // locked out -- burn down the timer first
        remaining_ -= dt;
        if (remaining_ > 0.0) return Status::Failure;   // still locked this tick
        remaining_ = 0.0;                   // expired exactly on/within this tick -> fall through
    }
    Status s = child_->tick(dt, bb);
    if (s == Status::Success) {             // start the lockout on success
        remaining_ = cd_;
        child_->reset();
    }
    return s;                               // Running/Failure pass straight through
}

void Cooldown::reset() { remaining_ = 0.0; child_->reset(); }

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

Status Condition::tick(double /*dt*/, Blackboard& bb) {
    return pred_(bb) ? Status::Success : Status::Failure;
}

Status Action::tick(double dt, Blackboard& bb) {
    return fn_(dt, bb);
}

Status Wait::tick(double dt, Blackboard& /*bb*/) {
    elapsed_ += dt;
    if (elapsed_ >= dur_) { elapsed_ = 0.0; return Status::Success; }
    return Status::Running;
}

Status SetBlackboard::tick(double /*dt*/, Blackboard& bb) {
    bb.setNumber(key_, value_);
    return Status::Success;
}

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

Status Tree::tick(double dt) {
    if (!root_) return Status::Failure;
    Status s = root_->tick(dt, bb_);
    if (s != Status::Running) root_->reset();   // re-evaluate from scratch next tick
    return s;
}

// ===========================================================================
// GATE
// ===========================================================================
bool runBehaviorTreeGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[bt] GATE BEHAVIORTREE -- tick-based policy: Sequence/Selector order, RUNNING resume, timed Wait, gated Action, Inverter\n");
    bool pass = true;

    // Helper leaves that record how many times they were ticked, to prove
    // ordering + resume semantics rather than just final status.
    auto counting = [](int& hits, Status ret) {
        return std::make_unique<Action>([&hits, ret](double, Blackboard&) {
            ++hits; return ret;
        });
    };

    // --- 1) Sequence: all-Success in order returns Success; a Failure fails it.
    {
        int a = 0, b = 0, c = 0;
        Sequence seq;
        seq.add(counting(a, Status::Success));
        seq.add(counting(b, Status::Success));
        seq.add(counting(c, Status::Success));
        Blackboard bb;
        Status s = seq.tick(1.0, bb);
        bool ok = (s == Status::Success) && a == 1 && b == 1 && c == 1;
        printf("[bt]   Sequence all-success -> Success, each child ticked once (a=%d b=%d c=%d)=%s  %s\n",
               a, b, c, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- NEG-CTRL: an always-Failure leaf in the middle fails the Sequence and
    // the child AFTER it is never reached (fail-fast).
    {
        int a = 0, bad = 0, after = 0;
        Sequence seq;
        seq.add(counting(a, Status::Success));
        seq.add(counting(bad, Status::Failure));   // always fails
        seq.add(counting(after, Status::Success));
        Blackboard bb;
        Status s = seq.tick(1.0, bb);
        bool ok = (s == Status::Failure) && a == 1 && bad == 1 && after == 0;
        printf("[bt]   NEG-CTRL Sequence fails on Failure leaf, downstream skipped (after=%d want 0)=%s  %s\n",
               after, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 2) Selector: returns Success on the first succeeding child and does
    // NOT tick the rest.
    {
        int first = 0, second = 0;
        Selector sel;
        sel.add(counting(first, Status::Success));
        sel.add(counting(second, Status::Success));
        Blackboard bb;
        Status s = sel.tick(1.0, bb);
        bool ok = (s == Status::Success) && first == 1 && second == 0;
        printf("[bt]   Selector takes first success, skips rest (second=%d want 0)=%s  %s\n",
               second, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- NEG-CTRL: a Selector of all-Failure children falls through to Failure
    // (having tried every one).
    {
        int f1 = 0, f2 = 0;
        Selector sel;
        sel.add(counting(f1, Status::Failure));
        sel.add(counting(f2, Status::Failure));
        Blackboard bb;
        Status s = sel.tick(1.0, bb);
        bool ok = (s == Status::Failure) && f1 == 1 && f2 == 1;
        printf("[bt]   NEG-CTRL Selector all-fail -> Failure, both tried (f1=%d f2=%d)=%s  %s\n",
               f1, f2, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 3) Wait(0.5): Running until t elapses, then Success. Drive with dt.
    {
        Wait w(0.5);
        Blackboard bb;
        Status s1 = w.tick(0.2, bb);   // 0.2  -> Running
        Status s2 = w.tick(0.2, bb);   // 0.4  -> Running
        Status s3 = w.tick(0.2, bb);   // 0.6  -> Success (crosses 0.5)
        bool ok = s1 == Status::Running && s2 == Status::Running && s3 == Status::Success;
        printf("[bt]   Wait(0.5) Running,Running,Success across dt-ticks=%s  %s\n",
               ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 4) Sequence RESUME: [Wait(0.3), Action]. The Action must fire ONCE,
    // only after the Wait completes -- proving the running child is remembered
    // and the succeeded prefix is not re-ticked / the action not re-issued.
    {
        int actHits = 0;
        Sequence seq;
        seq.add(std::make_unique<Wait>(0.3));
        seq.add(std::make_unique<Action>([&actHits](double, Blackboard&) {
            ++actHits; return Status::Success;
        }));
        Blackboard bb;
        Status s1 = seq.tick(0.2, bb);   // Wait running
        Status s2 = seq.tick(0.2, bb);   // Wait done (0.4>=0.3) -> Action fires -> Success
        bool ok = s1 == Status::Running && s2 == Status::Success && actHits == 1;
        printf("[bt]   Sequence resume: Action fires once only after Wait done (hits=%d want 1)=%s  %s\n",
               actHits, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 5) Condition gates an Action: the Action runs ONLY when the predicate
    // is true. Same subtree, two blackboard states.
    {
        auto gated = [](int& hits) {
            auto seq = std::make_unique<Sequence>();
            seq->add(std::make_unique<Condition>([](Blackboard& bb) {
                return bb.getBool("enabled", false);
            }));
            seq->add(std::make_unique<Action>([&hits](double, Blackboard&) {
                ++hits; return Status::Success;
            }));
            return seq;
        };
        int hitsOn = 0, hitsOff = 0;
        Blackboard bbOff;  // enabled unset -> false
        auto sOff = gated(hitsOff)->tick(1.0, bbOff);
        Blackboard bbOn; bbOn.setBool("enabled", true);
        auto sOn = gated(hitsOn)->tick(1.0, bbOn);
        bool ok = sOff == Status::Failure && hitsOff == 0
               && sOn  == Status::Success && hitsOn  == 1;
        printf("[bt]   Condition-gated Action: off->Failure hits=%d(0), on->Success hits=%d(1)=%s  %s\n",
               hitsOff, hitsOn, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 6) Inverter flips Success<->Failure (and passes Running through).
    {
        Blackboard bb;
        Inverter invS(std::make_unique<Action>([](double, Blackboard&) { return Status::Success; }));
        Inverter invF(std::make_unique<Action>([](double, Blackboard&) { return Status::Failure; }));
        Inverter invR(std::make_unique<Action>([](double, Blackboard&) { return Status::Running; }));
        bool ok = invS.tick(1.0, bb) == Status::Failure
               && invF.tick(1.0, bb) == Status::Success
               && invR.tick(1.0, bb) == Status::Running;
        printf("[bt]   Inverter: S->F, F->S, R->R=%s  %s\n",
               ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- 7) Realistic composite: "settle 0.1s, THEN if force < threshold
    // succeed else fail". Runs across ticks. Two runs prove BOTH branches:
    //   (a) force below limit -> Success ; (b) force above limit -> Failure,
    // and in both the check only happens AFTER the settle Wait completes.
    {
        auto build = [] {
            auto seq = std::make_unique<Sequence>();
            seq->add(std::make_unique<Wait>(0.1));                 // settle
            seq->add(std::make_unique<Condition>([](Blackboard& bb) {
                return bb.getNumber("force", 1e9) < bb.getNumber("threshold", 0.0);
            }));
            return seq;
        };

        // (a) force 3 < threshold 5 -> after settling, Success.
        {
            auto seq = build();
            Blackboard bb; bb.setNumber("force", 3.0); bb.setNumber("threshold", 5.0);
            Status s1 = seq->tick(0.06, bb);   // 0.06 -> Wait Running (Condition not yet evaluated)
            Status s2 = seq->tick(0.06, bb);   // 0.12 -> Wait done -> Condition true -> Success
            bool ok = s1 == Status::Running && s2 == Status::Success;
            printf("[bt]   Composite settle-then-check, force<thr: Running then Success=%s  %s\n",
                   ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
        // (b) force 9 > threshold 5 -> after settling, Failure.
        {
            auto seq = build();
            Blackboard bb; bb.setNumber("force", 9.0); bb.setNumber("threshold", 5.0);
            Status s1 = seq->tick(0.06, bb);   // Wait Running
            Status s2 = seq->tick(0.06, bb);   // Wait done -> Condition false -> Failure
            bool ok = s1 == Status::Running && s2 == Status::Failure;
            printf("[bt]   Composite settle-then-check, force>thr: Running then Failure=%s  %s\n",
                   ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
    }

    // --- 8) Parallel N-of-M: 2-of-3 succeeds when the 2nd success lands; a
    // 3-of-3 over the same {S,S,F} set fails (cannot reach the threshold).
    {
        Blackboard bb;
        {
            Parallel par(2);
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Success; }));
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Success; }));
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Failure; }));
            bool ok = par.tick(1.0, bb) == Status::Success;
            printf("[bt]   Parallel 2-of-3 {S,S,F} -> Success=%s  %s\n",
                   ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
        {
            Parallel par(3);
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Success; }));
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Success; }));
            par.add(std::make_unique<Action>([](double, Blackboard&) { return Status::Failure; }));
            bool ok = par.tick(1.0, bb) == Status::Failure;
            printf("[bt]   Parallel 3-of-3 {S,S,F} -> Failure (threshold unreachable)=%s  %s\n",
                   ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
    }

    // --- 9) Decorators: Repeat(3) succeeds after 3 child successes;
    // RetryUntilSuccess turns a fail-twice-then-succeed child into Success;
    // Timeout forces Failure on a never-ending child; Cooldown blocks re-fire.
    {
        Blackboard bb;
        {
            int hits = 0;
            Repeat rep(std::make_unique<Action>([&hits](double, Blackboard&) {
                ++hits; return Status::Success;
            }), 3);
            Status s = rep.tick(1.0, bb);
            bool ok = s == Status::Success && hits == 3;
            printf("[bt]   Repeat(3): Success after 3 iters (hits=%d)=%s  %s\n",
                   hits, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
        {
            int attempt = 0;
            RetryUntilSuccess retry(std::make_unique<Action>([&attempt](double, Blackboard&) {
                ++attempt; return attempt >= 3 ? Status::Success : Status::Failure;
            }), 0);
            Status s = retry.tick(1.0, bb);   // fails at 1,2 then succeeds at 3, all this tick
            bool ok = s == Status::Success && attempt == 3;
            printf("[bt]   RetryUntilSuccess: Success on 3rd attempt (attempt=%d)=%s  %s\n",
                   attempt, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
        {
            Timeout to(std::make_unique<Action>([](double, Blackboard&) { return Status::Running; }), 0.25);
            Status s1 = to.tick(0.1, bb);    // 0.1  -> Running
            Status s2 = to.tick(0.1, bb);    // 0.2  -> Running
            Status s3 = to.tick(0.1, bb);    // 0.3  -> budget blown -> Failure
            bool ok = s1 == Status::Running && s2 == Status::Running && s3 == Status::Failure;
            printf("[bt]   Timeout(0.25) on endless child -> Failure at deadline=%s  %s\n",
                   ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
        {
            int hits = 0;
            Cooldown cd(std::make_unique<Action>([&hits](double, Blackboard&) {
                ++hits; return Status::Success;
            }), 0.5);
            Status s1 = cd.tick(0.1, bb);   // fires -> Success, lockout 0.5
            Status s2 = cd.tick(0.1, bb);   // locked -> Failure (child NOT re-run)
            Status s3 = cd.tick(0.5, bb);   // lockout expired -> fires again -> Success
            bool ok = s1 == Status::Success && s2 == Status::Failure && s3 == Status::Success && hits == 2;
            printf("[bt]   Cooldown(0.5): fire,block,fire (hits=%d want 2)=%s  %s\n",
                   hits, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
            pass &= ok;
        }
    }

    // --- 10) Tree wrapper: root Success resets so the policy re-evaluates every
    // tick (a reactive controller re-runs continuously, not one-shot).
    {
        int hits = 0;
        auto root = std::make_unique<Action>([&hits](double, Blackboard&) {
            ++hits; return Status::Success;
        });
        Tree tree(std::move(root));
        Status s1 = tree.tick(1.0);
        Status s2 = tree.tick(1.0);
        bool ok = s1 == Status::Success && s2 == Status::Success && hits == 2;
        printf("[bt]   Tree re-evaluates each tick (Success twice, hits=%d want 2)=%s  %s\n",
               hits, ok ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    printf("[bt] %s\n", pass ? "ALL PASS (behavior-tree policy substrate verified)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::policy
