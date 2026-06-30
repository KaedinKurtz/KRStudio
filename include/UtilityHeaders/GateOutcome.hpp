#pragma once
// GateOutcome.hpp -- tri-state (PASS / FAIL / SKIP) for the overnight-bench dashboard.
//
// Most gates return bool. A gate that cannot run because an *environmental prerequisite* is
// absent (no MQTT broker daemon installed; the optional YCB asset pack not downloaded) should
// not report a hard FAIL -- that conflates "the code is broken" with "this machine lacks a
// dependency". Such a gate calls krs::gate::skip() and returns true; the dashboard then shows
// SKIP, distinct from a genuine PASS, so a vacuous green can never masquerade as a real pass and
// a skipped gate never fails the bench.
//
// Mechanism: skip() sets a thread-local flag. The dashboard stores each gate's bool result in a
// GateOutcome whose implicit constructor consumes the flag. Braced-init-list elements are
// evaluated left-to-right with all side effects sequenced (C++ [dcl.init.list]/4), so the flag
// set inside gateFn() is read by the very next GateOutcome(bool) before any other gate runs.
// Existing `{ "name", gateFn() }` dashboard entries compile unchanged (bool -> GateOutcome).
namespace krs::gate {

inline thread_local bool t_skip = false;

// Call from inside a gate (then `return true`) to report SKIP rather than PASS/FAIL.
inline void skip() { t_skip = true; }

struct GateOutcome {
    bool pass = false;
    bool skipped = false;
    GateOutcome(bool ok) {          // implicit, by design
        skipped = ok && t_skip;     // a real FAIL is never a skip
        pass    = ok && !t_skip;
        t_skip  = false;            // consume
    }
};

} // namespace krs::gate
